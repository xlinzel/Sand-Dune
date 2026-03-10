#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>
#include <iostream>

class PIV
{
public:

    struct PeakResult
    {
        float u;
        float v;
        float s2n;
    };

public:

    PIV();
    PIV(const int window_size, const int overlap, const int search_size);

    VectorField Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow);
    Eigen::MatrixXf CrossCorrelation(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow);
    PeakResult FindPeak(const Eigen::MatrixXf& ccmap);

    int GetWindowSize() const;
    int GetOverlap() const;
    int GetSearchSize() const;
    void SetWindowSize(const int size);
    void SetOverlap(const int overlap);
    void SetSearchSize(const int size);


private:
    int window_size = 64;
    int overlap = 50;
    int search_size = 72;
};