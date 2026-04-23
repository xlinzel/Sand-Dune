#include <grains/correlator.h>
#include <algorithm>
#include <fftw3.h>
#include <cmath>
#include <cstring>
#include <limits>


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
    : window_size(parameters.window_size),
      overlap(parameters.overlap),
      enable_pid(parameters.enable_pid),
      pid_iterations(parameters.pid_iterations),
      pid_relaxation(parameters.pid_relaxation),
      pid_smoothing_passes(parameters.pid_smoothing_passes)
{
    AllocateFFTBuffers();
}

Correlator::~Correlator()
{
    FreeFFTBuffers();
}

/// @brief Allocate all FFTW buffers and plans for the current interrogation size.
///
/// The correlator reuses one set of FFT buffers across all windows. This keeps
/// the hot path allocation-free and lets the CMM peak solver reuse the saved
/// reference FFT when building the autocorrelation patch.
void Correlator::AllocateFFTBuffers()
{
    /// @note Rows and columns are currently equal because windows are square,
    /// but they are stored separately so the FFT path can be generalized later.
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

/// @brief Release the FFTW resources owned by this correlator.
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

/// @brief Run the full correlator on one image pair.
///
/// This is the top-level dispatcher for the class. It validates the image and
/// window geometry, then either executes a single rigid-window correlation pass
/// or the PID outer loop when deformation correction is enabled.
VectorField Correlator::Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                                std::function<void(float)> on_progress)
{
    Eigen::Vector2i ref_size(reference.rows(), reference.cols());
    Eigen::Vector2i flow_size(flow.rows(), flow.cols());

    if(ref_size(0) != flow_size(0) || ref_size(1) != flow_size(1))
    {
        return VectorField();
    }

    if(window_size <= 1 || overlap < 0 || overlap >= window_size)
        return VectorField();

    if(enable_pid && pid_iterations > 0)
    {
        VectorField result = ComputeWithPid(reference, flow, on_progress);
        result.CalcMag();

        return result;
    }

    //Calculate magnitude map
    VectorField result = ComputeSinglePass(reference, flow, nullptr, nullptr, on_progress);
    result.CalcMag();

    return result;
}

