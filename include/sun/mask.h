#pragma once

#include <Eigen/Dense>
#include <iostream>
#include <numbers>

class Mask
{
public:
    Mask() = default;
    Mask(const int w, const int h, const Eigen::Vector2f center , const float radius);

    void GenBinCircleMask(const int w, const int h, const Eigen::Vector2f center , const float radius);
    void GenTukCircleMask(const int w, const int h, const Eigen::Vector2f center , const float radius, const float a);
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
