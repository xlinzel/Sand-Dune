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
    raw_piv_field = piv.Compute(ref.GetMat(), flow.GetMat());

    stagestates[STAGE_PIV] = Done;
    stagestates[STAGE_VAL] = Ready;

    return;
}

void Session::RunValidation()
{
    if(stagestates[STAGE_VAL] == Idle)
        return;

    Validation post;
    raw_val_field = post.PostProcess(raw_piv_field);

    stagestates[STAGE_VAL] = Done;
    stagestates[STAGE_RECON] = Ready;

    return;
}

void Session::RunReconstruction()
{
    if(stagestates[STAGE_RECON] == Idle)
        return;

    Reconstruction recon;
    surface = recon.Compute(raw_val_field);

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
        PIV piv(pivparameters);
        
        raw_piv_field = piv.Compute(ref.GetMat(), flow.GetMat());

        ScaleFields();

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
        raw_val_field = post.PostProcess(raw_piv_field);

        ScaleFields();

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
        if(mask_apply)
            mask.GenBinCircleMask(ref.GetWidth(), ref.GetHeight(), {posx, posy}, radius);

        Reconstruction recon;

        
        if(mask_apply)
        {
            VectorField temp = raw_val_field;
            temp.u = mask.ApplyMask(temp.u);
            temp.v = mask.ApplyMask(temp.u);
            raw_surface = recon.Compute(temp);
        }
        else
            raw_surface = recon.Compute(raw_val_field);

        ScaleFields();

        stagestates[STAGE_RECON] = Done;
    });

    return;
}

void Session::ScaleFields()
{
    // Clamp parameters to physically valid minimums to prevent division by zero
    opticalparameters.t    = std::max(opticalparameters.t,    0.001f);
    opticalparameters.P_px = std::max(opticalparameters.P_px, 0.001f);
    opticalparameters.Z_d  = std::max(opticalparameters.Z_d,  0.001f);
    opticalparameters.Z_a  = std::max(opticalparameters.Z_a,  0.001f);
    opticalparameters.f    = std::max(opticalparameters.f,    0.001f);

    // Convert pixel displacement -> dn/d(grid index), fully scaled
    // Reconstruction only needs to integrate — no further scaling required
    float Z_B   = opticalparameters.Z_d + opticalparameters.Z_a;
    float z_i   = opticalparameters.f * Z_B / (Z_B - opticalparameters.f);
    float scale = opticalparameters.P_px * 1e-3f
                * opticalparameters.Z_a * (Z_B - opticalparameters.f)
                / (opticalparameters.f * opticalparameters.Z_d * opticalparameters.t * z_i);

    if(stagestates[STAGE_PIV] != Idle && stagestates[STAGE_PIV] != Ready)
    {
        piv_field.u      = raw_piv_field.u.array() * scale;
        piv_field.v      = raw_piv_field.v.array() * scale;
        piv_field.s2n    = raw_piv_field.s2n;
        piv_field.width  = raw_piv_field.width;
        piv_field.height = raw_piv_field.height;
    }

    if(stagestates[STAGE_VAL] != Idle && stagestates[STAGE_VAL] != Ready)
    {
        val_field.u      = raw_val_field.u.array() * scale;
        val_field.v      = raw_val_field.v.array() * scale;
        val_field.s2n    = raw_val_field.s2n;
        val_field.width  = raw_val_field.width;
        val_field.height = raw_val_field.height;
    }
    
    if(stagestates[STAGE_RECON] != Idle && stagestates[STAGE_RECON] != Ready)
    {
        surface = raw_surface.array() * scale;
    }
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

const VectorField& Session::GetPIVField() const
{
    return piv_field;
}

const VectorField& Session::GetValField() const
{
    return val_field;
}

const Eigen::MatrixXf& Session::GetSurface() const
{
    return surface;
}

StageState Session::GetStageState(Stages s) const
{
    return stagestates[s];
}