/// @brief Execute one rigid or PID-warped correlation pass over the image grid.
///
/// Each window is processed independently:
/// 1. extract the reference and flow window
/// 2. optionally warp the flow window using the PID predictor
/// 3. compute the FFT correlation map
/// 4. run the CMM peak solver
/// 5. write `(u, v, s2n)` and optionally the local CMM residual `eps`
///
/// @param reference Undisturbed image.
/// @param flow Disturbed image.
/// @param predictor Optional deformation model for PID-warped windows.
/// @param fit_eps Optional output map of per-window CMM residuals.
/// @param on_progress Optional callback updated after each processed window.
/// @return Displacement field from this pass.
VectorField Correlator::ComputeSinglePass(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                                          const DeformationField* predictor,
                                          Eigen::MatrixXf* fit_eps,
                                          std::function<void(float)> on_progress)
{
    int movement = window_size - overlap;
    int num_rows = floor(reference.rows() / movement);
    int num_cols = floor(reference.cols() / movement);

    int total_windows = num_rows * num_cols;
    int completed = 0;

    VectorField vectorfield(num_rows, num_cols);
    if(fit_eps != nullptr)
        *fit_eps = Eigen::MatrixXf::Constant(num_rows, num_cols, std::numeric_limits<float>::infinity());
    Eigen::MatrixXf padded_reference(window_size, window_size);
    Eigen::MatrixXf padded_flow(window_size, window_size);

    for(int win_row = 0; win_row < num_rows; win_row++)
    {
        for(int win_col = 0; win_col < num_cols; win_col++)
        {
            int start_row = win_row * movement;
            int start_col = win_col * movement;
            int block_rows = std::min(window_size, static_cast<int>(reference.rows()) - start_row);
            int block_cols = std::min(window_size, static_cast<int>(reference.cols()) - start_col);
            bool full_window = (block_rows == window_size && block_cols == window_size);

            if(full_window && predictor == nullptr)
            {
                auto ref_window = reference.block(start_row, start_col, window_size, window_size);
                auto flow_window = flow.block(start_row, start_col, window_size, window_size);

                /// @note Flat windows are skipped because the correlation peak is
                /// not meaningful when the background pattern has almost no local contrast.
                float mean = ref_window.mean();
                float variance = (ref_window.array() - mean).square().sum() / (window_size * window_size);
                if(variance < 0.001f)
                {
                    vectorfield.u(win_row, win_col) = 0.0f;
                    vectorfield.v(win_row, win_col) = 0.0f;
                    vectorfield.s2n(win_row, win_col) = 0.0f;
                    if(fit_eps != nullptr)
                        (*fit_eps)(win_row, win_col) = std::numeric_limits<float>::infinity();
                    if(on_progress) on_progress(++completed / static_cast<float>(total_windows));
                    continue;
                }

                Eigen::MatrixXf ccmap = CrossCorrelationFFT(ref_window, flow_window);
                PeakResult peak = FindPeak(ccmap);
                vectorfield.u(win_row, win_col) = peak.u;
                vectorfield.v(win_row, win_col) = peak.v;
                vectorfield.s2n(win_row, win_col) = peak.s2n;
                if(fit_eps != nullptr)
                    (*fit_eps)(win_row, win_col) = peak.eps;
            }
            else if(full_window)
            {
                auto ref_window = reference.block(start_row, start_col, window_size, window_size);
                ExtractWarpedFlowWindow(flow, start_row, start_col, win_row, win_col, *predictor, padded_flow);

                float mean = ref_window.mean();
                float variance = (ref_window.array() - mean).square().sum() / (window_size * window_size);
                if(variance < 0.001f)
                {
                    vectorfield.u(win_row, win_col) = 0.0f;
                    vectorfield.v(win_row, win_col) = 0.0f;
                    vectorfield.s2n(win_row, win_col) = 0.0f;
                    if(fit_eps != nullptr)
                        (*fit_eps)(win_row, win_col) = std::numeric_limits<float>::infinity();
                    if(on_progress) on_progress(++completed / static_cast<float>(total_windows));
                    continue;
                }

                Eigen::MatrixXf ccmap = CrossCorrelationFFT(ref_window, padded_flow);
                PeakResult peak = FindPeak(ccmap);
                vectorfield.u(win_row, win_col) = peak.u;
                vectorfield.v(win_row, win_col) = peak.v;
                vectorfield.s2n(win_row, win_col) = peak.s2n;
                if(fit_eps != nullptr)
                    (*fit_eps)(win_row, win_col) = peak.eps;
            }
            else
            {
                ExtractPaddedWindow(reference, start_row, start_col, padded_reference);
                if(predictor != nullptr)
                    ExtractWarpedFlowWindow(flow, start_row, start_col, win_row, win_col, *predictor, padded_flow);
                else
                    ExtractPaddedWindow(flow, start_row, start_col, padded_flow);

                float mean = padded_reference.mean();
                float variance = (padded_reference.array() - mean).square().sum() / (window_size * window_size);
                if(variance < 0.001f)
                {
                    vectorfield.u(win_row, win_col) = 0.0f;
                    vectorfield.v(win_row, win_col) = 0.0f;
                    vectorfield.s2n(win_row, win_col) = 0.0f;
                    if(fit_eps != nullptr)
                        (*fit_eps)(win_row, win_col) = std::numeric_limits<float>::infinity();
                    if(on_progress) on_progress(++completed / static_cast<float>(total_windows));
                    continue;
                }

                Eigen::MatrixXf ccmap = CrossCorrelationFFT(padded_reference, padded_flow);
                PeakResult peak = FindPeak(ccmap);
                vectorfield.u(win_row, win_col) = peak.u;
                vectorfield.v(win_row, win_col) = peak.v;
                vectorfield.s2n(win_row, win_col) = peak.s2n;
                if(fit_eps != nullptr)
                    (*fit_eps)(win_row, win_col) = peak.eps;
            }

            if(on_progress) on_progress(++completed / static_cast<float>(total_windows));
        }
    }

    return vectorfield;
}

