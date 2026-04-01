#include <session.h>

Session::Session()
{
    stagestates[STAGE_PIV] = Idle;
    stagestates[STAGE_VAL] = Idle;
    stagestates[STAGE_RECON] = Idle;
}

Session::~Session()
{
    stop_requested = true;
    if(activetask.valid()) activetask.wait();
    if(save_task.valid())  save_task.wait();
}

void Session::LoadRef(const std::string& path)
{
    ref_path = path;
    ref.Load(path.c_str());

    if(ref.GetLoaded() && !flows.empty() && flows[0].GetLoaded())
    {
        stagestates[STAGE_PIV] = Ready;
    }
    
    posx = GetRef().GetWidth() / 2;
    posy = GetRef().GetHeight() / 2;
}

void Session::LoadFlow(const std::vector<std::string>& paths)
{
    flow_paths = paths;
    flows.clear();
    flows.resize(paths.size());

    // Reset pipeline
    stagestates[STAGE_PIV]   = Idle;
    stagestates[STAGE_VAL]   = Idle;
    stagestates[STAGE_RECON] = Idle;
    raw_piv_field.clear(); piv_field.clear();
    raw_val_field.clear(); val_field.clear();
    raw_surface.clear();   surface.clear();
    active_index = 0;

    for(int i = 0; i < paths.size(); i++)
        flows[i].Load(paths[i].c_str());

    if(ref.GetLoaded() && !flows.empty() && flows[0].GetLoaded())
      {
          stagestates[STAGE_PIV] = Ready;
          posx = flows[0].GetWidth()  / 2;
          posy = flows[0].GetHeight() / 2;
      }
}

void Session::RunPIV()
{
    if(stagestates[STAGE_PIV] == Idle)
        return;

    PIV piv(pivparameters);
    raw_piv_field.resize(flows.size());

    for(int i = 0; i < (int)flows.size(); i++)
    {
        raw_piv_field[i] = piv.Compute(ref.GetMat(), flows[i].GetMat());
    }

    stagestates[STAGE_PIV] = Done;
    stagestates[STAGE_VAL] = Ready;

    return;
}

void Session::RunValidation()
{
    if(stagestates[STAGE_VAL] == Idle)
        return;

    Validation post;
    raw_val_field.resize(raw_piv_field.size());

    for(int i = 0; i < (int)raw_piv_field.size(); i++)
    {
        raw_val_field[i] = post.PostProcess(raw_piv_field[i]);
    }

    stagestates[STAGE_VAL] = Done;
    stagestates[STAGE_RECON] = Ready;

    return;
}

