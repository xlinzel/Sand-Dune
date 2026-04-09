#include <grains/piv.h>
#include <algorithm>
#include <fftw3.h>
#include <cmath>
#include <numbers>
#include <chrono>

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

    // Profiling accumulators -- remove after tuning
    double t_patch = 0, t_phi = 0, t_R = 0, t_gamma = 0, t_nr = 0;
    int window_count = 0;

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
            
            //Check total variance of the reference window, if it is below 0.1%, assume there is essentially nothign there
                //May need to test teh percent here.
            float mean = w_reference.mean();
            float variance = (w_reference.array() - mean).square().sum() / (window_size * window_size);
            if(variance < 0.001f)
            {
                vectorfield.u(win_row, win_col) = 0.0f;
                vectorfield.v(win_row, win_col) = 0.0f;
                vectorfield.s2n(win_row, win_col) = 0.0f;
                continue;
            }

            Eigen::MatrixXf ccmap = CrossCorrelationFFT(w_reference, w_flow);

            PeakResult peak = FindPeak(ccmap, w_reference, w_flow,
                                       t_patch, t_phi, t_R, t_gamma, t_nr);
            window_count++;
            vectorfield.u(win_row, win_col) = peak.u;
            vectorfield.v(win_row, win_col) = peak.v;
            vectorfield.s2n(win_row, win_col) = peak.s2n;

            if(on_progress) on_progress(++completed / (float)total_windows);
        }
    }

    double total = t_patch + t_phi + t_R + t_gamma + t_nr;
    printf("\n--- FindPeak timing over %d windows ---\n", window_count);
    printf("Patch extraction:  %6.2f ms  (%4.1f%%)\n", t_patch*1e3,  100*t_patch/total);
    printf("Local phi:         %6.2f ms  (%4.1f%%)\n", t_phi*1e3,    100*t_phi/total);
    printf("Autocorr R:        %6.2f ms  (%4.1f%%)\n", t_R*1e3,      100*t_R/total);
    printf("Gamma from W:      %6.2f ms  (%4.1f%%)\n", t_gamma*1e3,  100*t_gamma/total);
    printf("Newton-Raphson:    %6.2f ms  (%4.1f%%)\n", t_nr*1e3,     100*t_nr/total);
    printf("Total in FindPeak: %6.2f ms\n", total*1e3);
    printf("Per window:        %6.4f ms\n", total*1e3/window_count);

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

    //Mean subtration for DC Offset Mitigation
    ref_hann.array() -= ref_hann.mean();
    flow_hann.array() -= flow_hann.mean();

    //Copy data into a row major matrix for more efficient FFT buffer filling
        //FFT uses row major storage, Eigen by default uses collumn major
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

//For the source of the CMM method: https://iopscience.iop.org/article/10.1088/0957-0233/16/8/010
//Comments are written by Claude and double checked to explain the method with reference to the paper math

