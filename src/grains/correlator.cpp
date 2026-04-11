#include <grains/correlator.h>
#include <algorithm>
#include <fftw3.h>
#include <cmath>
#include <chrono>
#include <numeric>


//////////////////////////////////////////////////////
// Construction And Buffer Lifetime
//////////////////////////////////////////////////////

Correlator::Correlator()
{
    AllocateFFTBuffers();
}

Correlator::Correlator(const int window_size, const int overlap)
    : window_size(window_size), overlap(overlap)
{
    AllocateFFTBuffers();
}

Correlator::Correlator(const CorrelatorParameters parameters)
    : window_size(parameters.window_size), overlap(parameters.overlap)
{
    AllocateFFTBuffers();
}

Correlator::~Correlator()
{
    FreeFFTBuffers();
}

void Correlator::AllocateFFTBuffers()
{
    //Rows and collumns are the same here, but may be different at somepoint who know, jsut for cflarity they are speerate variables
    rows = window_size;
    cols = window_size;
    freq_cols = (floor(window_size / 2) + 1);

    ref_in = (float*) fftwf_alloc_real(rows * cols);
    flow_in = (float*) fftwf_alloc_real(rows * cols);
    ref_out = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);
    flow_out = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);
    product  = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * rows * freq_cols);
    ref_out_saved = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);
    ccmap_raw = (float*)        fftwf_malloc(sizeof(float)         * rows * cols);

    ref_plan  = fftwf_plan_dft_r2c_2d(rows, cols, ref_in,  ref_out,  FFTW_MEASURE);
    flow_plan = fftwf_plan_dft_r2c_2d(rows, cols, flow_in, flow_out, FFTW_MEASURE);
    inv_plan  = fftwf_plan_dft_c2r_2d(rows, cols, product, ccmap_raw, FFTW_MEASURE);

}

void Correlator::FreeFFTBuffers()
{
    if(ref_plan) fftwf_destroy_plan(ref_plan);
    if(flow_plan) fftwf_destroy_plan(flow_plan);
    if(inv_plan) fftwf_destroy_plan(inv_plan);
    if(ref_out_saved) fftwf_free(ref_out_saved);
    if(ref_in) fftwf_free(ref_in);
    if(flow_in) fftwf_free(flow_in);
    if(ref_out) fftwf_free(ref_out);
    if(flow_out) fftwf_free(flow_out);
    if(product) fftwf_free(product);
    if(ccmap_raw) fftwf_free(ccmap_raw);
}

//////////////////////////////////////////////////////
// Windowed Correlation Driver
//////////////////////////////////////////////////////

