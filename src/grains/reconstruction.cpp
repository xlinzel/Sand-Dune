#include <grains/reconstruction.h>

Eigen::MatrixXf Reconstruction::Compute(const VectorField& data) const
{
    
}

Eigen::MatrixXf Reconstruction::Compute(const VectorField& data, const Eigen::Vector2f center , const float radius) const
{
    
}

Eigen::MatrixXf Reconstruction::Hanning(const VectorField& data) const
{
    /*for(int i = 0; i < rows; i++)
        hannr(i) = 0.5f * (1.0f - cos((2.0f * i * std::numbers::pi) / (rows - 1)));
    for(int j = 0; j < cols; j++)
        hannc(j) = 0.5f * (1.0f - cos((2.0f * j * std::numbers::pi) / (cols - 1)));*/
}

Eigen::MatrixXf Reconstruction::Hanning(const VectorField& data, const Eigen::Vector2f center , const float radius) const
{
    
}