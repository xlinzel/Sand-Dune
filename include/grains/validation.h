#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>

/// @brief Post-processing validation and outlier replacement for correlation vector fields.
///
/// Applies signal-to-noise and normalised-residual thresholds to flag spurious
/// vectors, then replaces them with a local median interpolation.
class Validation
{
public:
    Validation() {};

    /// @brief Validate and replace outliers in @p data.
    /// @param data Raw correlation vector field.
    /// @return A new VectorField with outliers replaced by median-interpolated values.
    const VectorField PostProcess(const VectorField& data) const;

    /// @brief Validate and replace outliers, with an additional external binary mask.
    /// @param data Raw correlation vector field.
    /// @param mask Boolean mask; false entries are treated as invalid regardless of metrics.
    /// @return A new VectorField with masked and outlier positions replaced.
    const VectorField PostProcess(const VectorField& data,
                                  const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic>& mask) const;

    /// @brief Compute a boolean validity mask for @p data without modifying it.
    /// @param data Raw correlation vector field.
    /// @return Per-vector boolean array; true = valid, false = outlier.
    const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> Validate(const VectorField& data) const;

    /// @brief Evaluate the UNO outlier criterion for one vector using a prepared neighbourhood.
    /// @param i Row index of the candidate vector.
    /// @param j Column index of the candidate vector.
    /// @param data Source vector field being tested.
    /// @param u_n Sorted u-component neighbourhood values around (i, j).
    /// @param v_n Sorted v-component neighbourhood values around (i, j).
    /// @param n Number of valid neighbourhood entries stored in @p u_n and @p v_n.
    /// @param u_med Median u-component neighbourhood value.
    /// @param v_med Median v-component neighbourhood value.
    /// @return True when the combined normalized residual exceeds @ref nrm_threshold.
    bool OutlierComp(int i, int j, const VectorField& data, std::array<float, 8>& u_n, std::array<float, 8>& v_n,
                    int n,  float u_med, float v_med) const;

private:
    float s2n_threshold = 1.3f; ///< Minimum acceptable signal-to-noise ratio.
    float nrm_threshold = 2.0f; ///< Maximum normalised residual (UNO criterion).
    float eps           = 0.1f; ///< Small regularisation value added to the median denominator.
};
