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
}

void Session::LoadFlow(const std::string& path)
{
    flow_path = path;
    flow.Load(path.c_str());

    if(ref.GetLoaded() && flow.GetLoaded())
    {
        stagestates[STAGE_PIV] = Ready;
    }
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

const Image& Session::GetRef() const
{
    return ref;
}

const Image& Session::GetFlow() const
{
    return flow;
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
