#pragma once

#include <Eigen/Dense>

/// @brief A 2-D displacement field produced by correlation or post-processed by validation.
///
/// Each matrix is stored in row-major order with dimensions (height x width).
class VectorField
{
public:
    VectorField() = default;

    /// @brief Allocate zero-filled u, v, and s2n matrices of the given size.
    VectorField(const int rows, const int cols);

    /// @brief Write the field to a CSV file with columns: col, row, u, v, s2n.
    /// @param path Destination file path.
    void SaveCSV(const std::string& path) const;

    Eigen::MatrixXf u;   ///< Horizontal displacement component (pixels or scaled units).
    Eigen::MatrixXf v;   ///< Vertical displacement component (pixels or scaled units).
    Eigen::MatrixXf s2n; ///< Signal-to-noise ratio of the cross-correlation peak.

    int width  = 0; ///< Number of vectors along the x-axis.
    int height = 0; ///< Number of vectors along the y-axis.
};
