#include <wind/vectorfield.h>

VectorField::VectorField(const int width, const int height)
    : width(width), height(height)
{
    u = Eigen::MatrixXf::Zero(width, height);
    v = Eigen::MatrixXf::Zero(width, height);
    s2n = Eigen::MatrixXf::Zero(width, height);
}