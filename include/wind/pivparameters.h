#pragma once

/// @brief Parameters controlling the PIV cross-correlation algorithm.
struct PIVParameters
{
    int window_size = 32; ///< Interrogation window side length (pixels).
    int overlap     = 24; ///< Overlap between adjacent windows (pixels). Step = window_size - overlap.
    int search_size = 42; ///< Search region side length for the flow image (pixels). Must be >= window_size.
};