VectorField Correlator::Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                        std::function<void(float)> on_progress)
{
    Eigen::Vector2i ref_size(reference.rows(), reference.cols());
    Eigen::Vector2i flow_size(flow.rows(), flow.cols());

    if(ref_size(0) != flow_size(0) || ref_size(1) != flow_size(1))
    {
        return VectorField();
    }

    int movement = window_size - overlap;
    int num_rows = floor(ref_size(0) / movement);
    int num_cols = floor(ref_size(1) / movement);

    int total_windows = num_rows * num_cols;
    int completed = 0;

    // Profiling accumulators -- remove after tuning
    double t_patch = 0, t_phi = 0, t_R = 0, t_gamma = 0, t_nr = 0;
    int window_count = 0;

    VectorField vectorfield(num_rows, num_cols);
    Eigen::MatrixXf padded_reference(window_size, window_size);
    Eigen::MatrixXf padded_flow(window_size, window_size);

    for(int win_row = 0; win_row < num_rows; win_row++)
    {
        for(int win_col = 0; win_col < num_cols; win_col++)
        {
            int start_row = win_row * movement;
            int start_col = win_col * movement;
            int block_rows = std::min(window_size, ref_size(0) - start_row);
            int block_cols = std::min(window_size, ref_size(1) - start_col);
            bool full_window = (block_rows == window_size && block_cols == window_size);

            if(full_window)
            {
                auto ref_window = reference.block(start_row, start_col, window_size, window_size);
                auto flow_window = flow.block(start_row, start_col, window_size, window_size);

                //Check total variance of the reference window, if it is below 0.1%, assume there is essentially nothign there
                //May need to test teh percent here.
                float mean = ref_window.mean();
                float variance = (ref_window.array() - mean).square().sum() / (window_size * window_size);
                if(variance < 0.001f)
                {
                    vectorfield.u(win_row, win_col) = 0.0f;
                    vectorfield.v(win_row, win_col) = 0.0f;
                    vectorfield.s2n(win_row, win_col) = 0.0f;
                    continue;
                }

                Eigen::MatrixXf ccmap = CrossCorrelationFFT(ref_window, flow_window);

                PeakResult peak = FindPeak(ccmap, t_patch, t_phi, t_R, t_gamma, t_nr);
                window_count++;
                vectorfield.u(win_row, win_col) = peak.u;
                vectorfield.v(win_row, win_col) = peak.v;
                vectorfield.s2n(win_row, win_col) = peak.s2n;
            }
            else
            {
                padded_reference.setZero();
                padded_flow.setZero();
                padded_reference.block(0, 0, block_rows, block_cols) =
                    reference.block(start_row, start_col, block_rows, block_cols);
                padded_flow.block(0, 0, block_rows, block_cols) =
                    flow.block(start_row, start_col, block_rows, block_cols);

                float mean = padded_reference.mean();
                float variance = (padded_reference.array() - mean).square().sum() / (window_size * window_size);
                if(variance < 0.001f)
                {
                    vectorfield.u(win_row, win_col) = 0.0f;
                    vectorfield.v(win_row, win_col) = 0.0f;
                    vectorfield.s2n(win_row, win_col) = 0.0f;
                    continue;
                }

                Eigen::MatrixXf ccmap = CrossCorrelationFFT(padded_reference, padded_flow);

                PeakResult peak = FindPeak(ccmap, t_patch, t_phi, t_R, t_gamma, t_nr);
                window_count++;
                vectorfield.u(win_row, win_col) = peak.u;
                vectorfield.v(win_row, win_col) = peak.v;
                vectorfield.s2n(win_row, win_col) = peak.s2n;
            }

            if(on_progress) on_progress(++completed / (float)total_windows);
        }
    }

    // Keep optional FindPeak profiling available for debugging, but leave it
    // disabled during normal runs because the timing dump floods the console.
    constexpr bool kPrintFindPeakTiming = false;
    if(kPrintFindPeakTiming && window_count > 0)
    {
        double total = t_patch + t_phi + t_R + t_gamma + t_nr;
        printf("\n--- FindPeak timing over %d windows ---\n", window_count);
        printf("Patch extraction:  %6.2f ms  (%4.1f%%)\n", t_patch*1e3,  100*t_patch/total);
        printf("Local phi:         %6.2f ms  (%4.1f%%)\n", t_phi*1e3,    100*t_phi/total);
        printf("Autocorr R:        %6.2f ms  (%4.1f%%)\n", t_R*1e3,      100*t_R/total);
        printf("Interp prep:       %6.2f ms  (%4.1f%%)\n", t_gamma*1e3,  100*t_gamma/total);
        printf("Subpixel search:   %6.2f ms  (%4.1f%%)\n", t_nr*1e3,     100*t_nr/total);
        printf("Total in FindPeak: %6.2f ms\n", total*1e3);
        printf("Per window:        %6.4f ms\n", total*1e3/window_count);
    }

    return vectorfield;
}

//////////////////////////////////////////////////////
// Correlation Kernels
//////////////////////////////////////////////////////

