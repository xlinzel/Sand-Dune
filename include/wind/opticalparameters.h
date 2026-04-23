#pragma once

/// @brief Physical parameters describing the BOS optical setup.
///
/// All distances are in millimetres and the pixel pitch is in micrometres,
/// matching the unit conventions used throughout the UI and in ScaleFields().
struct OpticalParameters
{
    // --- Sample properties ---
    float t   = 1.0f;   ///< Known sample thickness used for RI scaling and refraction correction (mm).
    float n   = 1.08f;  ///< Refractive index used for refraction correction and thickness-mode scaling.

    // --- Camera / lens ---
    float P_px = 2.315f; ///< Sensor pixel pitch (um). This is a sensor property, not a lens property.
    float f    = 25.0f;  ///< Lens focal length (mm).

    // --- Setup geometry ---
    float Z_d = 218.0f; ///< Distance from background dot pattern to the sample (mm).
    float Z_a = 45.0f;  ///< Distance from the sample to the lens front principal plane (mm).

    // --- Sensitivity / resolution helpers ---
    float d_a  = 10.5f; ///< Lens aperture diameter (mm). Used for depth-of-field estimates.
    float d_bg = 0.1f;  ///< Background dot diameter (mm). Used for resolution estimates.
};
