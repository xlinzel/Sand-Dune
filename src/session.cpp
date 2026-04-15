#include <session.h>
#include <algorithm>

namespace
{
float EstimateEtaSeconds(const std::chrono::steady_clock::time_point& start, float progress)
{
    progress = std::clamp(progress, 0.0f, 1.0f);
    if(progress <= 0.0005f || progress >= 1.0f)
        return -1.0f;

    float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - start).count();

    return elapsed / progress * (1.0f - progress);
}
}

Session::Session()
{
    stagestates[STAGE_CORRELATION].store(Idle);
    stagestates[STAGE_VAL].store(Idle);
    stagestates[STAGE_RECON].store(Idle);
    for(int i = 0; i < STAGE_TOTAL; i++)
    {
        stage_progress[i].store(0.0f);
        stage_start[i] = std::chrono::steady_clock::now();
    }
    task_start = std::chrono::steady_clock::now();
    full_task_start = task_start;
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

    if(ref.GetLoaded() && HasFlow())
    {
        stagestates[STAGE_CORRELATION] = Ready;
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
    stagestates[STAGE_CORRELATION] = Idle;
    stagestates[STAGE_VAL]   = Idle;
    stagestates[STAGE_RECON] = Idle;
    raw_correlation_field.clear(); correlation_field.clear();
    raw_val_field.clear(); val_field.clear();
    raw_surface.clear();   surface.clear();
    active_index = 0;

    for(int i = 0; i < static_cast<int>(paths.size()); i++)
        flows[i].Load(paths[i].c_str());

    bool all_loaded = !flows.empty()
                   && std::all_of(flows.begin(), flows.end(), [](const Image& image)
                      {
                          return image.GetLoaded();
                      });

    if(ref.GetLoaded() && all_loaded)
    {
        stagestates[STAGE_CORRELATION] = Ready;
        posx = flows[0].GetWidth()  / 2;
        posy = flows[0].GetHeight() / 2;
    }
}

void Session::RunCorrelation()
{
    if(GetStageState(STAGE_CORRELATION) == Idle)
        return;

    stagestates[STAGE_CORRELATION] = Busy;
    if(GetStageState(STAGE_VAL)   == Done) stagestates[STAGE_VAL]   = Dirty;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    Correlator correlator(correlatorparameters);
    raw_correlation_field.resize(flows.size());

    for(int i = 0; i < (int)flows.size(); i++)
        raw_correlation_field[i] = correlator.Compute(ref.GetMat(), flows[i].GetMat());

    ApplyRefractionCorrection();
    ScaleFields();

    stagestates[STAGE_CORRELATION] = Done;
    stagestates[STAGE_VAL] = Ready;
}

void Session::RunValidation()
{
    if(GetStageState(STAGE_VAL) == Idle)
        return;

    stagestates[STAGE_VAL] = Busy;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    Validation post;
    raw_val_field.resize(raw_correlation_field.size());

    for(int i = 0; i < (int)raw_correlation_field.size(); i++)
    {
        raw_val_field[i] = post.PostProcess(raw_correlation_field[i]);
    }

    ScaleFields();

    stagestates[STAGE_VAL] = Done;
    stagestates[STAGE_RECON] = Ready;
}

void Session::RunReconstruction()
{
    if(GetStageState(STAGE_RECON) == Idle)
        return;

    stagestates[STAGE_RECON] = Busy;

    Reconstruction recon;
    raw_surface.resize(raw_val_field.size());
    surface.resize(raw_val_field.size());

    Eigen::MatrixXf recon_mask = raw_val_field.empty()
                               ? Eigen::MatrixXf()
                               : GetReconstructionMask(raw_val_field[0].width, raw_val_field[0].height);

    for(int i = 0; i < (int)raw_val_field.size(); i++)
        raw_surface[i] = ReconstructField(recon, raw_val_field[i], recon_mask);

    ScaleFields();

    stagestates[STAGE_RECON] = Done;
}

bool Session::IsRunning() const
{
    return activetask.valid() &&
             activetask.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void Session::RunCorrelationAsync()
{
    if(GetStageState(STAGE_CORRELATION) == Idle || IsRunning())
        return;

    stagestates[STAGE_CORRELATION] = Busy;
    if(GetStageState(STAGE_VAL)   == Done) stagestates[STAGE_VAL]   = Dirty;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_CORRELATION] = 0.0f;
    stage_start[STAGE_CORRELATION] = task_start;
    full_progress = 0.0f;
    full_pipeline_running = false;

    activetask = std::async(std::launch::async, [this]()
    {
        Correlator correlator(correlatorparameters);
        int n = (int)flows.size();
        raw_correlation_field.resize(n);

        for(int i = 0; i < n; i++)
        {
            raw_correlation_field[i] = correlator.Compute(ref.GetMat(), flows[i].GetMat(),
                [this, i, n](float p)
                {
                    float stage_p = (i + p) / n;
                    progress = stage_p;
                    stage_progress[STAGE_CORRELATION] = stage_p;
                });
        }

        if(stop_requested) return;

        ApplyRefractionCorrection();
        ScaleFields();

        progress = 1.0f;
        stage_progress[STAGE_CORRELATION] = 1.0f;
        stagestates[STAGE_CORRELATION] = Done;
        stagestates[STAGE_VAL] = Ready;
    });
}

void Session::RunValidationAsync()
{
    if(GetStageState(STAGE_VAL) == Idle || IsRunning())
        return;

    stagestates[STAGE_VAL] = Busy;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_VAL] = 0.0f;
    stage_start[STAGE_VAL] = task_start;
    full_progress = 0.0f;
    full_pipeline_running = false;

    activetask = std::async(std::launch::async, [this]()
    {
        Validation post;
        raw_val_field.resize(raw_correlation_field.size());

        for(int i = 0; i < (int)raw_correlation_field.size(); i++)
        {
            raw_val_field[i] = post.PostProcess(raw_correlation_field[i]);
            float stage_p = (float)(i + 1) / raw_correlation_field.size();
            progress = stage_p;
            stage_progress[STAGE_VAL] = stage_p;
        }

        ScaleFields();

        progress = 1.0f;
        stage_progress[STAGE_VAL] = 1.0f;
        stagestates[STAGE_VAL] = Done;
        stagestates[STAGE_RECON] = Ready;
    });

    return;
}

