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
    
    posx = GetFlow().GetWidth() / 2;
    posy = GetFlow().GetHeight() / 2;
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
    surface = recon.Compute(processfield, opticalparameters);

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

    activetask = std::async(std::launch::async, [this]()
    {
        PIV piv(pivparameters);
        rawfield = piv.Compute(ref.GetMat(), flow.GetMat());
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
        surface = recon.Compute(processfield, opticalparameters);

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