void Session::RunReconstruction()
{
    if(stagestates[STAGE_RECON] == Idle)
        return;

    Reconstruction recon;
    raw_surface.resize(raw_val_field.size());
    surface.resize(raw_val_field.size());

    for(int i = 0; i < (int)raw_val_field.size(); i++)
    {
        raw_surface[i] = recon.Compute(raw_val_field[i]);
        surface[i]     = raw_surface[i];
    }

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

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();

    activetask = std::async(std::launch::async, [this]()
    {
        PIV piv(pivparameters);
        int n = (int)flows.size();
        raw_piv_field.resize(n);

        for(int i = 0; i < n; i++)
        {
            raw_piv_field[i] = piv.Compute(ref.GetMat(), flows[i].GetMat(),
                [this, i, n](float p){ progress = (i + p) / n; });
        }

        if(stop_requested) return;

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

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();

    activetask = std::async(std::launch::async, [this]()
    {
        Validation post;
        raw_val_field.resize(raw_piv_field.size());

        for(int i = 0; i < (int)raw_piv_field.size(); i++)
        {
            raw_val_field[i] = post.PostProcess(raw_piv_field[i]);
            progress = (float)(i + 1) / raw_piv_field.size();
        }

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
        {
            float step = (float)(pivparameters.window_size - pivparameters.overlap);
            mask.GenBinCircleMask(raw_val_field[0].width, raw_val_field[0].height,
                {posx / step, posy / step}, radius / step);
        }

        Reconstruction recon;
        int n = (int)raw_val_field.size();
        raw_surface.resize(n);

        for(int i = 0; i < n; i++)
        {
            if(mask_apply)
            {
                VectorField temp = raw_val_field[i];
                temp.u = mask.ApplyMask(temp.u);
                temp.v = mask.ApplyMask(temp.v);
                raw_surface[i] = recon.Compute(temp);
            }
            else
                raw_surface[i] = recon.Compute(raw_val_field[i]);

            progress = (float)(i + 1) / n;
        }

        ScaleFields();

        stagestates[STAGE_RECON] = Done;
    });

    return;
}



void Session::RunAllAsync()
{
    if(stagestates[STAGE_PIV] == Idle || IsRunning())
        return;

    stagestates[STAGE_PIV]   = Busy;
    stagestates[STAGE_VAL]   = Idle;
    stagestates[STAGE_RECON] = Idle;

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();

    activetask = std::async(std::launch::async, [this]()
    {
        int n = (int)flows.size();

        //Refrction correciton pre-computations
        float f    = opticalparameters.f    * 1e-3f;
        float Z_a  = opticalparameters.Z_a  * 1e-3f;
        float Z_d  = opticalparameters.Z_d  * 1e-3f;
        float P_px = opticalparameters.P_px * 1e-6f;
        float t    = opticalparameters.t    * 1e-3f;
        float Z_B  = Z_a + Z_d;
        float z_i  = f * Z_B / (Z_B - f); 
        int   step = pivparameters.window_size - pivparameters.overlap;

        float m1 = Z_a / z_i;

        //Pre-allocation
        float sx = 0;
        float sy = 0;

        float rx = 0;
        float ry = 0;

        float theta_x = 0;
        float theta_y = 0;

        float thetar_x = 0;
        float thetar_y = 0;

        float dx = 0; //lateral ray displacement
        float dy = 0; //lateral ray displacement

        float dsx = 0;
        float dsy = 0;

        // --- PIV ---
        PIV piv(pivparameters);
        raw_piv_field.resize(n);
        bool correction_computed = false;

        for(int i = 0; i < n && !stop_requested; i++)
        {
            raw_piv_field[i] = piv.Compute(ref.GetMat(), flows[i].GetMat(),
                [this, i, n](float p){ progress = (i + p) / n; });

            if(n_correction)
            {
                if(!correction_computed)
                {
                    int h = raw_piv_field[i].height;
                    int w = raw_piv_field[i].width;
                    correction[0] = Eigen::MatrixXf::Zero(h, w);
                    correction[1] = Eigen::MatrixXf::Zero(h, w);
                    
                    for(int row = 0; row < raw_piv_field[i].height; row++)
                    {
                        for(int col = 0; col < raw_piv_field[i].width; col++)
                        {
                            // physical position on sensor relative to centre
                            sx = (col - raw_piv_field[i].width/2.0f)  * step * P_px;
                            sy = (row - raw_piv_field[i].height/2.0f) * step * P_px;

                            rx = sx * m1;
                            ry = sy * m1;

                            theta_x = atanf(rx / Z_a);
                            theta_y = atanf(ry / Z_a);

                            thetar_x = asinf(sinf(theta_x) / opticalparameters.n);
                            thetar_y = asinf(sinf(theta_y) / opticalparameters.n);

                            dx = sinf(theta_x - thetar_x) * t / cosf(thetar_x); //lateral ray displacement
                            dy = sinf(theta_y - thetar_y) * t / cosf(thetar_y); //lateral ray displacement

                            dsx = dx / sinf((std::numbers::pi / 2) - theta_x);
                            dsy = dy / sinf((std::numbers::pi / 2) - theta_y);

                            correction[0](row, col) = dsx * z_i / (Z_B * P_px);
                            correction[1](row, col) = dsy * z_i / (Z_B * P_px);
                        }
                    }
                    correction_computed = true;
                }

                raw_piv_field[i].u -= correction[0];
                raw_piv_field[i].v -= correction[1];
            }
        }

        if(stop_requested) return;
        ScaleFields();
        stagestates[STAGE_PIV] = Done;
        stagestates[STAGE_VAL] = Busy;

        // --- Validation ---
        Validation post;
        raw_val_field.resize(n);
        for(int i = 0; i < n && !stop_requested; i++)
        {
            raw_val_field[i] = post.PostProcess(raw_piv_field[i]);
            progress = (float)(i + 1) / n;
        }

        if(stop_requested) return;
        ScaleFields();
        stagestates[STAGE_VAL] = Done;
        stagestates[STAGE_RECON] = Busy;

        // --- Reconstruction ---
        if(mask_apply)
        {
            float step = (float)(pivparameters.window_size - pivparameters.overlap);
            mask.GenBinCircleMask(raw_val_field[0].width, raw_val_field[0].height,
                {posx / step, posy / step}, radius / step);
        }

        Reconstruction recon;
        raw_surface.resize(n);
        for(int i = 0; i < n && !stop_requested; i++)
        {
            if(mask_apply)
            {
                VectorField temp = raw_val_field[i];
                temp.u = mask.ApplyMask(temp.u);
                temp.v = mask.ApplyMask(temp.v);
                raw_surface[i] = recon.Compute(temp);
            }
            else
                raw_surface[i] = recon.Compute(raw_val_field[i]);

            progress = (float)(i + 1) / n;
        }

        if(stop_requested) return;
        ScaleFields();
        stagestates[STAGE_RECON] = Done;
    });
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
    // Reconstruction only needs to integrate - no further scaling required
    float Z_B   = opticalparameters.Z_d + opticalparameters.Z_a;
    float z_i   = opticalparameters.f * Z_B / (Z_B - opticalparameters.f);

    float term = b_ref 
                    ? opticalparameters.t                          // RI mode: divide by thickness
                    : (opticalparameters.n - 1.0f);               // thickness mode: divide by (n-1)
    term = std::max(term, 0.001f);

    int step = pivparameters.window_size - pivparameters.overlap;
    float scale = (float)step * opticalparameters.P_px * 1e-3
                * (Z_B - opticalparameters.f)
                / (opticalparameters.f * opticalparameters.Z_d * term);

    if(stagestates[STAGE_PIV] != Idle && stagestates[STAGE_PIV] != Ready)
    {
        piv_field.resize(raw_piv_field.size());
        for(int i = 0; i < (int)raw_piv_field.size(); i++)
        {
            piv_field[i].u      = raw_piv_field[i].u.array() * scale;
            piv_field[i].v      = raw_piv_field[i].v.array() * scale;
            piv_field[i].s2n    = raw_piv_field[i].s2n;
            piv_field[i].width  = raw_piv_field[i].width;
            piv_field[i].height = raw_piv_field[i].height;
        }
    }

    if(stagestates[STAGE_VAL] != Idle && stagestates[STAGE_VAL] != Ready)
    {
        val_field.resize(raw_val_field.size());
        for(int i = 0; i < (int)raw_val_field.size(); i++)
        {
            val_field[i].u      = raw_val_field[i].u.array() * scale;
            val_field[i].v      = raw_val_field[i].v.array() * scale;
            val_field[i].s2n    = raw_val_field[i].s2n;
            val_field[i].width  = raw_val_field[i].width;
            val_field[i].height = raw_val_field[i].height;
        }
    }

    if(stagestates[STAGE_RECON] != Idle && stagestates[STAGE_RECON] != Ready)
    {
        surface.resize(raw_surface.size());
        for(int i = 0; i < (int)raw_surface.size(); i++)
            surface[i] = raw_surface[i].array() * scale;
    }
}

 bool Session::IsSaving() const
{
    return save_task.valid() &&
            save_task.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void Session::SaveAsync(const std::string& base_path)
{
    if(IsSaving()) return;

    save_task = std::async(std::launch::async, [this, base_path]()
    {
        if(stagestates[STAGE_PIV]   == Done) SavePIVCSV(base_path + "_piv.csv");
        if(stagestates[STAGE_VAL]   == Done) SaveValCSV(base_path + "_val.csv");
        if(stagestates[STAGE_RECON] == Done) SaveSurfaceCSV(base_path + "_surface.csv");
    });
}

void Session::SavePIVCSV(const std::string& base_path)
{
    for(int i = 0; i < (int)piv_field.size(); i++)
    {
        std::string path = base_path;
        if(piv_field.size() > 1)
        {
            auto dot = base_path.rfind('.');
            path = (dot != std::string::npos)
                ? base_path.substr(0, dot) + "_" + std::to_string(i) + base_path.substr(dot)
                : base_path + "_" + std::to_string(i);
        }
        piv_field[i].SaveCSV(path);
    }
}

void Session::SaveValCSV(const std::string& base_path)
{
    for(int i = 0; i < (int)val_field.size(); i++)
    {
        std::string path = base_path;
        if(val_field.size() > 1)
        {
            auto dot = base_path.rfind('.');
            path = (dot != std::string::npos)
                ? base_path.substr(0, dot) + "_" + std::to_string(i) + base_path.substr(dot)
                : base_path + "_" + std::to_string(i);
        }
        val_field[i].SaveCSV(path);
    }
}

void Session::SaveSurfaceCSV(const std::string& base_path)
{
    for(int i = 0; i < surface.size(); i++)
    {
        //Build filename
        std::string path = base_path;
        if(surface.size() > 1)
        {
            // Insert index before extension
            auto dot = base_path.rfind('.');
            if(dot != std::string::npos)
                path = base_path.substr(0, dot) + "_" + std::to_string(i) + base_path.substr(dot);
            else
                path = base_path + "_" + std::to_string(i);
        }

        std::ofstream file;
        file.open(path);

        if(!file.is_open())
            continue;

        const Eigen::MatrixXf& s = surface[i];
        file << "rows,cols\n";
        file << s.rows() << "," << s.cols() << "\n";

        //Collumn major format
        for(int j = 0; j < s.cols(); j++)
        {
            for(int k = 0; k < s.rows(); k++)
            {
                file << s(k, j) << "\n";
            }
        }
    }
}

const Image& Session::GetRef() const
{
    return ref;
}

const Image& Session::GetFlow() const
{
    return flows[active_index];
}

const std::string& Session::GetRefPath() const
{
    return ref_path;
}

const std::string& Session::GetFlowPath() const
{
    return flow_paths[active_index];
}

void Session::SetActiveIndex(int i)
{
    if(flows.empty()) return;
    active_index = std::clamp(i, 0, (int)flows.size() - 1);
}

int Session::GetActiveIndex() const {return active_index;}

bool Session::HasFlow() const       { return !flows.empty() && flows[0].GetLoaded(); }

int  Session::GetFlowCount() const  { return (int)flows.size(); }

const std::vector<std::string>& Session::GetFlowPaths() const { return flow_paths; }

const VectorField& Session::GetPIVField() const
{
    return piv_field[active_index];
}

const VectorField& Session::GetValField() const
{
    return val_field[active_index];
}

const Eigen::MatrixXf& Session::GetSurface() const
{
    return surface[active_index];
}

StageState Session::GetStageState(Stages s) const
{
    return stagestates[s];
}