void Session::RunReconstructionAsync()
{
    if(GetStageState(STAGE_RECON) == Idle || IsRunning())
        return;

    stagestates[STAGE_RECON] = Busy;
    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_RECON] = 0.0f;
    stage_start[STAGE_RECON] = task_start;
    full_progress = 0.0f;
    full_pipeline_running = false;

    activetask = std::async(std::launch::async, [this]()
    {
        Reconstruction recon;
        int n = (int)raw_val_field.size();
        raw_surface.resize(n);
        Eigen::MatrixXf recon_mask = (n > 0)
                                   ? GetReconstructionMask(raw_val_field[0].width, raw_val_field[0].height)
                                   : Eigen::MatrixXf();

        for(int i = 0; i < n; i++)
        {
            raw_surface[i] = ReconstructField(recon, raw_val_field[i], recon_mask);

            float stage_p = (float)(i + 1) / n;
            progress = stage_p;
            stage_progress[STAGE_RECON] = stage_p;
        }

        ScaleFields();

        progress = 1.0f;
        stage_progress[STAGE_RECON] = 1.0f;
        stagestates[STAGE_RECON] = Done;
    });
}

void Session::RunAllAsync()
{
    if(GetStageState(STAGE_CORRELATION) == Idle || IsRunning())
        return;

    stagestates[STAGE_CORRELATION] = Busy;
    stagestates[STAGE_VAL]   = Idle;
    stagestates[STAGE_RECON] = Idle;

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_CORRELATION] = 0.0f;
    stage_progress[STAGE_VAL] = 0.0f;
    stage_progress[STAGE_RECON] = 0.0f;
    stage_start[STAGE_CORRELATION] = task_start;
    full_progress = 0.0f;
    full_task_start = task_start;
    full_pipeline_running = true;

    activetask = std::async(std::launch::async, [this]()
    {
        int n = (int)flows.size();

        // --- Correlation ---
        Correlator correlator(correlatorparameters);
        raw_correlation_field.resize(n);

        for(int i = 0; i < n && !stop_requested; i++)
        {
            raw_correlation_field[i] = correlator.Compute(ref.GetMat(), flows[i].GetMat(),
                [this, i, n](float p)
                {
                    float stage_p = (i + p) / n;
                    progress = stage_p;
                    stage_progress[STAGE_CORRELATION] = stage_p;
                    full_progress = stage_p / 3.0f;
                });
        }

        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }
        ApplyRefractionCorrection();
        ScaleFields();
        stage_progress[STAGE_CORRELATION] = 1.0f;
        full_progress = 1.0f / 3.0f;
        stagestates[STAGE_CORRELATION] = Done;
        stagestates[STAGE_VAL] = Busy;
        task_start = std::chrono::steady_clock::now();
        stage_start[STAGE_VAL] = task_start;
        progress = 0.0f;
        stage_progress[STAGE_VAL] = 0.0f;

        // --- Validation ---
        Validation post;
        raw_val_field.resize(n);
        for(int i = 0; i < n && !stop_requested; i++)
        {
            raw_val_field[i] = post.PostProcess(raw_correlation_field[i]);
            float stage_p = (float)(i + 1) / n;
            progress = stage_p;
            stage_progress[STAGE_VAL] = stage_p;
            full_progress = (1.0f + stage_p) / 3.0f;
        }

        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }
        ScaleFields();
        stage_progress[STAGE_VAL] = 1.0f;
        full_progress = 2.0f / 3.0f;
        stagestates[STAGE_VAL] = Done;
        stagestates[STAGE_RECON] = Busy;

        // --- Reconstruction ---
        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }

        Reconstruction recon;
        raw_surface.resize(n);
        Eigen::MatrixXf recon_mask = (n > 0)
                                   ? GetReconstructionMask(raw_val_field[0].width, raw_val_field[0].height)
                                   : Eigen::MatrixXf();

        task_start = std::chrono::steady_clock::now();
        stage_start[STAGE_RECON] = task_start;
        progress = 0.0f;
        stage_progress[STAGE_RECON] = 0.0f;

        for(int i = 0; i < n && !stop_requested; i++)
        {
            raw_surface[i] = ReconstructField(recon, raw_val_field[i], recon_mask);

            float stage_p = (float)(i + 1) / n;
            progress = stage_p;
            stage_progress[STAGE_RECON] = stage_p;
            full_progress = (2.0f + stage_p) / 3.0f;
        }

        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }
        ScaleFields();

        progress = 1.0f;
        stage_progress[STAGE_RECON] = 1.0f;
        full_progress = 1.0f;
        full_pipeline_running = false;
        stagestates[STAGE_RECON] = Done;
    });
}

