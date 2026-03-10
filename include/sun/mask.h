#pragma once

#include <Eigen/Dense>
#include <iostream>

class Mask
{
public:
    Mask() = default;
    Mask(const int w, const int h, const Eigen::Vector2f center , const float radius);

    void GenerateCircleMask(const int w, const int h, const Eigen::Vector2f center , const float radius);
    Eigen::MatrixXf ApplyMask(const Eigen::MatrixXf& data);

    const Eigen::MatrixXf& GetMask() const;
    bool GetSet() const;
    int GetWidth() const;
    int GetHeight() const;


private:
    Eigen::MatrixXf mask;

    bool set = false;
    int width = 0, height = 0;
};
