#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>

/// @brief Post-processing validation and outlier replacement for PIV vector fields.
///
/// Applies signal-to-noise and normalised-residual thresholds to flag spurious
/// vectors, then replaces them with a local median interpolation.
class Validation
{
public:
    Validation() {};

    /// @brief Validate and replace outliers in @p data.
    /// @param data Raw PIV vector field.
    /// @return A new VectorField with outliers replaced by median-interpolated values.
    const VectorField PostProcess(const VectorField& data) const;

    /// @brief Validate and replace outliers, with an additional external binary mask.
    /// @param data Raw PIV vector field.
    /// @param mask Boolean mask; false entries are treated as invalid regardless of metrics.
    /// @return A new VectorField with masked and outlier positions replaced.
    const VectorField PostProcess(const VectorField& data,
                                  const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic>& mask) const;

    /// @brief Compute a boolean validity mask for @p data without modifying it.
    /// @return Per-vector boolean array; true = valid, false = outlier.
    const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> Validate(const VectorField& data) const;

private:
    float s2n_threshold = 1.3f; ///< Minimum acceptable signal-to-noise ratio.
    float nrm_threshold = 2.0f; ///< Maximum normalised residual (UNO criterion).
    float eps           = 0.1f; ///< Small regularisation value added to the median denominator.
};
