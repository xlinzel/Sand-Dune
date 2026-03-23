#include <grains/piv.h>
#include <algorithm>
#include <fftw3.h>
#include <cmath>
#include <numbers>

PIV::PIV()
{
    AllocateFFTBuffers();
}

PIV::PIV(const int window_size, const int overlap, const int search_size)
    : window_size(window_size), overlap(overlap), search_size(search_size)
{
    AllocateFFTBuffers();
}

PIV::PIV(const PIVParameters parameters)
    : window_size(parameters.window_size), overlap(parameters.overlap), search_size(parameters.search_size)
{
    AllocateFFTBuffers();
}

PIV::~PIV()
{
    FreeFFTBuffers();
}

void PIV::AllocateFFTBuffers()
{
    //Rows and collumns are the same here, but may be different at somepoint who know, jsut for cflarity they are speerate variables
    rows = search_size;
    cols = search_size;
    freq_cols = (floor(search_size / 2) + 1);

    ref_in = (float*) fftwf_alloc_real(rows * cols);
    flow_in = (float*) fftwf_alloc_real(rows * cols);
    ref_out = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);
    flow_out = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);
    product  = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * rows * freq_cols);
    ccmap_raw = (float*)        fftwf_malloc(sizeof(float)         * rows * cols);

    ref_plan  = fftwf_plan_dft_r2c_2d(rows, cols, ref_in,  ref_out,  FFTW_MEASURE);
    flow_plan = fftwf_plan_dft_r2c_2d(rows, cols, flow_in, flow_out, FFTW_MEASURE);
    inv_plan  = fftwf_plan_dft_c2r_2d(rows, cols, product, ccmap_raw, FFTW_MEASURE);

    //Hann Window Preprocessing
    Eigen::VectorXf hannr(rows), hannc(cols);

    for(int i = 0; i < rows; i++)
        hannr(i) = 0.5f * (1.0f - cos((2.0f * i * std::numbers::pi) / (rows - 1)));
    for(int j = 0; j < cols; j++)
        hannc(j) = 0.5f * (1.0f - cos((2.0f * j * std::numbers::pi) / (cols - 1)));

    hann2d = hannr * hannc.transpose();
}

void PIV::FreeFFTBuffers()
{
    if(ref_plan) fftwf_destroy_plan(ref_plan);
    if(flow_plan) fftwf_destroy_plan(flow_plan);
    if(inv_plan) fftwf_destroy_plan(inv_plan);
    if(ref_in) fftwf_free(ref_in);
    if(flow_in) fftwf_free(flow_in);
    if(ref_out) fftwf_free(ref_out);
    if(flow_out) fftwf_free(flow_out);
    if(product) fftwf_free(product);
    if(ccmap_raw) fftwf_free(ccmap_raw);
}

VectorField PIV::Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                        std::function<void(float)> on_progress)
{
    Eigen::Vector2i ref_size(reference.rows(), reference.cols());
    Eigen::Vector2i flow_size(flow.rows(), flow.cols());

    if(ref_size(0) != flow_size(0) || ref_size(1) != flow_size(1))
    {
        return VectorField();
    }

    int margin = (search_size - window_size) / 2;
    int movement = window_size - overlap;

    int total_windows = floor(ref_size(0) / movement) * floor(ref_size(1) / movement);
    int completed = 0;

    VectorField vectorfield(floor(ref_size(0) / movement),floor(ref_size(1) / movement));

    for(int win_row = 0; win_row < floor(ref_size(0) / movement); win_row++)
    {
        for(int win_col = 0; win_col < floor(ref_size(1) / movement); win_col++)
        {
            Eigen::MatrixXf w_reference = Eigen::MatrixXf::Zero(window_size, window_size);

            w_reference.block(
                0, 0,
                std::min(window_size, ref_size(0) - win_row * movement),
                std::min(window_size, ref_size(1) - win_col * movement)) 
                
            = reference.block(
                win_row * movement, win_col * movement,
                std::min(window_size, ref_size(0) - win_row * movement),
                std::min(window_size, ref_size(1) - win_col * movement));

            Eigen::MatrixXf w_flow = Eigen::MatrixXf::Zero(search_size, search_size);

            w_flow.block(
                std::max(0, margin - win_row * movement),
                std::max(0, margin - win_col * movement),
                std::min(search_size - std::max(0, margin - win_row * movement), ref_size(0) - std::max(0, win_row * movement - margin)),
                std::min(search_size - std::max(0, margin - win_col * movement), ref_size(1) - std::max(0, win_col * movement - margin)))
            
            = flow.block(
                std::max(0, win_row * movement - margin),
                std::max(0, win_col * movement - margin),
                std::min(search_size - std::max(0, margin - win_row * movement), ref_size(0) - std::max(0, win_row * movement - margin)),
                std::min(search_size - std::max(0, margin - win_col * movement), ref_size(1) - std::max(0, win_col * movement - margin)));
            
            //Check total "energy" of the reference window, if it is below 1%, assume there is essentially nothign there
                //May need to test teh percent here.
            float energy = w_reference.array().abs().sum();
            if(energy < window_size * window_size * 0.01f)
            {
                vectorfield.u(win_row, win_col) = 0.0f;
                vectorfield.v(win_row, win_col) = 0.0f;
                vectorfield.s2n(win_row, win_col) = 0.0f;
                continue;
            }

            Eigen::MatrixXf ccmap = CrossCorrelationFFT(w_reference, w_flow);

            PeakResult peak = FindPeak(ccmap);
            vectorfield.u(win_row, win_col) = peak.u;
            vectorfield.v(win_row, win_col) = peak.v;
            vectorfield.s2n(win_row, win_col) = peak.s2n;

            if(on_progress) on_progress(++completed / (float)total_windows);
        }
    }

    return vectorfield;
}