/// @brief Execute the PID deformation-correction loop around the base correlator.
///
/// The PID loop follows the same high-level structure as the paper:
/// 1. solve the displacement field once with rigid windows
/// 2. estimate a first-order deformation model from that field
/// 3. warp each flow window using the local affine model
/// 4. solve the residual displacement on the warped windows
/// 5. accept only per-window updates whose CMM residual does not get worse
///
/// This keeps the final field image-driven while preventing a weaker PID fit
/// from overwriting a better result from an earlier pass.
VectorField Correlator::ComputeWithPid(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                                       std::function<void(float)> on_progress)
{
    auto make_progress = [&](int pass_index)
    {
        if(!on_progress)
        return std::function<void(float)>{};

        return std::function<void(float)>([&, pass_index](float p)
        {
            p = std::clamp(p, 0.0f, 1.0f);

            float rigid_w = 1.0f;
            float pid_w   = 1.0f;
            float total_w = rigid_w + pid_iterations * pid_w;

            float done_before = 0.0f;
            float current_w   = rigid_w;

            if(pass_index == 0)
            {
                done_before = 0.0f;
                current_w   = rigid_w;
            }
            else
            {
                done_before = rigid_w + (pass_index - 1) * pid_w;
                current_w   = pid_w;
            }

            on_progress((done_before + current_w * p) / total_w);
        });
    };

    Eigen::MatrixXf current_fit_eps;
    VectorField current = ComputeSinglePass(reference, flow, nullptr, &current_fit_eps, make_progress(0));
    float relaxation = std::clamp(pid_relaxation, 0.05f, 1.0f);
    constexpr float kPidS2nTolerance = 1e-4f;
    constexpr float kPidEpsTolerance = 1e-5f;

    for(int iter = 0; iter < pid_iterations; iter++)
    {
        DeformationField predictor = EstimateDeformationField(current);
        Eigen::MatrixXf residual_fit_eps;
        VectorField residual = ComputeSinglePass(reference, flow, &predictor, &residual_fit_eps, make_progress(iter + 1));

        /// @note The warped pass measures the residual shift after the affine
        /// predictor has already been applied to the window. A window update is
        /// accepted only when the new CMM residual is not worse than the old one.
        Eigen::MatrixXf updated_u = predictor.u.array() + relaxation * residual.u.array();
        Eigen::MatrixXf updated_v = predictor.v.array() + relaxation * residual.v.array();
        double accepted_correction_sum = 0.0;
        int accepted_windows = 0;

        for(int row = 0; row < current.height; row++)
        {
            for(int col = 0; col < current.width; col++)
            {
                float previous_eps = current_fit_eps(row, col);
                float candidate_eps = residual_fit_eps(row, col);
                float previous_s2n = current.s2n(row, col);
                float candidate_s2n = residual.s2n(row, col);
                bool candidate_eps_valid = std::isfinite(candidate_eps);
                bool previous_eps_valid = std::isfinite(previous_eps);
                bool accept_candidate = false;
                if(candidate_eps_valid && previous_eps_valid)
                {
                    accept_candidate = candidate_eps <= previous_eps + kPidEpsTolerance;
                }
                else if(candidate_eps_valid && !previous_eps_valid)
                {
                    accept_candidate = true;
                }
                else if(!candidate_eps_valid && !previous_eps_valid)
                {
                    accept_candidate =
                        std::isfinite(candidate_s2n) &&
                        (!std::isfinite(previous_s2n) || candidate_s2n + kPidS2nTolerance >= previous_s2n);
                }

                if(!accept_candidate)
                    continue;

                current.u(row, col) = updated_u(row, col);
                current.v(row, col) = updated_v(row, col);
                current.s2n(row, col) = candidate_s2n;
                current_fit_eps(row, col) = candidate_eps;

                float du = residual.u(row, col);
                float dv = residual.v(row, col);
                accepted_correction_sum += std::sqrt(static_cast<double>(du * du + dv * dv));
                accepted_windows++;
            }
        }

        if(accepted_windows == 0)
            break;

        double mean_correction = accepted_correction_sum / static_cast<double>(accepted_windows);
        if(mean_correction < 1e-3)
            break;
    }

    if(on_progress)
        on_progress(1.0f);

    return current;
}

