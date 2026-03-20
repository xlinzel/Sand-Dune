#pragma once

#include <future>

#include <sun/image.h>
#include <sun/mask.h>
#include <wind/vectorfield.h>
#include <grains/piv.h>
#include <grains/validation.h>
#include <grains/reconstruction.h>
#include <wind/opticalparameters.h>
#include <wind/pivparameters.h>

enum StageState
{
    Idle,
    Ready,
    Busy,
    Done,
    Dirty
};

enum Stages
{
    STAGE_PIV,
    STAGE_VAL,
    STAGE_RECON,
    STAGE_TOTAL
};

class Session
{
public:
    Session();

    void LoadRef(const std::string& path);
    void LoadFlow(const std::string& path);

    void RunPIV();
    void RunValidation();
    void RunReconstruction();
    
    bool IsRunning() const;
    void RunPIVAsync();
    void RunValidationAsync();
    void RunReconstructionAsync();

    //TODO: apply mask function

    const Image& GetRef() const;
    const Image& GetFlow() const;
    
    const std::string& GetRefPath() const;
    const std::string& GetFlowPath() const;

    const VectorField& GetRawField() const;
    const VectorField& GetProcessedField() const;
    const Eigen::MatrixXf& GetSurface() const;
    StageState GetStageState(Stages s) const;
    //Mask open variables
    int posx = 0, posy = 0;
    int radius = 1000;
    float a = 0.1f;
    bool mask_apply = true;

    PIVParameters pivparameters;
    OpticalParameters opticalparameters;
private:
    std::string ref_path, flow_path;
    Image ref, flow;
    Mask mask;

    VectorField rawfield, processfield;
    Eigen::MatrixXf surface;

    StageState stagestates[STAGE_TOTAL];

    //Async
    std::future<void> activetask;
};