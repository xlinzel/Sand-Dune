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

/// @brief Surface reconstruction from a 2-D gradient field using the Frankot-Chellappa FFT method.
///
/// Integrates the @p u (dS/dx) and @p v (dS/dy) components of a VectorField
/// in the Fourier domain to produce a single-valued surface height map.
/// The method enforces integrability and is well-suited to noisy BOS gradient data.
class Reconstruction
{
public:
    Reconstruction() {};

    /// @brief Integrate the gradient field and return the reconstructed surface.
    /// @param data VectorField whose @p u and @p v components are the surface gradients (dn/dx, dn/dy).
/// @return Height map as a float matrix (rows x cols), in the same units as the input gradients.
    Eigen::MatrixXf Compute(const VectorField& data) const;

private:
    float eps = std::numeric_limits<float>::min(); ///< Regularisation term added to the denominator to avoid DC singularity.
};