/// @brief Extract one interrogation window with zero padding outside the image bounds.
///
/// This helper keeps the correlation path uniform near the image border. Interior
/// windows take the faster direct block path; only partial edge windows come here.
void Correlator::ExtractPaddedWindow(const Eigen::MatrixXf& image, int start_row, int start_col,
                                     Eigen::MatrixXf& window) const
{
    window.setZero(window_size, window_size);

    if(start_row >= image.rows() || start_col >= image.cols())
        return;

    int block_rows = std::min(window_size, static_cast<int>(image.rows()) - start_row);
    int block_cols = std::min(window_size, static_cast<int>(image.cols()) - start_col);

    if(block_rows <= 0 || block_cols <= 0)
        return;

    window.block(0, 0, block_rows, block_cols) = image.block(start_row, start_col, block_rows, block_cols);
}

//////////////////////////////////////////////////////
// Correlation Kernels
//////////////////////////////////////////////////////

/// @brief Compute the normalized correlation map for one window pair using FFTs.
///
/// The reference FFT is also cached because the later CMM solve needs it to
/// build the local autocorrelation patch `R`.
Eigen::MatrixXf Correlator::CrossCorrelationFFT(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                                const Eigen::Ref<const Eigen::MatrixXf>& w_flow)
{
    Eigen::Vector2i size(w_reference.rows(), w_reference.cols());

    /// @note FFTW expects row-major contiguous input. Converting once here is
    /// cheaper than scattered element-wise filling of the FFT buffers.
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ref_rm = w_reference;
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> flow_rm = w_flow;

    ref_rm.array() -= ref_rm.mean();
    flow_rm.array() -= flow_rm.mean();

    memcpy(ref_in, ref_rm.data(), sizeof(float) * size(0) * size(1));
    memcpy(flow_in, flow_rm.data(), sizeof(float) * size(0) * size(1));

    /// @brief Execute forward FFTs for the current reference and flow windows.
    fftwf_execute(ref_plan);
    fftwf_execute(flow_plan);

    /// @brief Save the reference FFT for the later CMM autocorrelation build.
    memcpy(ref_out_saved, ref_out, sizeof(fftwf_complex) * rows * freq_cols);

    /// @brief Form the cross-power product `conj(ref) * flow` in frequency space.
    for(int i = 0; i < rows * freq_cols; i++)
    {
        product[i][0] = ref_out[i][0] * flow_out[i][0] + ref_out[i][1] * flow_out[i][1];
        product[i][1] = ref_out[i][0] * flow_out[i][1] - ref_out[i][1] * flow_out[i][0];
    }

    fftwf_execute(inv_plan);

    /// @brief Convert the inverse FFT output into an Eigen matrix for peak search.
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ccmap_rm(rows, cols);
    memcpy(ccmap_rm.data(), ccmap_raw,sizeof(float) * rows * cols);

    ccmap_rm *= (1.0f / float(rows * cols));

    Eigen::MatrixXf ccmap = ccmap_rm; // Cross-correlation map.

    /// @brief Shift the inverse FFT output so zero displacement is centered.
    Eigen::MatrixXf ccmap_shifted(rows, cols);
    int shift_r = rows / 2;
    int shift_c = cols / 2;

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            int rr = (r + shift_r) % rows;
            int cc = (c + shift_c) % cols;
            ccmap_shifted(rr, cc) = ccmap(r, c);
        }
    }

    return ccmap_shifted;
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

void Correlator::SetOverlap(const int overlap)
{
    this->overlap = overlap;
}
