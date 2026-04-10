#include <grains/piv.h>
#include <algorithm>
#include <fftw3.h>
#include <cmath>
#include <chrono>
#include <numeric>

PIV::PIV()
{
    AllocateFFTBuffers();
}

PIV::PIV(const int window_size, const int overlap)
    : window_size(window_size), overlap(overlap)
{
    AllocateFFTBuffers();
}

PIV::PIV(const PIVParameters parameters)
    : window_size(parameters.window_size), overlap(parameters.overlap)
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

void PIV::FreeFFTBuffers()
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

VectorField PIV::Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
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

                PeakResult peak = FindPeak(ccmap,
                                           t_patch, t_phi, t_R, t_gamma, t_nr);
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

                PeakResult peak = FindPeak(ccmap,
                                           t_patch, t_phi, t_R, t_gamma, t_nr);
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

Eigen::MatrixXf PIV::CrossCorrelationFFT(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
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

Eigen::MatrixXf PIV::CrossCorrelationSpatial(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
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

PIV::PeakResult PIV::FindPeak(const Eigen::MatrixXf& ccmap,
                              double& t_patch, double& t_phi, double& t_R, double& t_gamma, double& t_nr)
{
    int row, col;
    float peak = ccmap.maxCoeff(&row, &col);

    if ((row == 0 || row == ccmap.rows() - 1 || col == 0 || col == ccmap.cols() - 1)
        || row == 1 || row == ccmap.rows() - 2 || col == 1 || col == ccmap.cols() - 2)
    {
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

    using clk = std::chrono::high_resolution_clock;

    auto t0 = clk::now();

    for(int i = 0; i < rows * freq_cols; i++) {
        product[i][0] = ref_out_saved[i][0]*ref_out_saved[i][0] 
                    + ref_out_saved[i][1]*ref_out_saved[i][1];
        product[i][1] = 0.0f;
    }
    fftwf_execute(inv_plan);

    float R[9][9] = {};
    for(int p = -4; p <= 4; p++)
        for(int q = -4; q <= 4; q++) 
        {
            int ri = (p + rows) % rows;
            int ci = (q + cols) % cols;
            R[p + 4][q + 4] = ccmap_raw[ri * cols + ci] / float(rows * cols);
        }

    t_R += std::chrono::duration<double>(clk::now() - t0).count();
    t0 = clk::now();

    auto cubic_weight = [](float x) -> float
    {
        x = std::abs(x);
        if(x <= 1.0f)
            return ((1.5f * x - 2.5f) * x * x) + 1.0f;
        if(x < 2.0f)
            return (((-0.5f * x + 2.5f) * x - 4.0f) * x) + 2.0f;
        return 0.0f;
    };
    auto cubic_derivative = [](float x) -> float
    {
        float sign = (x < 0.0f) ? -1.0f : 1.0f;
        x = std::abs(x);

        if(x <= 1.0f)
            return sign * (4.5f * x * x - 5.0f * x);
        if(x < 2.0f)
            return sign * (-1.5f * x * x + 5.0f * x - 4.0f);
        return 0.0f;
    };

    t_gamma += std::chrono::duration<double>(clk::now() - t0).count();

    struct EvalResult
    {
        float eps = 1e30f;
        float g_u = 0.0f;
        float g_v = 0.0f;
        float H_uu = 0.0f;
        float H_uv = 0.0f;
        float H_vv = 0.0f;
    };

    struct CellSolution
    {
        float du = 0.0f;
        float dv = 0.0f;
        float eps = 1e30f;
        int int_u = 0;
        int int_v = 0;
        int row = 0;
        int col = 0;
        bool valid = false;
    };

    constexpr float lock_threshold = 0.47f;

    auto offset_extent = [](float du, float dv)
    {
        return std::max(std::abs(du), std::abs(dv));
    };

    auto better_cell = [&](const CellSolution& candidate, const CellSolution& current)
    {
        constexpr float eps_tol = 5e-4f;

        if(!candidate.valid)
            return false;
        if(!current.valid)
            return true;
        if(candidate.eps + eps_tol < current.eps)
            return true;
        if(std::abs(candidate.eps - current.eps) > eps_tol)
            return false;

        float cand_extent = offset_extent(candidate.du, candidate.dv);
        float current_extent = offset_extent(current.du, current.dv);
        bool cand_locked = cand_extent > lock_threshold;
        bool current_locked = current_extent > lock_threshold;

        if(current_locked && !cand_locked)
            return true;

        return cand_extent + 1e-4f < current_extent;
    };

    auto solve_cell = [&](int cell_row, int cell_col, bool use_hint, float hint_u, float hint_v) -> CellSolution
    {
        CellSolution solution;
        if(cell_row <= 1 || cell_row >= ccmap.rows() - 2 || cell_col <= 1 || cell_col >= ccmap.cols() - 2)
            return solution;

        solution.row = cell_row;
        solution.col = cell_col;
        solution.int_u = cell_col - ccmap.cols() / 2;
        solution.int_v = cell_row - ccmap.rows() / 2;

        float gauss_u = 0.0f;
        float gauss_v = 0.0f;

        if(ccmap(cell_row, cell_col - 1) > 0 && ccmap(cell_row, cell_col) > 0 && ccmap(cell_row, cell_col + 1) > 0)
            gauss_u = std::clamp(
                cell_col + (std::log(ccmap(cell_row, cell_col - 1)) - std::log(ccmap(cell_row, cell_col + 1)))
                    / (2 * std::log(ccmap(cell_row, cell_col - 1)) - 4 * std::log(ccmap(cell_row, cell_col))
                    + 2 * std::log(ccmap(cell_row, cell_col + 1)))
                - static_cast<float>(cell_col), -0.49f, 0.49f);

        if(ccmap(cell_row - 1, cell_col) > 0 && ccmap(cell_row, cell_col) > 0 && ccmap(cell_row + 1, cell_col) > 0)
            gauss_v = std::clamp(
                cell_row + (std::log(ccmap(cell_row - 1, cell_col)) - std::log(ccmap(cell_row + 1, cell_col)))
                    / (2 * std::log(ccmap(cell_row - 1, cell_col)) - 4 * std::log(ccmap(cell_row, cell_col))
                    + 2 * std::log(ccmap(cell_row + 1, cell_col)))
                - static_cast<float>(cell_row), -0.49f, 0.49f);

        if(!std::isfinite(gauss_u)) gauss_u = 0.0f;
        if(!std::isfinite(gauss_v)) gauss_v = 0.0f;

        auto phi_t0 = clk::now();
        float phi[25] = {};
        for(int m = -2; m <= 2; m++)
            for(int n = -2; n <= 2; n++)
                phi[(m + 2) * 5 + (n + 2)] = ccmap(cell_row + m, cell_col + n);
        t_phi += std::chrono::duration<double>(clk::now() - phi_t0).count();

        const float phi_mean = std::accumulate(std::begin(phi), std::end(phi), 0.0f) / 25.0f;
        if(std::abs(phi_mean) < 1e-8f)
            return solution;

        float phi_norm[25] = {};
        for(int idx = 0; idx < 25; idx++)
            phi_norm[idx] = phi[idx] / phi_mean;

        auto evaluate = [&](float du, float dv) -> EvalResult
        {
            struct Basis1D
            {
                int base = 0;
                float w[4] = {};
                float dw[4] = {};
            };

            auto build_basis = [&](Basis1D (&basis)[5], float shift)
            {
                for(int i = 0; i < 5; i++)
                {
                    float pos = static_cast<float>(i - 2) - shift;
                    int base = static_cast<int>(std::floor(pos));
                    basis[i].base = base;

                    for(int k = 0; k < 4; k++)
                    {
                        float sample = static_cast<float>(base - 1 + k);
                        float delta = pos - sample;
                        basis[i].w[k] = cubic_weight(delta);
                        basis[i].dw[k] = cubic_derivative(delta);
                    }
                }
            };

            Basis1D x_basis[5];
            Basis1D y_basis[5];
            build_basis(x_basis, du);
            build_basis(y_basis, dv);

            float pred[25] = {};
            float pred_du[25] = {};
            float pred_dv[25] = {};
            float pred_mean = 0.0f;
            float pred_du_mean = 0.0f;
            float pred_dv_mean = 0.0f;

            int idx = 0;
            for(int m = 0; m < 5; m++)
            {
                const Basis1D& yb = y_basis[m];
                for(int n = 0; n < 5; n++)
                {
                    const Basis1D& xb = x_basis[n];
                    float value = 0.0f;
                    float dx = 0.0f;
                    float dy = 0.0f;

                    for(int ky = 0; ky < 4; ky++)
                    {
                        int ry = yb.base - 1 + ky + 4;
                        float wy = yb.w[ky];
                        float dwy = yb.dw[ky];

                        for(int kx = 0; kx < 4; kx++)
                        {
                            int rx = xb.base - 1 + kx + 4;
                            float rv = R[ry][rx];
                            float wx = xb.w[kx];
                            float dwx = xb.dw[kx];

                            value += rv * wy * wx;
                            dx += rv * wy * dwx;
                            dy += rv * dwy * wx;
                        }
                    }

                    pred[idx] = value;
                    pred_du[idx] = -dx;
                    pred_dv[idx] = -dy;
                    pred_mean += pred[idx];
                    pred_du_mean += pred_du[idx];
                    pred_dv_mean += pred_dv[idx];
                    idx++;
                }
            }

            pred_mean /= 25.0f;
            pred_du_mean /= 25.0f;
            pred_dv_mean /= 25.0f;

            EvalResult out;
            if(std::abs(pred_mean) < 1e-8f)
                return out;
            out.eps = 0.0f;

            for(int idx = 0; idx < 25; idx++)
            {
                float pred_norm = pred[idx] / pred_mean;
                float pred_norm_du = (pred_du[idx] * pred_mean - pred[idx] * pred_du_mean)
                                   / (pred_mean * pred_mean);
                float pred_norm_dv = (pred_dv[idx] * pred_mean - pred[idx] * pred_dv_mean)
                                   / (pred_mean * pred_mean);

                float r = phi_norm[idx] - pred_norm;

                out.eps += r * r;
                out.g_u += -2.0f * r * pred_norm_du;
                out.g_v += -2.0f * r * pred_norm_dv;
                out.H_uu += 2.0f * pred_norm_du * pred_norm_du;
                out.H_uv += 2.0f * pred_norm_du * pred_norm_dv;
                out.H_vv += 2.0f * pred_norm_dv * pred_norm_dv;
            }
            return out;
        };

        auto solve_t0 = clk::now();

        float best_u = std::clamp(gauss_u, -0.49f, 0.49f);
        float best_v = std::clamp(gauss_v, -0.49f, 0.49f);
        EvalResult best = evaluate(best_u, best_v);

        if(use_hint)
        {
            float hinted_u = std::clamp(hint_u, -0.49f, 0.49f);
            float hinted_v = std::clamp(hint_v, -0.49f, 0.49f);
            EvalResult hinted = evaluate(hinted_u, hinted_v);
            if(hinted.eps < best.eps)
            {
                best_u = hinted_u;
                best_v = hinted_v;
                best = hinted;
            }
        }

        EvalResult centered = evaluate(0.0f, 0.0f);
        if(centered.eps < best.eps)
        {
            best_u = 0.0f;
            best_v = 0.0f;
            best = centered;
        }

        auto search_grid = [&](float u_min, float u_max, float v_min, float v_max, float step)
        {
            u_min = std::clamp(u_min, -0.49f, 0.49f);
            u_max = std::clamp(u_max, -0.49f, 0.49f);
            v_min = std::clamp(v_min, -0.49f, 0.49f);
            v_max = std::clamp(v_max, -0.49f, 0.49f);

            if(u_min > u_max || v_min > v_max)
                return;

            int u_steps = static_cast<int>(std::floor((u_max - u_min) / step + 0.5f));
            int v_steps = static_cast<int>(std::floor((v_max - v_min) / step + 0.5f));

            for(int ui = 0; ui <= u_steps; ui++)
            {
                float du = std::min(0.49f, u_min + ui * step);
                for(int vi = 0; vi <= v_steps; vi++)
                {
                    float dv = std::min(0.49f, v_min + vi * step);
                    EvalResult candidate = evaluate(du, dv);
                    if(candidate.eps < best.eps)
                    {
                        best_u = du;
                        best_v = dv;
                        best = candidate;
                    }
                }
            }
        };

        bool improved = false;
        for(int iter = 0; iter < 8; iter++)
        {
            float det = best.H_uu * best.H_vv - best.H_uv * best.H_uv;
            if(std::abs(det) < 1e-10f)
                break;

            float delta_u = (-best.g_u * best.H_vv + best.g_v * best.H_uv) / det;
            float delta_v = (-best.g_v * best.H_uu + best.g_u * best.H_uv) / det;

            delta_u = std::clamp(delta_u, -0.25f, 0.25f);
            delta_v = std::clamp(delta_v, -0.25f, 0.25f);

            bool accepted = false;
            for(float alpha : {1.0f, 0.5f, 0.25f, 0.1f, 0.05f})
            {
                float cand_u = std::clamp(best_u + alpha * delta_u, -0.49f, 0.49f);
                float cand_v = std::clamp(best_v + alpha * delta_v, -0.49f, 0.49f);
                EvalResult candidate = evaluate(cand_u, cand_v);
                if(candidate.eps < best.eps)
                {
                    best_u = cand_u;
                    best_v = cand_v;
                    best = candidate;
                    improved = true;
                    accepted = true;
                    break;
                }
            }

            if(!accepted || (std::abs(delta_u) < 1e-4f && std::abs(delta_v) < 1e-4f))
                break;
        }

        search_grid(best_u - 0.01f, best_u + 0.01f, best_v - 0.01f, best_v + 0.01f, 0.005f);

        bool invalid_solution = !std::isfinite(best.eps) || best.eps >= 1e29f;
        bool near_boundary = offset_extent(best_u, best_v) > 0.45f;
        if(!improved && (invalid_solution || near_boundary))
        {
            search_grid(-0.49f, 0.49f, -0.49f, 0.49f, 0.05f);
            search_grid(best_u - 0.05f, best_u + 0.05f, best_v - 0.05f, best_v + 0.05f, 0.01f);
        }

        t_nr += std::chrono::duration<double>(clk::now() - solve_t0).count();

        solution.du = best_u;
        solution.dv = best_v;
        solution.eps = best.eps;
        solution.valid = std::isfinite(best.eps) && best.eps < 1e29f;
        return solution;
    };

    CellSolution best_cell = solve_cell(row, col, false, 0.0f, 0.0f);
    if(!best_cell.valid)
    {
        best_cell.valid = true;
        best_cell.row = row;
        best_cell.col = col;
        best_cell.int_u = col - ccmap.cols() / 2;
        best_cell.int_v = row - ccmap.rows() / 2;
    }

    for(int handoff_iter = 0; handoff_iter < 2 && best_cell.valid; handoff_iter++)
    {
        int u_offsets[2] = {0, 0};
        int v_offsets[2] = {0, 0};
        int u_count = 1;
        int v_count = 1;

        if(best_cell.du > lock_threshold) u_offsets[u_count++] = 1;
        else if(best_cell.du < -lock_threshold) u_offsets[u_count++] = -1;

        if(best_cell.dv > lock_threshold) v_offsets[v_count++] = 1;
        else if(best_cell.dv < -lock_threshold) v_offsets[v_count++] = -1;

        bool moved = false;
        CellSolution handoff_best = best_cell;

        for(int vi = 0; vi < v_count; vi++)
        {
            for(int ui = 0; ui < u_count; ui++)
            {
                int du_offset = u_offsets[ui];
                int dv_offset = v_offsets[vi];
                if(du_offset == 0 && dv_offset == 0)
                    continue;

                int candidate_row = best_cell.row + dv_offset;
                int candidate_col = best_cell.col + du_offset;

                CellSolution candidate = solve_cell(candidate_row, candidate_col, true,
                                                    best_cell.du - static_cast<float>(du_offset),
                                                    best_cell.dv - static_cast<float>(dv_offset));
                if(better_cell(candidate, handoff_best))
                {
                    handoff_best = candidate;
                    moved = true;
                }
            }
        }

        if(!moved)
            break;

        best_cell = handoff_best;
    }

    row = best_cell.row;
    col = best_cell.col;
    float u = best_cell.int_u + best_cell.du;
    float v = best_cell.int_v + best_cell.dv;

    Eigen::MatrixXf ccmap_flattened = ccmap.array() - ccmap.minCoeff();
    peak = ccmap_flattened(row, col);

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

void PIV::SetWindowSize(const int size)
{
    window_size = size;
}

void PIV::SetOverlap(const int overlap)
{
    this->overlap = overlap;
}
