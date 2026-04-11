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

    int window_size = 64;
    int overlap     = 50;

    // FFTW preallocated buffers, assigned in constructor
    float*         ref_in    = nullptr;
    float*         flow_in   = nullptr;
    fftwf_complex* ref_out   = nullptr;
    fftwf_complex* flow_out  = nullptr;
    fftwf_complex* product   = nullptr;
    float*         ccmap_raw = nullptr;

    //Store fft for findpeak use
    fftwf_complex* ref_out_saved = nullptr;

    int rows = 0, cols = 0, freq_cols = 0;

    fftwf_plan ref_plan  = nullptr;
    fftwf_plan flow_plan = nullptr;
    fftwf_plan inv_plan  = nullptr;

    //////////////////////////////////////////////////////
    // Internal Helpers
    //////////////////////////////////////////////////////

    void AllocateFFTBuffers();
    void FreeFFTBuffers();

    /// @brief Direct spatial cross-correlation (used for small windows or validation).
    Eigen::MatrixXf CrossCorrelationSpatial(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                           const Eigen::Ref<const Eigen::MatrixXf>& w_flow);

    /// @brief FFT-based normalised cross-correlation (primary path).
    Eigen::MatrixXf CrossCorrelationFFT(const Eigen::Ref<const Eigen::MatrixXf>& w_reference,
                                       const Eigen::Ref<const Eigen::MatrixXf>& w_flow);

    /// @brief Locate the cross-correlation peak and compute sub-pixel position and S2N.
    PeakResult FindPeak(const Eigen::MatrixXf& ccmap);
};
