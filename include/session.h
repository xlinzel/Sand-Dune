#pragma once

#include <future>
#include <chrono>
#include <fstream>

#include <sun/image.h>
#include <sun/mask.h>
#include <wind/vectorfield.h>
#include <grains/piv.h>
#include <grains/validation.h>
#include <grains/reconstruction.h>
#include <wind/opticalparameters.h>
#include <wind/pivparameters.h>

//https://link.springer.com/article/10.1007/s00348-015-1927-5
//https://link.springer.com/article/10.1007/s00348-005-0016-6
//https://link.springer.com/article/10.1007/s00348-010-0985-y

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
    ~Session();

    void LoadRef(const std::string& path);
    void LoadFlow(const std::vector<std::string>& paths);

    void RunPIV();
    void RunValidation();
    void RunReconstruction();
    
    bool IsRunning() const;

    void RunPIVAsync();
    void RunValidationAsync();
    void RunReconstructionAsync();
    void RunAllAsync();

    void ScaleFields();

    bool IsSaving() const;
    void SaveAsync(const std::string& base_path);

    void SavePIVCSV(const std::string& base_path);
    void SaveValCSV(const std::string& base_path);
    void SaveSurfaceCSV(const std::string& base_path);

    const Image& GetRef() const;
    const Image& GetFlow() const;
    
    const std::string& GetRefPath() const;
    const std::string& GetFlowPath() const;

    void SetActiveIndex(int i);
    int GetActiveIndex() const;

    bool HasFlow() const;
    int GetFlowCount() const;
    const std::vector<std::string>& GetFlowPaths() const;

    const VectorField& GetPIVField() const;
    const VectorField& GetValField() const;
    const Eigen::MatrixXf& GetSurface() const;
    StageState GetStageState(Stages s) const;
    //Mask open variables
    int posx = 0, posy = 0;
    int radius = 1000;
    float a = 0.1f;
    bool mask_apply = true;

    PIVParameters pivparameters;
    OpticalParameters opticalparameters;
    
    //Progress tracking
    std::atomic<float> progress{0.0f};
    std::chrono::steady_clock::time_point task_start;

    //Refraction correction
    bool n_correction = false;

    //Refractive index/thickness toggle
    bool b_ref = true;
private:
    std::string ref_path;
    std::vector<std::string> flow_paths;
    std::atomic<int> batch_index{0};
    Image ref;
    std::vector<Image> flows;
    Mask mask;

    std::vector<VectorField> raw_piv_field;
    std::vector<VectorField> piv_field;
    Eigen::MatrixXf correction[2];
    
    std::vector<VectorField> raw_val_field;
    std::vector<VectorField> val_field;

    std::vector<Eigen::MatrixXf> raw_surface;
    std::vector<Eigen::MatrixXf> surface;

    int active_index = 0;

    StageState stagestates[STAGE_TOTAL];

    //Async
    std::atomic<bool> stop_requested{false};
    std::future<void> activetask;
    std::future<void> save_task;
};