Eigen::MatrixXf PIV::CrossCorrelationFFT(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow)
{
    Eigen::Vector2i ref_size(w_reference.rows(), w_reference.cols());
    Eigen::Vector2i flow_size(w_flow.rows(), w_flow.cols());

    //Zero pad reference frame to match flow frame (reduces circular artifacting)
    Eigen::MatrixXf w_refpad = Eigen::MatrixXf::Zero(w_flow.rows(), w_flow.cols());
    w_refpad.block(
        (flow_size(0) - ref_size(0)) / 2, 
        (flow_size(1) - ref_size(1)) / 2,
        ref_size(0), ref_size(1))
    = w_reference;
    
    //Apply Hann Window
    Eigen::MatrixXf ref_hann = w_refpad.array() * hann2d.array();
    Eigen::MatrixXf flow_hann = w_flow.array() * hann2d.array();

    //Copy data into a row major matrix for more efficient FFT buffer filling
        //FFT uses row major storage, while Eigen by default uses collumn major
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ref_rm = ref_hann;
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> flow_rm = flow_hann;

    memcpy(ref_in, ref_rm.data(), sizeof(float) * flow_size(0) * flow_size(1));
    memcpy(flow_in, flow_rm.data(), sizeof(float) * flow_size(0) * flow_size(1));

    //Execute FFT
    fftwf_execute(ref_plan);
    fftwf_execute(flow_plan);

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

Eigen::MatrixXf PIV::CrossCorrelationSpatial(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow)
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

PIV::PeakResult PIV::FindPeak(const Eigen::MatrixXf& ccmap)
{
    int row, col;

    float peak = ccmap.maxCoeff(&row, &col);

    if ((row == 0 || row == ccmap.rows() - 1 || col == 0 || col == ccmap.cols() - 1))
    {
        //SCC PPR singal to noise ratio calculations
        Eigen::MatrixXf ccmap_flattened = ccmap.array() - ccmap.minCoeff();

        //Get the peak on the subtracted plane
        peak = ccmap_flattened.maxCoeff();

        int mask_size = 5;
        int r0 = std::max(0, row - mask_size);
        int c0 = std::max(0, col - mask_size);
        int r1 = std::min((int)ccmap.rows(), row + mask_size + 1);
        int c1 = std::min((int)ccmap.cols(), col + mask_size + 1);

        ccmap_flattened.block(r0, c0, r1 - r0, c1 - c0).setZero();

        float second_peak = ccmap_flattened.maxCoeff();

        float sig2noise = peak / second_peak;
        
        return PeakResult{float(col - ccmap.cols()/2), float(row - ccmap.rows()/2), sig2noise};
    }
    else if (ccmap(row, col-1) <= 0 || ccmap(row, col+1) <= 0 || ccmap(row-1, col) <= 0 || ccmap(row+1, col) <= 0)
    {
        //SCC PPR singal to noise ratio calculations
        Eigen::MatrixXf ccmap_flattened = ccmap.array() - ccmap.minCoeff();

        //Get the peak on the subtracted plane
        peak = ccmap_flattened.maxCoeff();

        int mask_size = 5;
        int r0 = std::max(0, row - mask_size);
        int c0 = std::max(0, col - mask_size);
        int r1 = std::min((int)ccmap.rows(), row + mask_size + 1);
        int c1 = std::min((int)ccmap.cols(), col + mask_size + 1);

        ccmap_flattened.block(r0, c0, r1 - r0, c1 - c0).setZero();

        float second_peak = ccmap_flattened.maxCoeff();

        float sig2noise = peak / second_peak;

        return PeakResult{float(col - ccmap.cols()/2), float(row - ccmap.rows()/2), sig2noise};
    }

    //Gaussian Interpolation
    float x_interp = col + (std::log(ccmap(row, col - 1)) - std::log(ccmap(row, col + 1)))
                            / (2 * std::log(ccmap(row, col - 1)) - 4 * std::log(ccmap(row, col)) + 2 * std::log(ccmap(row, col + 1)));

    float y_interp = row + (std::log(ccmap(row - 1, col)) - std::log(ccmap(row + 1, col)))
                            / (2 * std::log(ccmap(row - 1, col)) - 4 * std::log(ccmap(row, col)) + 2 * std::log(ccmap(row + 1, col)));

    float u = x_interp - ccmap.cols() / 2;
    float v = y_interp - ccmap.rows() / 2;
    
    //SCC PPR singal to noise ratio calculations
    Eigen::MatrixXf ccmap_flattened = ccmap.array() - ccmap.minCoeff();

    //Get the peak on the subtracted plane
    peak = ccmap_flattened.maxCoeff();

    int mask_size = 5;
    int r0 = std::max(0, row - mask_size);
    int c0 = std::max(0, col - mask_size);
    int r1 = std::min((int)ccmap.rows(), row + mask_size + 1);
    int c1 = std::min((int)ccmap.cols(), col + mask_size + 1);

    ccmap_flattened.block(r0, c0, r1 - r0, c1 - c0).setZero();

    float second_peak = ccmap_flattened.maxCoeff();

    float sig2noise = peak / second_peak;

    return PeakResult{u, v, sig2noise};
}

int PIV::GetWindowSize() const
{
    return window_size;
}

int PIV::GetOverlap() const
{
    return overlap;
}

int PIV::GetSearchSize() const
{
    return search_size;
}

void PIV::SetWindowSize(const int size)
{
    window_size = size;
}

void PIV::SetOverlap(const int overlap)
{
    this->overlap = overlap;
}

void PIV::SetSearchSize(const int size)
{
    search_size = size;

    FreeFFTBuffers();
    AllocateFFTBuffers();
}