void Session::ComputeRefractionCorrection(int h, int w)
{
    float f    = opticalparameters.f    * 1e-3f;
    float Z_a  = opticalparameters.Z_a  * 1e-3f;
    float Z_d  = opticalparameters.Z_d  * 1e-3f;
    float P_px = opticalparameters.P_px * 1e-6f;
    float t    = opticalparameters.t    * 1e-3f;
    float Z_B  = Z_a + Z_d;
    float z_i  = f * Z_B / (Z_B - f);
    int   step = std::max(1, correlatorparameters.window_size - correlatorparameters.overlap);
    float m1   = Z_a / z_i;

    correction[0] = Eigen::MatrixXf::Zero(h, w);
    correction[1] = Eigen::MatrixXf::Zero(h, w);

    for(int row = 0; row < h; row++)
    {
        for(int col = 0; col < w; col++)
        {
            float sx = (col - w / 2.0f) * step * P_px;
            float sy = (row - h / 2.0f) * step * P_px;

            float rx = sx * m1;
            float ry = sy * m1;

            float theta_x = atanf(rx / Z_a);
            float theta_y = atanf(ry / Z_a);

            float thetar_x = asinf(sinf(theta_x) / opticalparameters.n);
            float thetar_y = asinf(sinf(theta_y) / opticalparameters.n);

            float dx = sinf(theta_x - thetar_x) * t / cosf(thetar_x);
            float dy = sinf(theta_y - thetar_y) * t / cosf(thetar_y);

            float dsx = dx / sinf((std::numbers::pi / 2) - theta_x);
            float dsy = dy / sinf((std::numbers::pi / 2) - theta_y);

            correction[0](row, col) = dsx * z_i / (Z_B * P_px);
            correction[1](row, col) = dsy * z_i / (Z_B * P_px);
        }
    }
}

