#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>
#include <iostream>
#include <functional>
#include <fftw3.h>
#include <wind/correlatorparameters.h>

/// @brief Windowed image correlator for displacement-field estimation.
///
/// Divides the reference and flow images into overlapping interrogation windows
/// and locates the displacement peak in the normalised cross-correlation map via FFT.
/// FFTW buffers are pre-allocated in the constructor and reused across calls for efficiency.
class Correlator
{
public:
    //////////////////////////////////////////////////////
    // Public Result Types
    //////////////////////////////////////////////////////

    /// @brief Result of a single-window cross-correlation peak search.
    struct PeakResult
    {
        float u;   ///< Sub-pixel horizontal displacement (pixels).
        float v;   ///< Sub-pixel vertical displacement (pixels).
        float s2n; ///< Signal-to-noise ratio (primary peak / secondary peak).
        float eps; ///< Final local CMM residual for the chosen peak representation.
    };

public:
    //////////////////////////////////////////////////////
    // Construction And Main API
    //////////////////////////////////////////////////////

    Correlator();

    /// @brief Construct with explicit window parameters.
    Correlator(const int window_size, const int overlap);

    /// @brief Construct from a CorrelatorParameters struct.
    Correlator(const CorrelatorParameters parameters);

    ~Correlator();

    /// @brief Compute the displacement field between @p reference and @p flow.
    /// @param reference  Normalised float image of the undisturbed background pattern.
    /// @param flow       Normalised float image of the disturbed background pattern.
    /// @param on_progress Optional callback invoked with progress in [0, 1] after each window.
    /// @return VectorField containing per-window (u, v, s2n) values.
    VectorField Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                        std::function<void(float)> on_progress = nullptr);

    int GetWindowSize() const; ///< Interrogation window side length (pixels).
    int GetOverlap()    const; ///< Window overlap (pixels).
    void SetOverlap(const int overlap);

private:
    //////////////////////////////////////////////////////
    // Correlator State
    //////////////////////////////////////////////////////

    int window_size = 64;      ///< Interrogation window side length in pixels.
    int overlap     = 50;      ///< Overlap between neighboring windows in pixels.
    bool enable_pid = false;   ///< Enables iterative PID-style window deformation correction.
    int pid_iterations = 2;    ///< Maximum number of PID correction passes after the initial solve.
    float pid_relaxation = 1.0f; ///< Relaxation applied to the residual correction each PID pass.
    int pid_smoothing_passes = 1; ///< Number of 3x3 smoothing passes applied before PID differentiation.

    /// @brief First-order deformation model used to warp one PID interrogation window.
    ///
    /// The displacement field is stored on the correlator grid. For each window,
    /// the center displacement `(u, v)` and its first spatial derivatives define
    /// the local affine warp used to resample the flow image before correlation.
    struct DeformationField
    {
        Eigen::MatrixXf u;      ///< Window-center horizontal displacement.
        Eigen::MatrixXf v;      ///< Window-center vertical displacement.
        Eigen::MatrixXf du_dx;  ///< Horizontal displacement gradient along x.
        Eigen::MatrixXf du_dy;  ///< Horizontal displacement gradient along y.
        Eigen::MatrixXf dv_dx;  ///< Vertical displacement gradient along x.
        Eigen::MatrixXf dv_dy;  ///< Vertical displacement gradient along y.
    };

    /// @brief FFTW real-valued input buffer for the reference window.
    float*         ref_in    = nullptr;
    /// @brief FFTW real-valued input buffer for the flow or warped-flow window.
    float*         flow_in   = nullptr;
    /// @brief Frequency-domain FFT output for the reference window.
    fftwf_complex* ref_out   = nullptr;
    /// @brief Frequency-domain FFT output for the flow or warped-flow window.
    fftwf_complex* flow_out  = nullptr;
    /// @brief Frequency-domain product buffer reused for correlation and autocorrelation.
    fftwf_complex* product   = nullptr;
    /// @brief Spatial-domain inverse FFT buffer used to read the correlation map.
    float*         ccmap_raw = nullptr;

    /// @brief Saved reference FFT used by CMM to build the local autocorrelation patch.
    fftwf_complex* ref_out_saved = nullptr;

    int rows = 0;      ///< Current FFT row count.
    int cols = 0;      ///< Current FFT column count.
    int freq_cols = 0; ///< Packed complex-column count for the FFT output.

    /// @brief Forward FFT plan for the reference window.
    fftwf_plan ref_plan  = nullptr;
    /// @brief Forward FFT plan for the flow or warped-flow window.
    fftwf_plan flow_plan = nullptr;
    /// @brief Inverse FFT plan for the correlation/autocorrelation product.
    fftwf_plan inv_plan  = nullptr;

    //////////////////////////////////////////////////////
    // Internal Helpers
    //////////////////////////////////////////////////////

    /// @brief Allocate FFTW buffers and plans for the current window size.
    void AllocateFFTBuffers();
    /// @brief Release all FFTW buffers and plans owned by this correlator.
    void FreeFFTBuffers();

    /// @brief Run one full correlation pass over the image pair.
    /// @param reference Undisturbed image.
    /// @param flow Disturbed image.
    /// @param predictor Optional PID deformation model used to warp the flow window.
    /// @param fit_eps Optional per-window storage for the final CMM residual.
    /// @param on_progress Optional progress callback in [0, 1].
    /// @return Displacement field from this pass only.
    VectorField ComputeSinglePass(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                                  const DeformationField* predictor,
                                  Eigen::MatrixXf* fit_eps = nullptr,
                                  std::function<void(float)> on_progress = nullptr);
    /// @brief Run the iterative PID deformation-correction loop.
    /// @param reference Undisturbed image.
    /// @param flow Disturbed image.
    /// @param on_progress Optional progress callback in [0, 1].
    /// @return PID-refined displacement field.
    VectorField ComputeWithPid(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                               std::function<void(float)> on_progress = nullptr);
    /// @brief Copy one interrogation window from the source image with zero padding at the edges.
    void ExtractPaddedWindow(const Eigen::MatrixXf& image, int start_row, int start_col,
                              Eigen::MatrixXf& window) const;
    /// @brief Build one PID-warped flow window from the local affine deformation model.
    void ExtractWarpedFlowWindow(const Eigen::MatrixXf& flow, int start_row, int start_col,
                                 int win_row, int win_col, const DeformationField& predictor,
                                 Eigen::MatrixXf& window) const;
    /// @brief Estimate the per-window PID deformation model from the current displacement field.
    DeformationField EstimateDeformationField(const VectorField& field) const;

    /// @brief Direct spatial cross-correlation (used for small windows or validation).
    Eigen::MatrixXf CrossCorrelationSpatial(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                            const Eigen::Ref<const Eigen::MatrixXf>& w_flow);

    /// @brief FFT-based normalized cross-correlation (primary path).
    Eigen::MatrixXf CrossCorrelationFFT(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                        const Eigen::Ref<const Eigen::MatrixXf>& w_flow);

    /// @brief Locate the cross-correlation peak and compute sub-pixel position, S2N, and CMM residual.
    PeakResult FindPeak(const Eigen::MatrixXf& ccmap);
};
