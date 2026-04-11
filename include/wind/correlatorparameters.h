#pragma once

/// @brief Parameters controlling the correlation engine.
struct CorrelatorParameters
{
    int window_size = 32; ///< Interrogation window side length (pixels).
    int overlap     = 24; ///< Overlap between adjacent windows (pixels). Step = window_size - overlap.
};
