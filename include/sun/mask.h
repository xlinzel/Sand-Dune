#pragma once

#include <Eigen/Dense>
#include <iostream>
#include <numbers>

/// @brief Generates and applies spatial masks to field matrices.
///
/// Masks are float matrices with values in [0, 1].  A hard binary circle sets
/// pixels outside the radius to 0; the Tukey variant applies a soft cosine
/// roll-off at the edge controlled by the parameter @p a.
class Mask
{
public:
    Mask() = default;

    /// @brief Construct and immediately generate a binary circular mask.
    /// @param w      Field width (columns).
    /// @param h      Field height (rows).
    /// @param center Mask centre in field coordinates (col, row).
    /// @param radius Mask radius in field units.
    Mask(const int w, const int h, const Eigen::Vector2f center, const float radius);

    /// @brief Generate a hard binary circular mask (1 inside, 0 outside).
    void GenBinCircleMask(const int w, const int h, const Eigen::Vector2f center, const float radius);

    /// @brief Generate a Tukey-windowed circular mask with cosine roll-off at the edge.
    /// @param w      Field width (columns).
    /// @param h      Field height (rows).
    /// @param center Mask centre in field coordinates (col, row).
    /// @param radius Mask radius in field units.
    /// @param a      Roll-off fraction in [0, 1].  0 = hard cutoff, 1 = full Hann window.
    void GenTukCircleMask(const int w, const int h, const Eigen::Vector2f center, const float radius, const float a);

    /// @brief Multiply @p data element-wise by the mask and return the result.
    Eigen::MatrixXf ApplyMask(const Eigen::MatrixXf& data);

    const Eigen::MatrixXf& GetMask()  const; ///< Read-only access to the raw mask matrix.
    bool GetSet()    const; ///< Returns true if a mask has been generated.
    int  GetWidth()  const; ///< Mask width in columns.
    int  GetHeight() const; ///< Mask height in rows.

private:
    Eigen::MatrixXf mask;
    bool set    = false;
    int  width  = 0;
    int  height = 0;
};
