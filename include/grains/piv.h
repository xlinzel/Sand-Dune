#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>
#include <iostream>
#include <fftw3.h>

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
    ~PIV();

    VectorField Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow);

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

    //FFTW preallocated buffers, assigned in constructor
    float* ref_in = nullptr;
    float* flow_in = nullptr;
    fftwf_complex* ref_out = nullptr;
    fftwf_complex* flow_out = nullptr;
    fftwf_complex* product = nullptr;
    float* ccmap_raw = nullptr;

    int rows = 0, cols = 0, freq_cols = 0;

    fftwf_plan ref_plan = nullptr;
    fftwf_plan flow_plan = nullptr;
    fftwf_plan inv_plan = nullptr;

    Eigen::MatrixXf hann2d;
    
    void AllocateFFTBuffers();
    void FreeFFTBuffers();

    Eigen::MatrixXf CrossCorrelationSpatial(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow);
    Eigen::MatrixXf CrossCorrelationFFT(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow);
    PeakResult FindPeak(const Eigen::MatrixXf& ccmap);

};