void Session::ApplyRefractionCorrection()
{
    if(!n_correction || raw_correlation_field.empty()) return;

    ComputeRefractionCorrection(raw_correlation_field[0].height, raw_correlation_field[0].width);

    for(auto& field : raw_correlation_field)
    {
        field.u -= correction[0];
        field.v -= correction[1];
    }
}

Eigen::MatrixXf Session::GetReconstructionMask(int width, int height)
{
    if(mask_apply)
    {
        float step = static_cast<float>(std::max(1, correlatorparameters.window_size - correlatorparameters.overlap));
        mask.GenBinCircleMask(width, height, {posx / step, posy / step}, radius / step);
    }

    if(mask_apply && mask.GetSet())
        return mask.GetMask();

    return Eigen::MatrixXf::Ones(height, width);
}

Eigen::MatrixXf Session::ReconstructField(const Reconstruction& recon,
                                          const VectorField& field,
                                          const Eigen::MatrixXf& recon_mask) const
{
    ReconstructionSolver solver = reconstruction_solver.load();
    if(solver == RECON_FRANKOT_CHELLAPPA)
    {
        if(mask_apply
        && recon_mask.rows() == field.height
        && recon_mask.cols() == field.width)
        {
            VectorField masked = field;
            masked.u = field.u.array() * recon_mask.array();
            masked.v = field.v.array() * recon_mask.array();
            return recon.ComputeFC(masked);
        }

        return recon.ComputeFC(field);
    }

    return recon.Compute(field, recon_mask);
}

void Session::ScaleFields()
{
    // Clamp parameters to physically valid minimums to prevent division by zero.
    float   t    = std::max(opticalparameters.t,    0.001f);
    float   P_px = std::max(opticalparameters.P_px, 0.001f);
    float   Z_d  = std::max(opticalparameters.Z_d,  0.001f);
    float   Z_a  = std::max(opticalparameters.Z_a,  0.001f);
    float   f    = std::max(opticalparameters.f,    0.001f);
    float   n    = std::max(opticalparameters.n,    0.001f);

    // scale converts raw pixel displacement into displayed gradient units
    // (dn/dx or mm/dx). surf_fac is the physical correlation-grid spacing in
    // the sample plane, used to convert the reconstructed grid-integrated
    // surface into final display units.
    float Z_B   = Z_d + Z_a;
    float z_i   = f * Z_B / (Z_B - f);

    float term = b_ref
                    ? t                   // RI mode: divide by thickness
                    : (n - 1.0f);         // thickness mode: divide by (n-1)
    term = std::max(term, 0.001f);

    int step = std::max(1, correlatorparameters.window_size - correlatorparameters.overlap);
    float scale =   P_px * 1e-3 * (Z_B - f) * n
                    / (f * Z_d * term);

    float surf_fac = (float)step * P_px * 1e-3 * Z_a / z_i;

    if(GetStageState(STAGE_CORRELATION) != Idle && GetStageState(STAGE_CORRELATION) != Ready)
    {
        correlation_field.resize(raw_correlation_field.size());
        for(int i = 0; i < (int)raw_correlation_field.size(); i++)
        {
            correlation_field[i].u      = raw_correlation_field[i].u.array() * scale;
            correlation_field[i].v      = raw_correlation_field[i].v.array() * scale;
            correlation_field[i].s2n    = raw_correlation_field[i].s2n;
            correlation_field[i].width  = raw_correlation_field[i].width;
            correlation_field[i].height = raw_correlation_field[i].height;
        }
    }

    if(GetStageState(STAGE_VAL) != Idle && GetStageState(STAGE_VAL) != Ready)
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

    if(GetStageState(STAGE_RECON) != Idle && GetStageState(STAGE_RECON) != Ready)
    {
        surface.resize(raw_surface.size());
        for(int i = 0; i < (int)raw_surface.size(); i++)
            surface[i] = raw_surface[i].array() * scale * surf_fac;
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
        if(GetStageState(STAGE_CORRELATION) == Done) SaveCorrelationCSV(base_path + "_correlation.csv");
        if(GetStageState(STAGE_VAL)   == Done) SaveValCSV(base_path + "_val.csv");
        if(GetStageState(STAGE_RECON) == Done) SaveSurfaceCSV(base_path + "_surface.csv");
    });
}