Eigen::MatrixXf Correlator::CrossCorrelationFFT(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                         const Eigen::Ref<const Eigen::MatrixXf>& w_flow)
{
    Eigen::Vector2i size(w_reference.rows(), w_reference.cols());

    //Copy data into a row major matrix for more efficient FFT buffer filling
        //FFT uses row major storage, Eigen by default uses collumn major
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ref_rm = w_reference;
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> flow_rm = w_flow;

    ref_rm.array() -= ref_rm.mean();
    flow_rm.array() -= flow_rm.mean();

    memcpy(ref_in, ref_rm.data(), sizeof(float) * size(0) * size(1));
    memcpy(flow_in, flow_rm.data(), sizeof(float) * size(0) * size(1));

    //Execute FFT
    fftwf_execute(ref_plan);
    fftwf_execute(flow_plan);

    //Save fft
    memcpy(ref_out_saved, ref_out, sizeof(fftwf_complex) * rows * freq_cols);

    //Actual Cross Correlation (ref* x flow)
    for(int i = 0; i < rows * freq_cols; i++)
    {
        product[i][0] = ref_out[i][0] * flow_out[i][0] + ref_out[i][1] * flow_out[i][1];
        product[i][1] = ref_out[i][0] * flow_out[i][1] - ref_out[i][1] * flow_out[i][0];
    }

    fftwf_execute(inv_plan);

    //Converting ccmap raw to row major eigen for efficient data copying
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ccmap_rm(rows, cols);
    memcpy(ccmap_rm.data(), ccmap_raw,sizeof(float) * rows * cols);

    ccmap_rm *= (1.0f / float(rows * cols));

    Eigen::MatrixXf ccmap = ccmap_rm; //Cross correlation map

    //FFT inverse creates a shifted spatial mapping (by 1/2 grid)
    Eigen::MatrixXf ccmap_shifted(rows, cols);
    int half_r = rows / 2;
    int half_c = cols / 2;
    ccmap_shifted.block(0, 0, half_r, half_c) = ccmap.block(half_r, half_c, half_r, half_c);
    ccmap_shifted.block(half_r, half_c, half_r, half_c) = ccmap.block(0, 0, half_r, half_c);
    ccmap_shifted.block(0,  half_c, half_r, half_c) = ccmap.block(half_r, 0, half_r, half_c);
    ccmap_shifted.block(half_r,  0, half_r, half_c) = ccmap.block(0, half_c, half_r, half_c);

    return ccmap_shifted;
}

Eigen::MatrixXf Correlator::CrossCorrelationSpatial(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                             const Eigen::Ref<const Eigen::MatrixXf>& w_flow)
{
    Eigen::Vector2i ref_size(w_reference.rows(), w_reference.cols());
    Eigen::Vector2i flow_size(w_flow.rows(), w_flow.cols());

    Eigen::MatrixXf ccmap(flow_size(0) + ref_size(0) - 1, flow_size(1) + ref_size(1) - 1); //Cross correlation map

    for(int row_offset = 0; row_offset < flow_size(0) + ref_size(0) - 1; row_offset++)
    {
        for(int col_offset = 0; col_offset < flow_size(1) + ref_size(1) - 1; col_offset++)
        {
            Eigen::MatrixXf ref_overlap = w_reference.block(
                std::clamp(ref_size(0) - row_offset - 1, 0, ref_size(0)), 
                std::clamp(ref_size(1) - col_offset - 1, 0, ref_size(1)),
                std::min({row_offset + 1, ref_size(0), flow_size(0), ref_size(0) + flow_size(0) - 1 - row_offset}),
                std::min({col_offset + 1, ref_size(1), flow_size(1), ref_size(1) + flow_size(1) - 1 - col_offset}));

            Eigen::MatrixXf flow_overlap = w_flow.block(
                std::clamp(row_offset - ref_size(0) + 1, 0, flow_size(0)),
                std::clamp(col_offset - ref_size(1) + 1, 0, flow_size(1)),
                std::min({row_offset + 1, ref_size(0), flow_size(0), ref_size(0) + flow_size(0) - 1 - row_offset}),
                std::min({col_offset + 1, ref_size(1), flow_size(1), ref_size(1) + flow_size(1) - 1 - col_offset}));

            float sum = (ref_overlap.array() * flow_overlap.array()).sum();

            ccmap(row_offset, col_offset) = sum;
        }
    }

    return ccmap;
}

//////////////////////////////////////////////////////
// Simple Accessors
//////////////////////////////////////////////////////

int Correlator::GetWindowSize() const
{
    return window_size;
}

int Correlator::GetOverlap() const
{
    return overlap;
}

void Correlator::SetWindowSize(const int size)
{
    window_size = size;
}

void Correlator::SetOverlap(const int overlap)
{
    this->overlap = overlap;
}
