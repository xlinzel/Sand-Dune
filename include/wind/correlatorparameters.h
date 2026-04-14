#pragma once

/// @brief Parameters controlling the correlation engine.
struct CorrelatorParameters
{
    int window_size = 32; ///< Interrogation window side length (pixels).
    int overlap     = 24; ///< Overlap between adjacent windows (pixels). Step = window_size - overlap.
    bool enable_pid = false; ///< Enable first-order iterative window deformation correction.
    int pid_iterations = 2; ///< Number of deformation-correction passes after the initial solve.
    float pid_relaxation = 1.0f; ///< Scale factor applied to each residual correction update.
    int pid_smoothing_passes = 1; ///< Box-smoothing passes applied to deformation gradients.
};