void Session::SaveCorrelationCSV(const std::string& base_path)
{
    for(int i = 0; i < (int)correlation_field.size(); i++)
    {
        std::string path = base_path;
        if(correlation_field.size() > 1)
        {
            auto dot = base_path.rfind('.');
            path = (dot != std::string::npos)
                ? base_path.substr(0, dot) + "_" + std::to_string(i) + base_path.substr(dot)
                : base_path + "_" + std::to_string(i);
        }
        correlation_field[i].SaveCSV(path);
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

bool Session::HasFlow() const
{
    return !flows.empty()
        && std::all_of(flows.begin(), flows.end(), [](const Image& image)
           {
               return image.GetLoaded();
           });
}

int  Session::GetFlowCount() const  { return (int)flows.size(); }

const std::vector<std::string>& Session::GetFlowPaths() const { return flow_paths; }

const VectorField& Session::GetCorrelationField() const
{
    return correlation_field[active_index];
}

const VectorField& Session::GetRawCorrelationField() const
{
    return raw_correlation_field[active_index];
}

const VectorField& Session::GetValField() const
{
    return val_field[active_index];
}

const VectorField& Session::GetRawValField() const
{
    return raw_val_field[active_index];
}

const Eigen::MatrixXf& Session::GetSurface() const
{
    return surface[active_index];
}

float Session::GetStageProgress(Stages s) const
{
    return std::clamp(stage_progress[s].load(), 0.0f, 1.0f);
}

float Session::GetStageEtaSeconds(Stages s) const
{
    if(GetStageState(s) != Busy)
        return -1.0f;

    return EstimateEtaSeconds(stage_start[s], GetStageProgress(s));
}

float Session::GetFullProgress() const
{
    return std::clamp(full_progress.load(), 0.0f, 1.0f);
}

float Session::GetFullEtaSeconds() const
{
    if(!full_pipeline_running.load())
        return -1.0f;

    return EstimateEtaSeconds(full_task_start, GetFullProgress());
}

bool Session::IsRunningFullPipeline() const
{
    return full_pipeline_running.load();
}

void Session::SetReconstructionSolver(ReconstructionSolver solver)
{
    ReconstructionSolver current = reconstruction_solver.load();
    if(current == solver)
        return;

    reconstruction_solver.store(solver);

    if(GetStageState(STAGE_RECON) == Done)
        stagestates[STAGE_RECON] = Dirty;
}

ReconstructionSolver Session::GetReconstructionSolver() const
{
    return reconstruction_solver.load();
}

StageState Session::GetStageState(Stages s) const
{
    return stagestates[s].load();
}
