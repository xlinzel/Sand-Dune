#pragma once

#include <Eigen/Dense>
#include <wind/vectorfield.h>

class Validation
{
public:
    Validation() {};

    const VectorField PostProcess(const VectorField& data) const;
    const VectorField PostProcess(const VectorField& data, const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic>& mask) const;
    const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> Validate(const VectorField& data) const; //Returns a mask of validated positions
    

private:

    float s2n_threshold = 1.3;
    float nrm_threshold = 2.0; //Normalized residual mean threshold
    float eps = 0.1; 
};