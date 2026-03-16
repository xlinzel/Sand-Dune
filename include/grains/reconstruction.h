#pragma once

#include <wind/vectorfield.h>
#include <limits>

//https://cpb.iphy.ac.cn/article/2017/1892/cpb_26_6_064701.html

//Most important (FFT method, not technically the best)
//https://wavepy.readthedocs.io/en/latest/source/api/wavepy.surface_from_grad.html

//MATLAB Surface reconstruction sources
//https://ieeexplore.ieee.org/document/5995427
//https://ieeexplore.ieee.org/document/4587414
//https://arxiv.org/abs/1308.4292
//https://www.researchgate.net/publication/259099431_Direct_regularized_surface_reconstruction_from_gradients_for_Industrial_Photometric_Stereo

class Reconstruction
{
public:
    Reconstruction();

    //Full pipeline
    Eigen::MatrixXf Compute(const VectorField& data) const;
    Eigen::MatrixXf Compute(const VectorField& data, const Eigen::Vector2f center , const float radius) const;

private:
    //Sub steps???

    //Apply Hanning window, circular if specified???
    Eigen::MatrixXf Hanning(const VectorField& data) const;
    Eigen::MatrixXf Hanning(const VectorField& data, const Eigen::Vector2f center , const float radius) const;

    float eps = std::numeric_limits<float>::min();
};