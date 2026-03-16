#pragma once

#include <Eigen/Dense>

class VectorField
{
public:
    VectorField() = default;
    VectorField(const int width, const int height);

    void SaveCSV(const std::string& path) const;

    Eigen::MatrixXf u;
    Eigen::MatrixXf v;
    Eigen::MatrixXf s2n;
    int width = 0;
    int height = 0;
};