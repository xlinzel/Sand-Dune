#pragma once

#include <wind/vectorfield.h>
#include <limits>
#include <fftw3.h>

/// @brief References for the Frankot-Chellappa integration method:
/// - https://cpb.iphy.ac.cn/article/2017/1892/cpb_26_6_064701.html
/// - https://wavepy.readthedocs.io/en/latest/source/api/wavepy.surface_from_grad.html (most important)
/// - https://ieeexplore.ieee.org/document/5995427
/// - https://ieeexplore.ieee.org/document/4587414
/// - https://arxiv.org/abs/1308.4292
/// - https://www.researchgate.net/publication/259099431_Direct_regularized_surface_reconstruction_from_gradients_for_Industrial_Photometric_Stereo

/// @brief References for the POisson least-squares integration method:
/// - https://www.cs.cmu.edu/~ILIM/projects/IM/aagrawal/eccv06/AgrawalECCV06.pdf
/// - https://www.cs.cmu.edu/~ILIM/projects/IM/aagrawal/integrability_iccv05.pdf

/// @brief Surface reconstruction from a 2-D displacement field using various methods.
///
/// In the current pipeline the input field contains raw BOS displacements in
/// pixel units on the correlation grid; physical scaling is applied later in
/// Session::ScaleFields().
class Reconstruction
{
public:
    Reconstruction() {};

    /// @brief Integrate (Franko-Chellapa Method) the displacement field and return the reconstructed relative surface. DEPRECATED METHOD
    /// @param data VectorField whose @p u and @p v components are raw BOS displacements in pixel units.
    /// @return Relative surface map on the correlation grid before physical scaling is applied.
    Eigen::MatrixXf ComputeFC(const VectorField& data) const;

    
    /// @brief Integrate the displacement field with a mask-aware grid-based Poisson solve.
    /// @param data VectorField whose @p u and @p v components are raw BOS displacements in pixel units.
    /// @param mask Eigen matrix with 1 as valid positions and 0 as invalid or unknown positions.
    /// @return Relative surface map on the correlation grid before physical scaling is applied.
    Eigen::MatrixXf Compute(const VectorField& data, const Eigen::MatrixXf& mask) const;

private:
    float eps = std::numeric_limits<float>::min(); ///< Regularisation term added to the denominator to avoid DC singularity.
};