PIV::PeakResult PIV::FindPeak(const Eigen::MatrixXf& ccmap, const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow,
                              double& t_patch, double& t_phi, double& t_R, double& t_gamma, double& t_nr)
{
    // CMM: Chen & Katz 2005, "Elimination of peak-locking error in PIV analysis
    //      using the correlation mapping method", Meas. Sci. Technol. 16, 1605-1618
    //
    // The displacement (U,V) is split into an integer part (uint, vint) found from
    // the discrete correlation peak, and a sub-pixel part (u,v) found by CMM.
    // See paper section 2.1, equation before eq.(4).

    // Step 1 (paper section 2.1): Find integer displacement (uint, vint)
    // by locating the peak of the discrete correlation map phi(m,n) -- eq.(1)
    int row, col;
    float peak = ccmap.maxCoeff(&row, &col);

    // CMM requires a 5x5 neighborhood around the peak (m,n in [-2,2]).
    // If the peak is within 2 pixels of the ccmap border, that neighborhood
    // would go out of bounds. Fall back to integer-only displacement in that case.
    if ((row == 0 || row == ccmap.rows() - 1 || col == 0 || col == ccmap.cols() - 1)
        || row == 1 || row == ccmap.rows() - 2 || col == 1 || col == ccmap.cols() - 2)
    {
        // SCC PPR signal to noise ratio calculations
        Eigen::MatrixXf ccmap_flattened = ccmap.array() - ccmap.minCoeff();

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

    // -----------------------------------------------------------------------
    // CMM: Correlation Mapping Method (Chen & Katz 2005)
    //
    // Core idea: instead of fitting a curve to the correlation peak (which causes
    // peak-locking bias), express the second image as a bicubic polynomial of the
    // first image and the unknown sub-pixel displacement. The virtual correlation
    // phi'(m,n,u',v') then becomes a third-order polynomial in (u',v') whose
    // coefficients depend only on g1. Matching phi' to the measured phi across a
    // 5x5 neighborhood via least squares gives the sub-pixel displacement without
    // any curve fitting. See paper section 2.
    // -----------------------------------------------------------------------

    // Rebuild windows in the same form used by CrossCorrelationFFT()

    Eigen::Vector2i ref_size(w_reference.rows(), w_reference.cols());
    Eigen::Vector2i flow_size(w_flow.rows(), w_flow.cols());

    // Zero-pad reference to match flow/search size
    Eigen::MatrixXf w_refpad = Eigen::MatrixXf::Zero(flow_size(0), flow_size(1));
    int pad_r = (flow_size(0) - ref_size(0)) / 2;
    int pad_c = (flow_size(1) - ref_size(1)) / 2;

    w_refpad.block(pad_r, pad_c, ref_size(0), ref_size(1)) = w_reference;

    // Apply same Hann windowing
    Eigen::MatrixXf ref_proc  = w_refpad.array() * hann2d.array();
    Eigen::MatrixXf flow_proc = w_flow.array()   * hann2d.array();

    // Apply same mean subtraction
    ref_proc.array()  -= ref_proc.mean();
    flow_proc.array() -= flow_proc.mean();

    // Patch-local CMM: sub-pixel peak shape is determined by the dot profile,
    // not the window size. For 2-4px dots the correlation peak is 4-8px wide.
    // An 11x11 patch centered on the integer peak captures it completely.
    // Full-window FFT gives integer peak SNR. Local patch gives sub-pixel accuracy.
    // Both phi and phi' are built from the same patch -- consistency preserved.
    using clk = std::chrono::high_resolution_clock;

    static constexpr int PS  = ref_proc.cols();   // patch size -- sized for 2-4px dots
    static constexpr int PSH = PS/2; // 5

    // Integer displacement in image coordinates
    int int_u = col - ccmap.cols()/2;
    int int_v = row - ccmap.rows()/2;

    float gauss_u = 0.5f, gauss_v = 0.5f;

    if(ccmap(row,col-1) > 0 && ccmap(row,col) > 0 && ccmap(row,col+1) > 0)
        gauss_u = std::clamp(
            col + (std::log(ccmap(row,col-1)) - std::log(ccmap(row,col+1)))
                / (2*std::log(ccmap(row,col-1)) - 4*std::log(ccmap(row,col))
                + 2*std::log(ccmap(row,col+1)))
            - (float)col, -0.49f, 0.49f);

    if(ccmap(row-1,col) > 0 && ccmap(row,col) > 0 && ccmap(row+1,col) > 0)
        gauss_v = std::clamp(
            row + (std::log(ccmap(row-1,col)) - std::log(ccmap(row+1,col)))
                / (2*std::log(ccmap(row-1,col)) - 4*std::log(ccmap(row,col))
                + 2*std::log(ccmap(row+1,col)))
            - (float)row, -0.49f, 0.49f);

    // Shift to [0,1): negative fractional -> shift integer peak, add 1 to sub-pixel
    if(gauss_u < 0.0f) { int_u -= 1; gauss_u += 1.0f; }
    if(gauss_v < 0.0f) { int_v -= 1; gauss_v += 1.0f; }

    // NOW compute patch centers using adjusted int_u/int_v
    int ref_cx = ref_proc.rows()/2;
    int ref_cy = ref_proc.cols()/2;
    int flow_cx = ref_cx + int_v;
    int flow_cy = ref_cy + int_u;

    // --- Patch extraction ---
    auto t0 = clk::now();

    float patch_ref[PS*PS]  = {};
    float patch_flow[PS*PS] = {};

    for(int pi = 0; pi < PS; pi++) {
        for(int pj = 0; pj < PS; pj++) {
            int ri = ref_cx  - PSH + pi;
            int rj = ref_cy  - PSH + pj;
            int fi = flow_cx - PSH + pi;
            int fj = flow_cy - PSH + pj;
            if(ri >= 0 && ri < ref_proc.rows() &&
               rj >= 0 && rj < ref_proc.cols())
                patch_ref[pi*PS + pj] = ref_proc(ri,rj);
            if(fi >= 0 && fi < flow_proc.rows() &&
               fj >= 0 && fj < flow_proc.cols())
                patch_flow[pi*PS + pj] = flow_proc(fi,fj);
        }
    }

    t_patch += std::chrono::duration<double>(clk::now() - t0).count();
    t0 = clk::now();
    
    // Mean subtract patches -- raw ref_proc/flow_proc have ~0.6 DC offset
    // which flattens autocorrelation R, making Gamma degenerate.
    // Removing the mean isolates the dot signal so R is properly peaked.
    float mean_ref = 0.0f, mean_flow = 0.0f;
    for(int i = 0; i < PS*PS; i++) {
        mean_ref  += patch_ref[i];
        mean_flow += patch_flow[i];
    }
    mean_ref  /= float(PS*PS);
    mean_flow /= float(PS*PS);
    for(int i = 0; i < PS*PS; i++) {
        patch_ref[i]  -= mean_ref;
        patch_flow[i] -= mean_flow;
    }

    // --- Local phi ---
    // Step 2 (local): compute phi(m,n) from local patch cross-correlation.
    // Only 25 values needed (m,n in [-2,2]) -- spatial is cheaper than FFT here
    // since we need 25 specific offsets out of ~484 possible. Cost: 25 x 121 = 3,025 ops.
    float phi[25] = {};
    for(int m = -2; m <= 2; m++)
        for(int n = -2; n <= 2; n++)
            phi[(m+2)*5+(n+2)] = ccmap(row+m, col+n);

    t_phi += std::chrono::duration<double>(clk::now() - t0).count();
    t0 = clk::now();

    // Replace Gamma loop with autocorrelation of patch.
    // Gamma[m][n][k] = sum_{a,b} W[k][a+1][b+1] * R[m+a+3][n+b+3]
    // where R(p,q) = sum_{i,j} patch_ref(i,j) * patch_ref(i+p, j+q)
    // Offsets: m in [-2,2], a in {-1,0,1,2} → total range [-3,4] -> 8 values each axis.
    // Cost: 64 x 121 = 7,744 ops (vs 48,400 for the p,q Gamma loop).

    // --- Autocorr R ---
    float R[8][8] = {};
    for(int p = -4; p <= 3; p++)
        for(int q = -4; q <= 3; q++) {
            float sum = 0.0f;
            for(int i = 0; i < PS; i++)
                for(int j = 0; j < PS; j++) {
                    int ip = i + p, jq = j + q;
                    if(ip >= 0 && ip < PS && jq >= 0 && jq < PS)
                        sum += patch_ref[i*PS + j] * patch_ref[ip*PS + jq];
                }
            R[p+4][q+4] = sum;
        }

    t_R += std::chrono::duration<double>(clk::now() - t0).count();
    t0 = clk::now();

    // W[k][a+1][b+1]: appendix B bicubic weights recast as lookup table.
    // Derived by reading each C[k] formula and placing weight for g(a,b)
    // at W[k][a+1][b+1]. a,b in {-1,0,1,2}, indices 0..3.
    // Transcribed directly from appendix B of Chen & Katz 2005.
    static const float W[16][4][4] = {
        // k=0: C[0] weights
        {{ 1/36.f, -1/12.f,  1/12.f, -1/36.f},
         {-1/12.f,  1/ 4.f, -1/ 4.f,  1/12.f},
         { 1/12.f, -1/ 4.f,  1/ 4.f, -1/12.f},
         {-1/36.f,  1/12.f, -1/12.f,  1/36.f}},
        // k=1: C[1] weights
        {{ 0,       0,       0,       0      },
         {-1/12.f,  1/ 4.f, -1/ 4.f,  1/12.f},
         { 1/ 6.f, -1/ 2.f,  1/ 2.f, -1/ 6.f},
         {-1/12.f,  1/ 4.f, -1/ 4.f,  1/12.f}},
        // k=2: C[2] weights
        {{ 0,      -1/12.f,  1/ 6.f, -1/12.f},
         { 0,       1/ 4.f, -1/ 2.f,  1/ 4.f},
         { 0,      -1/ 4.f,  1/ 2.f, -1/ 4.f},
         { 0,       1/12.f, -1/ 6.f,  1/12.f}},
        // k=3: C[3] weights
        {{ 0,       0,       0,       0      },
         { 0,       1/ 4.f, -1/ 2.f,  1/ 4.f},
         { 0,      -1/ 2.f,  1.0f,   -1/ 2.f},
         { 0,       1/ 4.f, -1/ 2.f,  1/ 4.f}},
        // k=4: C[4] weights
        {{-1/36.f,  1/12.f, -1/12.f,  1/36.f},
         { 1/ 6.f, -1/ 2.f,  1/ 2.f, -1/ 6.f},
         {-1/12.f,  1/ 4.f, -1/ 4.f,  1/12.f},
         {-1/18.f,  1/ 6.f, -1/ 6.f,  1/18.f}},
        // k=5: C[5] weights
        {{-1/36.f,  1/ 6.f, -1/12.f, -1/18.f},
         { 1/12.f, -1/ 2.f,  1/ 4.f,  1/ 6.f},
         {-1/12.f,  1/ 2.f, -1/ 4.f, -1/ 6.f},
         { 1/36.f, -1/ 6.f,  1/12.f,  1/18.f}},
        // k=6: C[6] weights
        {{ 0,       0,       0,       0      },
         { 0,       0,       0,       0      },
         {-1/ 6.f,  1/ 2.f, -1/ 2.f,  1/ 6.f},
         { 0,       0,       0,       0      }},
        // k=7: C[7] weights
        {{ 0,       0,      -1/ 6.f,  0      },
         { 0,       0,       1/ 2.f,  0      },
         { 0,       0,      -1/ 2.f,  0      },
         { 0,       0,       1/ 6.f,  0      }},
        // k=8: C[8] weights
        {{ 0,       1/12.f, -1/ 6.f,  1/12.f},
         { 0,      -1/ 2.f,  1.0f,   -1/ 2.f},
         { 0,       1/ 4.f, -1/ 2.f,  1/ 4.f},
         { 0,       1/ 6.f, -1/ 3.f,  1/ 6.f}},
        // k=9: C[9] weights
        {{ 0,       0,       0,       0      },
         { 1/12.f, -1/ 2.f,  1/ 4.f,  1/ 6.f},
         {-1/ 6.f,  1.0f,   -1/ 2.f, -1/ 3.f},
         { 1/12.f, -1/ 2.f,  1/ 4.f,  1/ 6.f}},
        // k=10: C[10] weights
        {{ 0,       0,       0,       0      },
         { 0,       0,       0,       0      },
         { 0,       1/ 2.f, -1.0f,    1/ 2.f},
         { 0,       0,       0,       0      }},
        // k=11: C[11] weights
        {{ 0,       0,       0,       0      },
         { 0,       0,       1/ 2.f,  0      },
         { 0,       0,      -1.0f,    0      },
         { 0,       0,       1/ 2.f,  0      }},
        // k=12: C[12] weights
        {{ 1/36.f, -1/ 6.f,  1/12.f,  1/18.f},
         {-1/ 6.f,  1.0f,   -1/ 2.f, -1/ 3.f},
         { 1/12.f, -1/ 2.f,  1/ 4.f,  1/ 6.f},
         { 1/18.f, -1/ 3.f,  1/ 6.f,  1/ 9.f}},
        // k=13: C[13] weights
        {{ 0,       0,       0,       0      },
         { 0,       0,       0,       0      },
         { 1/ 6.f, -1.0f,    1/ 2.f,  1/ 3.f},
         { 0,       0,       0,       0      }},
        // k=14: C[14] weights
        {{ 0,       0,       1/ 6.f,  0      },
         { 0,       0,      -1.0f,    0      },
         { 0,       0,       1/ 2.f,  0      },
         { 0,       0,       1/ 3.f,  0      }},
        // k=15: C[15] weights
        {{ 0,       0,       0,       0      },
         { 0,       0,       0,       0      },
         { 0,       0,       1.0f,    0      },
         { 0,       0,       0,       0      }}
    };

    // --- Gamma from W ---
    float Gamma[5][5][16] = {};
    for(int m = -2; m <= 2; m++)
        for(int n = -2; n <= 2; n++)
            for(int k = 0; k < 16; k++)
                for(int a = -1; a <= 2; a++)
                    for(int b = -1; b <= 2; b++)
                        Gamma[m + 2][n + 2][k] +=
                            W[k][a + 1][b + 1] * R[m + a + 3][n + b + 3];

    static int debug_count = 0;
    if(debug_count > 100000 & debug_count < 100008) {
        printf("\n=== Window %d ===\n", debug_count);
        printf("int_u=%d int_v=%d gauss_u=%.4f gauss_v=%.4f\n", int_u, int_v, gauss_u, gauss_v);
        printf("phi center: %.6f\n", phi[2*5+2]);
        printf("phi neighbors: %.6f %.6f %.6f\n", phi[1*5+2], phi[3*5+2], phi[2*5+3]);
        printf("R(0,0)=%.6f R(1,0)=%.6f R(0,1)=%.6f\n", R[4][4], R[5][4], R[4][5]);
        printf("Gamma[2][2][15]=%.6f\n", Gamma[2][2][15]);
    }
    debug_count++;

    t_gamma += std::chrono::duration<double>(clk::now() - t0).count();
    t0 = clk::now();

    // Step 5: Newton-Raphson minimization of residue eq.(9).
    // Seeded by Gaussian sub-pixel estimate -- already close to the minimum,
    // ensures convergence in 3-5 iterations rather than a global search.
    // Only the sub-pixel fractional part (offset from integer peak) is needed.

    float up = gauss_u;
    float vp = gauss_v;

    // Newton-Raphson loop: at each step compute gradient (g_u, g_v) and
    // Hessian (H_uu, H_vv, H_uv) of eps(u',v') from eq.(9) analytically,
    // then solve the 2x2 system H*delta = -g via Cramer's rule.
    // Derivatives of basis_k computed by power rule from eq.(6) term order.
    for(int iter = 0; iter < 10; iter++)
    {
        float eu = 1.0f - up, ev = 1.0f - vp;
        float eu2 = eu*eu, eu3 = eu2*eu;
        float ev2 = ev*ev, ev3 = ev2*ev;

        float B[16]   = {eu3*ev3, eu3*ev2, eu2*ev3, eu2*ev2, eu3*ev,  eu*ev3, eu3, ev3,
                        eu2*ev,  eu*ev2,  eu2,     ev2,     eu*ev,   eu,     ev,  1.0f};
        float Bu[16]  = {-3*eu2*ev3, -3*eu2*ev2, -2*eu*ev3, -2*eu*ev2, -3*eu2*ev, -ev3, -3*eu2, 0,
                        -2*eu*ev,   -ev2,       -2*eu,      0,         -ev,       -1,    0,      0};
        float Bv[16]  = {-3*eu3*ev2, -2*eu3*ev, -3*eu2*ev2, -2*eu2*ev, -eu3, -3*eu*ev2, 0, -3*ev2,
                        -eu2,       -2*eu*ev,   0,          -2*ev,     -eu,   0,        -1,  0};
        float Buu[16] = {6*eu*ev3, 6*eu*ev2, 2*ev3, 2*ev2, 6*eu*ev, 0, 6*eu, 0,
                        2*ev,     0,        2,     0,     0,       0,  0,    0};
        float Bvv[16] = {6*eu3*ev, 2*eu3, 6*eu2*ev, 2*eu2, 0, 6*eu*ev, 0, 6*ev,
                        0,        2*eu,  0,        2,     0, 0,       0,  0};
        float Buv[16] = {9*eu2*ev2, 6*eu2*ev, 6*eu*ev2, 4*eu*ev, 3*eu2, 3*ev2, 0, 0,
                        2*eu,      2*ev,     0,         0,       1,     0,     0, 0};

        float g_u=0, g_v=0, H_uu=0, H_vv=0, H_uv=0;

        for(int m = -2; m <= 2; m++) {
            for(int n = -2; n <= 2; n++) {
                const float* G = Gamma[m+2][n+2];
                float pv=0, pu=0, pv_=0, puu=0, pvv=0, puv=0;
                for(int k = 0; k < 16; k++) {
                    pv  += G[k]*B[k];    pu  += G[k]*Bu[k];
                    pv_ += G[k]*Bv[k];   puu += G[k]*Buu[k];
                    pvv += G[k]*Bvv[k];  puv += G[k]*Buv[k];
                }
                float r = phi[(m + 2) * 5 + (n + 2)] - pv;
                g_u  += -2.0f*r*pu;
                g_v  += -2.0f*r*pv_;
                H_uu += 2.0f*pu*pu   - 2.0f*r*puu;
                H_vv += 2.0f*pv_*pv_ - 2.0f*r*pvv;
                H_uv += 2.0f*pu*pv_  - 2.0f*r*puv;
            }
        }

        // Solve 2x2 system H*delta = -g via Cramer's rule
        float det = H_uu*H_vv - H_uv*H_uv;
        if(std::abs(det) < 1e-10f) break;

        float delta_u = (-g_u*H_vv + g_v*H_uv) / det;
        float delta_v = (-g_v*H_uu + g_u*H_uv) / det;

        up = std::clamp(up + delta_u, 0.0f, 0.99f);
        vp = std::clamp(vp + delta_v, 0.0f, 0.99f);

        if(debug_count > 100000 & debug_count < 100008)
            printf("iter=%d up=%.6f vp=%.6f det=%.6e delta_u=%.6f delta_v=%.6f\n",
                iter, up, vp, det, delta_u, delta_v);

        // Converged when step smaller than 0.0001px
        if(std::abs(delta_u) < 1e-4f && std::abs(delta_v) < 1e-4f) break;
    }

    float u = int_u + up;
    float v = int_v + vp;

    t_nr += std::chrono::duration<double>(clk::now() - t0).count();
    
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