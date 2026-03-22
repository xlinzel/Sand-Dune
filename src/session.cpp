#include <session.h>

Session::Session()
{
    stagestates[STAGE_PIV] = Idle;
    stagestates[STAGE_VAL] = Idle;
    stagestates[STAGE_RECON] = Idle;
}

void Session::LoadRef(const std::string& path)
{
    ref_path = path;
    ref.Load(path.c_str());

    if(ref.GetLoaded() && flow.GetLoaded())
    {
        stagestates[STAGE_PIV] = Ready;
    }
    
    posx = GetRef().GetWidth() / 2;
    posy = GetRef().GetHeight() / 2;
}

void Session::LoadFlow(const std::string& path)
{
    flow_path = path;
    flow.Load(path.c_str());

    if(ref.GetLoaded() && flow.GetLoaded())
    {
        stagestates[STAGE_PIV] = Ready;
    }

    posx = GetFlow().GetWidth() / 2;
    posy = GetFlow().GetHeight() / 2;
}

void Session::RunPIV()
{
    if(stagestates[STAGE_PIV] == Idle)
        return;

    PIV piv(pivparameters);
    rawfield = piv.Compute(ref.GetMat(), flow.GetMat());

    stagestates[STAGE_PIV] = Done;
    stagestates[STAGE_VAL] = Ready;

    return;
}

void Session::RunValidation()
{
    if(stagestates[STAGE_VAL] == Idle)
        return;

    Validation post;
    processfield = post.PostProcess(rawfield);

    stagestates[STAGE_VAL] = Done;
    stagestates[STAGE_RECON] = Ready;

    return;
}

void Session::RunReconstruction()
{
    if(stagestates[STAGE_RECON] == Idle)
        return;

    Reconstruction recon;
    surface = recon.Compute(processfield);

    stagestates[STAGE_RECON] = Done;

    return;
}

bool Session::IsRunning() const
{
    return activetask.valid() &&
             activetask.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void Session::RunPIVAsync()
{
    if(stagestates[STAGE_PIV] == Idle || IsRunning())
        return;

    stagestates[STAGE_PIV] = Busy;
    if(stagestates[STAGE_VAL]   == Done) stagestates[STAGE_VAL]   = Dirty;
    if(stagestates[STAGE_RECON] == Done) stagestates[STAGE_RECON] = Dirty;

    activetask = std::async(std::launch::async, [this]()
    {
        if(mask_apply)
            mask.GenBinCircleMask(ref.GetWidth(), ref.GetHeight(), {posx, posy}, radius);

        PIV piv(pivparameters);
        
        if(mask_apply)
            rawfield = piv.Compute(mask.ApplyMask(ref.GetMat()), mask.ApplyMask(flow.GetMat()));
        else
            rawfield = piv.Compute(ref.GetMat(), flow.GetMat());

        // Convert pixel displacement -> dn/d(grid index), fully scaled
        // Reconstruction only needs to integrate — no further scaling required
        float Z_B   = opticalparameters.Z_d + opticalparameters.Z_a;
        float z_i   = opticalparameters.f * Z_B / (Z_B - opticalparameters.f);
        float scale = opticalparameters.P_px * 1e-3f
                      * opticalparameters.Z_a * (Z_B - opticalparameters.f)
                      / (opticalparameters.f * opticalparameters.Z_d * opticalparameters.t * z_i);
        rawfield.u.array() *= scale;
        rawfield.v.array() *= scale;

        stagestates[STAGE_PIV] = Done;
        stagestates[STAGE_VAL] = Ready;
    });

    return;
}

void Session::RunValidationAsync()
{
    if(stagestates[STAGE_VAL] == Idle || IsRunning())
        return;

    stagestates[STAGE_VAL] = Busy;
    if(stagestates[STAGE_RECON] == Done) stagestates[STAGE_RECON] = Dirty;

    activetask = std::async(std::launch::async, [this]()
    {
        Validation post;
        processfield = post.PostProcess(rawfield);

        stagestates[STAGE_VAL] = Done;
        stagestates[STAGE_RECON] = Ready;
    });

    return;
}

void Session::RunReconstructionAsync()
{
    if(stagestates[STAGE_RECON] == Idle || IsRunning())
        return;

    stagestates[STAGE_RECON] = Busy;

    activetask = std::async(std::launch::async, [this]()
    {
        Reconstruction recon;
        surface = recon.Compute(processfield);

        stagestates[STAGE_RECON] = Done;
    });

    return;
}

const Image& Session::GetRef() const
{
    return ref;
}

const Image& Session::GetFlow() const
{
    return flow;
}

const std::string& Session::GetRefPath() const
{
    return ref_path;
}

const std::string& Session::GetFlowPath() const
{
    return flow_path;
}

const VectorField& Session::GetRawField() const
{
    return rawfield;
}

const VectorField& Session::GetProcessedField() const
{
    return processfield;
}

const Eigen::MatrixXf& Session::GetSurface() const
{
    return surface;
}

StageState Session::GetStageState(Stages s) const
{
    return stagestates[s];
}
