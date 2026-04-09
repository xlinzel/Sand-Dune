#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>
#include <iostream>
#include <functional>
#include <fftw3.h>
#include <wind/pivparameters.h>

/// @brief Particle Image Velocimetry (PIV) cross-correlation engine.
///
/// Divides the reference and flow images into overlapping interrogation windows
/// and locates the displacement peak in the normalised cross-correlation map via FFT.
/// FFTW buffers are pre-allocated in the constructor and reused across calls for efficiency.
class PIV
{
public:
    /// @brief Result of a single-window cross-correlation peak search.
    struct PeakResult
    {
        float u;   ///< Sub-pixel horizontal displacement (pixels).
        float v;   ///< Sub-pixel vertical displacement (pixels).
        float s2n; ///< Signal-to-noise ratio (primary peak / secondary peak).
    };

public:
    PIV();

    /// @brief Construct with explicit window parameters.
    PIV(const int window_size, const int overlap, const int search_size);

    /// @brief Construct from a PIVParameters struct.
    PIV(const PIVParameters parameters);

    ~PIV();

    /// @brief Compute the displacement field between @p reference and @p flow.
    /// @param reference  Normalised float image of the undisturbed background pattern.
    /// @param flow       Normalised float image of the disturbed background pattern.
    /// @param on_progress Optional callback invoked with progress in [0, 1] after each row of windows.
    /// @return VectorField containing per-window (u, v, s2n) values.
    VectorField Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow,
                        std::function<void(float)> on_progress = nullptr);

    int GetWindowSize() const; ///< Interrogation window side length (pixels).
    int GetOverlap()    const; ///< Window overlap (pixels).
    int GetSearchSize() const; ///< Search region side length (pixels).

    /// @brief Resize FFTW buffers and rebuild plans for a new window size.
    void SetWindowSize(const int size);
    void SetOverlap(const int overlap);
    /// @brief Resize FFTW buffers and rebuild plans for a new search size.
    void SetSearchSize(const int size);

private:
    int window_size = 64;
    int overlap     = 50;
    int search_size = 72;

    // FFTW preallocated buffers, assigned in constructor
    float*         ref_in    = nullptr;
    float*         flow_in   = nullptr;
    fftwf_complex* ref_out   = nullptr;
    fftwf_complex* flow_out  = nullptr;
    fftwf_complex* product   = nullptr;
    float*         ccmap_raw = nullptr;

    int rows = 0, cols = 0, freq_cols = 0;

    fftwf_plan ref_plan  = nullptr;
    fftwf_plan flow_plan = nullptr;
    fftwf_plan inv_plan  = nullptr;

    Eigen::MatrixXf hann2d; ///< Pre-computed 2-D Hann window applied to each interrogation window.

    void AllocateFFTBuffers();
    void FreeFFTBuffers();

    /// @brief Direct spatial cross-correlation (used for small windows or validation).
    Eigen::MatrixXf CrossCorrelationSpatial(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow);

    /// @brief FFT-based normalised cross-correlation (primary path).
    Eigen::MatrixXf CrossCorrelationFFT(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow);

    /// @brief Locate the cross-correlation peak and compute sub-pixel position and S2N.
    PeakResult FindPeak(const Eigen::MatrixXf& ccmap, const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow,
                        double& t_patch, double& t_phi, double& t_R, double& t_gamma, double& t_nr);
};
