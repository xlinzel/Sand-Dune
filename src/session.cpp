#include <session.h>
#include <algorithm>
#include <filesystem>

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

int TotalFlowCount(const std::vector<BatchGroup>& groups)
{
    int total = 0;
    for(const auto& group : groups)
        total += static_cast<int>(group.flows.size());
    return total;
}

bool GroupHasLoadedFlows(const BatchGroup& group)
{
    return !group.flows.empty()
        && std::all_of(group.flows.begin(), group.flows.end(), [](const Image& image)
           {
               return image.GetLoaded();
           });
}

bool AllGroupsReadyForCorrelation(const std::vector<BatchGroup>& groups)
{
    if(groups.empty())
        return false;

    return std::all_of(groups.begin(), groups.end(), [](const BatchGroup& group)
    {
        return group.ref.GetLoaded() && GroupHasLoadedFlows(group);
    });
}

std::string BuildFlowNamedPath(const std::string& base_path, const std::string& flow_path,
                               const std::string& stage_suffix, int group_index, int flow_index)
{
    namespace fs = std::filesystem;

    fs::path base(base_path);
    fs::path directory = base.has_filename() ? base.parent_path() : base;

    std::string ui_name = base.has_filename()
        ? (base.has_extension() ? base.stem().string() : base.filename().string())
        : std::string{};

    std::string flow_name = fs::path(flow_path).stem().string();
    if(flow_name.empty())
        flow_name = "flow";

    std::string filename = ui_name.empty()
        ? flow_name
        : ui_name + "_" + flow_name;

    filename += "_g" + std::to_string(group_index);
    filename += "_f" + std::to_string(flow_index);
    filename += "_" + stage_suffix + ".csv";

    return (directory / filename).string();
}
}

Session::Session()
{
    groups.resize(1);
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
    if(groups.empty())
        groups.resize(1);

    BatchGroup& group = groups[active_group_index];
    group.ref_path = path;
    group.ref.Load(path.c_str());
    group.raw_correlation_field.clear();
    group.correlation_field.clear();
    group.raw_val_field.clear();
    group.val_field.clear();
    group.raw_surface.clear();
    group.surface.clear();

    if(AllGroupsReadyForCorrelation(groups))
        stagestates[STAGE_CORRELATION] = Ready;
    else
        stagestates[STAGE_CORRELATION] = Idle;
    stagestates[STAGE_VAL] = Idle;
    stagestates[STAGE_RECON] = Idle;

    std::lock_guard<std::mutex> lock(params_mutex);
    posx = group.ref.GetWidth() / 2;
    posy = group.ref.GetHeight() / 2;
}

void Session::LoadFlow(const std::vector<std::string>& paths)
{
    if(groups.empty())
        groups.resize(1);

    BatchGroup& group = groups[active_group_index];
    group.flow_paths = paths;
    group.flows.clear();
    group.flows.resize(paths.size());

    // Reset pipeline
    stagestates[STAGE_CORRELATION] = Idle;
    stagestates[STAGE_VAL]   = Idle;
    stagestates[STAGE_RECON] = Idle;
    group.raw_correlation_field.clear(); group.correlation_field.clear();
    group.raw_val_field.clear(); group.val_field.clear();
    group.raw_surface.clear();   group.surface.clear();
    active_flow_index = 0;

    for(int i = 0; i < static_cast<int>(paths.size()); i++)
        group.flows[i].Load(paths[i].c_str());

    if(AllGroupsReadyForCorrelation(groups))
        stagestates[STAGE_CORRELATION] = Ready;
    else
        stagestates[STAGE_CORRELATION] = Idle;
    if(group.ref.GetLoaded() && GroupHasLoadedFlows(group))
    {
        std::lock_guard<std::mutex> lock(params_mutex);
        posx = group.flows[0].GetWidth()  / 2;
        posy = group.flows[0].GetHeight() / 2;
    }
}

void Session::AddGroup()
{
    groups.emplace_back();
    active_group_index = static_cast<int>(groups.size()) - 1;
    active_flow_index = 0;
    stagestates[STAGE_CORRELATION] = Idle;
    stagestates[STAGE_VAL] = Idle;
    stagestates[STAGE_RECON] = Idle;
}

void Session::DeleteActiveGroup()
{
    if(groups.empty())
    {
        groups.emplace_back();
        active_group_index = 0;
        active_flow_index = 0;
    }
    else if(groups.size() == 1)
    {
        groups[0] = BatchGroup{};
        active_group_index = 0;
        active_flow_index = 0;
    }
    else
    {
        groups.erase(groups.begin() + active_group_index);
        active_group_index = std::clamp(active_group_index, 0, static_cast<int>(groups.size()) - 1);
        const auto& group = groups[active_group_index];
        active_flow_index = group.flows.empty()
            ? 0
            : std::clamp(active_flow_index, 0, static_cast<int>(group.flows.size()) - 1);
    }

    stagestates[STAGE_CORRELATION] = AllGroupsReadyForCorrelation(groups) ? Ready : Idle;
    stagestates[STAGE_VAL] = Idle;
    stagestates[STAGE_RECON] = Idle;

    std::lock_guard<std::mutex> lock(params_mutex);
    if(!groups.empty() && groups[active_group_index].ref.GetLoaded())
    {
        posx = groups[active_group_index].ref.GetWidth() / 2;
        posy = groups[active_group_index].ref.GetHeight() / 2;
    }
    else
    {
        posx = 0;
        posy = 0;
    }
}

void Session::RunCorrelation()
{
    if(GetStageState(STAGE_CORRELATION) == Idle)
        return;

    const auto params = GetParamsSnapshot();

    stagestates[STAGE_CORRELATION] = Busy;
    if(GetStageState(STAGE_VAL)   == Done) stagestates[STAGE_VAL]   = Dirty;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    Correlator correlator(params.correlatorparameters);
    for(auto& group : groups)
    {
        group.raw_correlation_field.resize(group.flows.size());
        for(int i = 0; i < static_cast<int>(group.flows.size()); i++)
            group.raw_correlation_field[i] = correlator.Compute(group.ref.GetMat(), group.flows[i].GetMat());
    }

    ScaleFields(params);

    stagestates[STAGE_CORRELATION] = Done;
    stagestates[STAGE_VAL] = Ready;
}

void Session::RunValidation()
{
    if(GetStageState(STAGE_VAL) == Idle)
        return;

    const auto params = GetParamsSnapshot();

    stagestates[STAGE_VAL] = Busy;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    Validation post;
    for(auto& group : groups)
    {
        group.raw_val_field.resize(group.raw_correlation_field.size());
        for(int i = 0; i < static_cast<int>(group.raw_correlation_field.size()); i++)
            group.raw_val_field[i] = post.PostProcess(group.raw_correlation_field[i]);
    }

    ScaleFields(params);

    stagestates[STAGE_VAL] = Done;
    stagestates[STAGE_RECON] = Ready;
}

void Session::RunReconstruction()
{
    if(GetStageState(STAGE_RECON) == Idle)
        return;

    const auto params = GetParamsSnapshot();

    stagestates[STAGE_RECON] = Busy;

    Reconstruction recon;
    for(auto& group : groups)
    {
        group.raw_surface.resize(group.raw_val_field.size());
        group.surface.resize(group.raw_val_field.size());

        Eigen::MatrixXf recon_mask = group.raw_val_field.empty()
                                   ? Eigen::MatrixXf()
                                   : GetReconstructionMask(params, group.raw_val_field[0].width, group.raw_val_field[0].height);

        for(int i = 0; i < static_cast<int>(group.raw_val_field.size()); i++)
        {
            group.raw_surface[i] = ReconstructField(recon, group.raw_val_field[i], recon_mask, params,
                                                    group.ref.GetWidth(), group.ref.GetHeight());
        }
    }

    ScaleFields(params);

    stagestates[STAGE_RECON] = Done;
}

bool Session::IsRunning() const
{
    return activetask.valid() &&
             activetask.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

Session::ParamsSnapshot Session::GetParamsSnapshot() const
{
    std::lock_guard<std::mutex> lock(params_mutex);
    return {
        correlatorparameters,
        opticalparameters,
        posx,
        posy,
        radius,
        a,
        mask_apply,
        n_correction,
        b_ref,
        raw_displacements
    };
}

void Session::SetParamsSnapshot(const ParamsSnapshot& params)
{
    std::lock_guard<std::mutex> lock(params_mutex);
    correlatorparameters = params.correlatorparameters;
    opticalparameters = params.opticalparameters;
    posx = params.posx;
    posy = params.posy;
    radius = params.radius;
    a = params.a;
    mask_apply = params.mask_apply;
    n_correction = params.n_correction;
    b_ref = params.b_ref;
    raw_displacements = params.raw_displacements;
}

void Session::RunCorrelationAsync()
{
    if(GetStageState(STAGE_CORRELATION) == Idle || IsRunning())
        return;

    const auto params = GetParamsSnapshot();

    stagestates[STAGE_CORRELATION] = Busy;
    if(GetStageState(STAGE_VAL)   == Done) stagestates[STAGE_VAL]   = Dirty;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_CORRELATION] = 0.0f;
    stage_start[STAGE_CORRELATION] = task_start;
    full_progress = 0.0f;
    full_pipeline_running = false;

    activetask = std::async(std::launch::async, [this, params]()
    {
        Correlator correlator(params.correlatorparameters);
        int total = std::max(1, TotalFlowCount(groups));
        int completed = 0;

        for(auto& group : groups)
        {
            group.raw_correlation_field.resize(group.flows.size());
            for(int i = 0; i < static_cast<int>(group.flows.size()); i++)
            {
                int completed_before = completed;
                group.raw_correlation_field[i] = correlator.Compute(group.ref.GetMat(), group.flows[i].GetMat(),
                [this, completed_before, total](float p)
                {
                    float stage_p = (completed_before + p) / total;
                    progress = stage_p;
                    stage_progress[STAGE_CORRELATION] = stage_p;
                });
                completed++;
            }
        }

        if(stop_requested) return;

        ScaleFields(params);

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

    const auto params = GetParamsSnapshot();

    stagestates[STAGE_VAL] = Busy;
    if(GetStageState(STAGE_RECON) == Done) stagestates[STAGE_RECON] = Dirty;

    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_VAL] = 0.0f;
    stage_start[STAGE_VAL] = task_start;
    full_progress = 0.0f;
    full_pipeline_running = false;

    activetask = std::async(std::launch::async, [this, params]()
    {
        Validation post;
        int total = std::max(1, TotalFlowCount(groups));
        int completed = 0;

        for(auto& group : groups)
        {
            group.raw_val_field.resize(group.raw_correlation_field.size());
            for(int i = 0; i < static_cast<int>(group.raw_correlation_field.size()); i++)
            {
                group.raw_val_field[i] = post.PostProcess(group.raw_correlation_field[i]);
                completed++;
                float stage_p = static_cast<float>(completed) / total;
                progress = stage_p;
                stage_progress[STAGE_VAL] = stage_p;
            }
        }

        ScaleFields(params);

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

    const auto params = GetParamsSnapshot();

    stagestates[STAGE_RECON] = Busy;
    progress = 0.0f;
    task_start = std::chrono::steady_clock::now();
    stage_progress[STAGE_RECON] = 0.0f;
    stage_start[STAGE_RECON] = task_start;
    full_progress = 0.0f;
    full_pipeline_running = false;

    activetask = std::async(std::launch::async, [this, params]()
    {
        Reconstruction recon;
        int total = std::max(1, TotalFlowCount(groups));
        int completed = 0;

        for(auto& group : groups)
        {
            int n = static_cast<int>(group.raw_val_field.size());
            group.raw_surface.resize(n);
            Eigen::MatrixXf recon_mask = (n > 0)
                                       ? GetReconstructionMask(params, group.raw_val_field[0].width, group.raw_val_field[0].height)
                                       : Eigen::MatrixXf();

            for(int i = 0; i < n; i++)
            {
                group.raw_surface[i] = ReconstructField(recon, group.raw_val_field[i], recon_mask, params,
                                                        group.ref.GetWidth(), group.ref.GetHeight());

                completed++;
                float stage_p = static_cast<float>(completed) / total;
                progress = stage_p;
                stage_progress[STAGE_RECON] = stage_p;
            }
        }

        ScaleFields(params);

        progress = 1.0f;
        stage_progress[STAGE_RECON] = 1.0f;
        stagestates[STAGE_RECON] = Done;
    });
}

void Session::RunAllAsync()
{
    if(GetStageState(STAGE_CORRELATION) == Idle || IsRunning())
        return;

    const auto params = GetParamsSnapshot();

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

    activetask = std::async(std::launch::async, [this, params]()
    {
        int total = std::max(1, TotalFlowCount(groups));
        int completed = 0;

        // --- Correlation ---
        Correlator correlator(params.correlatorparameters);
        for(auto& group : groups)
        {
            group.raw_correlation_field.resize(group.flows.size());
            for(int i = 0; i < static_cast<int>(group.flows.size()) && !stop_requested; i++)
            {
                int completed_before = completed;
                group.raw_correlation_field[i] = correlator.Compute(group.ref.GetMat(), group.flows[i].GetMat(),
                [this, completed_before, total](float p)
                {
                    float stage_p = (completed_before + p) / total;
                    progress = stage_p;
                    stage_progress[STAGE_CORRELATION] = stage_p;
                    full_progress = stage_p / 2.05f;
                });
                completed++;
            }
        }

        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }
        ScaleFields(params);
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
        completed = 0;
        for(auto& group : groups)
        {
            group.raw_val_field.resize(group.raw_correlation_field.size());
            for(int i = 0; i < static_cast<int>(group.raw_correlation_field.size()) && !stop_requested; i++)
            {
                group.raw_val_field[i] = post.PostProcess(group.raw_correlation_field[i]);
                completed++;
                float stage_p = static_cast<float>(completed) / total;
                progress = stage_p;
                stage_progress[STAGE_VAL] = stage_p;
                full_progress = (1.0f + stage_p * 0.05f) / 2.05f;
            }
        }

        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }
        ScaleFields(params);
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
        completed = 0;

        task_start = std::chrono::steady_clock::now();
        stage_start[STAGE_RECON] = task_start;
        progress = 0.0f;
        stage_progress[STAGE_RECON] = 0.0f;

        for(auto& group : groups)
        {
            int n = static_cast<int>(group.raw_val_field.size());
            group.raw_surface.resize(n);
            Eigen::MatrixXf recon_mask = (n > 0)
                                       ? GetReconstructionMask(params, group.raw_val_field[0].width, group.raw_val_field[0].height)
                                       : Eigen::MatrixXf();

            for(int i = 0; i < n && !stop_requested; i++)
            {
                group.raw_surface[i] = ReconstructField(recon, group.raw_val_field[i], recon_mask, params,
                                                        group.ref.GetWidth(), group.ref.GetHeight());

                completed++;
                float stage_p = static_cast<float>(completed) / total;
                progress = stage_p;
                stage_progress[STAGE_RECON] = stage_p;
                full_progress = (1.05f + stage_p) / 2.05f;
            }
        }

        if(stop_requested)
        {
            full_pipeline_running = false;
            return;
        }
        ScaleFields(params);

        progress = 1.0f;
        stage_progress[STAGE_RECON] = 1.0f;
        full_progress = 1.0f;
        full_pipeline_running = false;
        stagestates[STAGE_RECON] = Done;
    });
}

void Session::ComputeRefractionCorrection(const ParamsSnapshot& params, int ref_width, int ref_height, int h, int w)
{
    float f    = params.opticalparameters.f    * 1e-3f;
    float Z_a  = params.opticalparameters.Z_a  * 1e-3f;
    float Z_d  = params.opticalparameters.Z_d  * 1e-3f;
    float P_px = params.opticalparameters.P_px * 1e-6f;
    float t    = params.opticalparameters.t    * 1e-3f;
    float Z_B  = Z_a + Z_d;
    float z_i  = f * Z_B / (Z_B - f);

    int   step = std::max(1, params.correlatorparameters.window_size - params.correlatorparameters.overlap);
    float m1   = Z_a / z_i;

    float img_cx = 0.5f * (ref_width  - 1.0f);
    float img_cy = 0.5f * (ref_height - 1.0f);

    float win_center = 0.5f * (params.correlatorparameters.window_size - 1.0f);

    correction[0] = Eigen::MatrixXf::Zero(h, w);
    correction[1] = Eigen::MatrixXf::Zero(h, w);

    for(int row = 0; row < h; row++)
    {
        for(int col = 0; col < w; col++)
        {
            float sx = (col * step + win_center - img_cx) * P_px;
            float sy = (row * step + win_center - img_cy) * P_px;

            float rx = sx * m1;
            float ry = sy * m1;

            float r = std::hypot(rx, ry);

            float theta = atan2(r, Z_a);

            float thetar = asin(std::sin(theta) / params.opticalparameters.n);

            float d = std::sin(theta - thetar) * t / cosf(thetar);

            float ds = d / std::cos(theta);

            float dsx = (r > 0.0f) ? ds * (rx / r) : 0.0f;
            float dsy = (r > 0.0f) ? ds * (ry / r) : 0.0f;

            correction[0](row, col) = dsx * z_i / (Z_B * P_px);
            correction[1](row, col) = dsy * z_i / (Z_B * P_px);
        }
    }
}

void Session::ApplyRefractionCorrection(const ParamsSnapshot& params, BatchGroup& group)
{
    if(!params.n_correction || group.correlation_field.empty()) return;

    ComputeRefractionCorrection(params, group.ref.GetWidth(), group.ref.GetHeight(),
                                group.correlation_field[0].height, group.correlation_field[0].width);

    if(GetStageState(STAGE_CORRELATION) != Idle && GetStageState(STAGE_CORRELATION) != Ready)
    {
        for(auto& field : group.correlation_field)
        {
            field.u -= correction[0];
            field.v -= correction[1];
            field.CalcMag();
        }
    }

    if(GetStageState(STAGE_VAL) != Idle && GetStageState(STAGE_VAL) != Ready)
    {
        for(auto& field : group.val_field)
        {
            field.u -= correction[0];
            field.v -= correction[1];
            field.CalcMag();
        }
    }
}

Eigen::MatrixXf Session::GetReconstructionMask(const ParamsSnapshot& params, int width, int height)
{
    if(params.mask_apply)
    {
        float step = static_cast<float>(std::max(1, params.correlatorparameters.window_size - params.correlatorparameters.overlap));
        mask.GenBinCircleMask(width, height, {params.posx / step, params.posy / step}, params.radius / step);
    }

    if(params.mask_apply && mask.GetSet())
        return mask.GetMask();

    return Eigen::MatrixXf::Ones(height, width);
}

Eigen::MatrixXf Session::ReconstructField(const Reconstruction& recon,
                                          const VectorField& field,
                                          const Eigen::MatrixXf& recon_mask,
                                          const ParamsSnapshot& params,
                                          int ref_width, int ref_height)
{
    VectorField p_field = field;

    if(params.n_correction)
    {
        ComputeRefractionCorrection(params, ref_width, ref_height, p_field.height, p_field.width);
        p_field.u -= correction[0];
        p_field.v -= correction[1];
        p_field.CalcMag();
    }

    ReconstructionSolver solver = reconstruction_solver.load();
    if(solver == RECON_FRANKOT_CHELLAPPA)
    {
        if(params.mask_apply
        && recon_mask.rows() == p_field.height
        && recon_mask.cols() == p_field.width)
        {
            VectorField masked = p_field;
            masked.u = p_field.u.array() * recon_mask.array();
            masked.v = p_field.v.array() * recon_mask.array();
            return recon.ComputeFC(masked);
        }

        return recon.ComputeFC(p_field);
    }

    return recon.Compute(p_field, recon_mask);
}

void Session::ScaleFields()
{
    ScaleFields(GetParamsSnapshot());
}

void Session::ScaleFields(const ParamsSnapshot& params)
{
    // Clamp parameters to physically valid minimums to prevent division by zero.
    float   t    = std::max(params.opticalparameters.t,    0.001f);
    float   P_px = std::max(params.opticalparameters.P_px, 0.001f);
    float   Z_d  = std::max(params.opticalparameters.Z_d,  0.001f);
    float   Z_a  = std::max(params.opticalparameters.Z_a,  0.001f);
    float   f    = std::max(params.opticalparameters.f,    0.001f);
    float   n    = std::max(params.opticalparameters.n,    0.001f);

    // scale converts raw pixel displacement into displayed gradient units
    // (dn/dx or mm/dx). surf_fac is the physical correlation-grid spacing in
    // the sample plane, used to convert the reconstructed grid-integrated
    // surface into final display units.
    float Z_B   = Z_d + Z_a;
    float z_i   = f * Z_B / (Z_B - f);

    float term = params.b_ref
                    ? t                   // RI mode: divide by thickness
                    : (n - 1.0f);         // thickness mode: divide by (n-1)
    term = std::max(term, 0.001f);

    int step = std::max(1, params.correlatorparameters.window_size - params.correlatorparameters.overlap);
    float scale =   P_px * 1e-3 * (Z_B - f)
                    / (f * Z_d * term);

    float surf_fac = (float)step * P_px * 1e-3 * Z_a / z_i;

    if(GetStageState(STAGE_CORRELATION) != Idle && GetStageState(STAGE_CORRELATION) != Ready)
    {
        for(auto& group : groups)
        {
            group.correlation_field.resize(group.raw_correlation_field.size());
            for(int i = 0; i < static_cast<int>(group.raw_correlation_field.size()); i++)
            {
                group.correlation_field[i].u      = group.raw_correlation_field[i].u.array();
                group.correlation_field[i].v      = group.raw_correlation_field[i].v.array();
                group.correlation_field[i].mag    = group.raw_correlation_field[i].mag.array();
                group.correlation_field[i].s2n    = group.raw_correlation_field[i].s2n;
                group.correlation_field[i].width  = group.raw_correlation_field[i].width;
                group.correlation_field[i].height = group.raw_correlation_field[i].height;
            }
        }
    }

    if(GetStageState(STAGE_VAL) != Idle && GetStageState(STAGE_VAL) != Ready)
    {
        for(auto& group : groups)
        {
            group.val_field.resize(group.raw_val_field.size());
            for(int i = 0; i < static_cast<int>(group.raw_val_field.size()); i++)
            {
                group.val_field[i].u      = group.raw_val_field[i].u.array();
                group.val_field[i].v      = group.raw_val_field[i].v.array();
                group.val_field[i].mag    = group.raw_val_field[i].mag.array();
                group.val_field[i].s2n    = group.raw_val_field[i].s2n;
                group.val_field[i].width  = group.raw_val_field[i].width;
                group.val_field[i].height = group.raw_val_field[i].height;
            }
        }
    }

    if(GetStageState(STAGE_RECON) != Idle && GetStageState(STAGE_RECON) != Ready)
    {
        for(auto& group : groups)
        {
            group.surface.resize(group.raw_surface.size());
            for(int i = 0; i < static_cast<int>(group.raw_surface.size()); i++)
                group.surface[i] = group.raw_surface[i].array() * scale * surf_fac;
        }
    }

    //Refraction correction is in pixel units, do before scaling
    if(params.n_correction)
    {
        for(auto& group : groups)
            ApplyRefractionCorrection(params, group);
    }

    if(!params.raw_displacements)
    {
        if(GetStageState(STAGE_CORRELATION) != Idle && GetStageState(STAGE_CORRELATION) != Ready)
        {
            for(auto& group : groups)
            {
                for(auto& field : group.correlation_field)
                {
                    field.u   *= scale;
                    field.v   *= scale;
                    field.mag *= scale;
                }
            }
        }

        if(GetStageState(STAGE_VAL) != Idle && GetStageState(STAGE_VAL) != Ready)
        {
            for(auto& group : groups)
            {
                for(auto& field : group.val_field)
                {
                    field.u   *= scale;
                    field.v   *= scale;
                    field.mag *= scale;
                }
            }
        }
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
        if(GetStageState(STAGE_CORRELATION) == Done) SaveCorrelationCSV(base_path);
        if(GetStageState(STAGE_VAL)   == Done) SaveValCSV(base_path);
        if(GetStageState(STAGE_RECON) == Done) SaveSurfaceCSV(base_path);
    });
}

void Session::SaveCorrelationCSV(const std::string& base_path)
{
    for(int g = 0; g < static_cast<int>(groups.size()); g++)
    {
        const auto& group = groups[g];
        for(int i = 0; i < static_cast<int>(group.correlation_field.size()); i++)
        {
            std::string path = BuildFlowNamedPath(base_path, group.flow_paths[i], "correlation", g, i);
            group.correlation_field[i].SaveCSV(path);
        }
    }
}

void Session::SaveValCSV(const std::string& base_path)
{
    for(int g = 0; g < static_cast<int>(groups.size()); g++)
    {
        const auto& group = groups[g];
        for(int i = 0; i < static_cast<int>(group.val_field.size()); i++)
        {
            std::string path = BuildFlowNamedPath(base_path, group.flow_paths[i], "val", g, i);
            group.val_field[i].SaveCSV(path);
        }
    }
}

void Session::SaveSurfaceCSV(const std::string& base_path)
{
    for(int g = 0; g < static_cast<int>(groups.size()); g++)
    {
        const auto& group = groups[g];
        for(int i = 0; i < static_cast<int>(group.surface.size()); i++)
        {
            std::string path = BuildFlowNamedPath(base_path, group.flow_paths[i], "surface", g, i);

            std::ofstream file;
            file.open(path);

            if(!file.is_open())
                continue;

            const Eigen::MatrixXf& s = group.surface[i];
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
}

const Image& Session::GetRef() const
{
    return groups[active_group_index].ref;
}

const Image& Session::GetFlow() const
{
    return groups[active_group_index].flows[active_flow_index];
}

const std::string& Session::GetRefPath() const
{
    return groups[active_group_index].ref_path;
}

const std::string& Session::GetFlowPath() const
{
    return groups[active_group_index].flow_paths[active_flow_index];
}

int Session::GetGroupCount() const
{
    return static_cast<int>(groups.size());
}

int Session::GetActiveGroupIndex() const
{
    return active_group_index;
}

int Session::GetActiveFlowIndex() const
{
    return active_flow_index;
}

void Session::SetActiveGroupIndex(int i)
{
    if(groups.empty())
    {
        active_group_index = 0;
        active_flow_index = 0;
        return;
    }

    active_group_index = std::clamp(i, 0, static_cast<int>(groups.size()) - 1);

    const auto& group = groups[active_group_index];
    if(group.flows.empty())
        active_flow_index = 0;
    else
        active_flow_index = std::clamp(active_flow_index, 0, static_cast<int>(group.flows.size()) - 1);
}

void Session::SetActiveFlowIndex(int i)
{
    if(groups.empty())
    {
        active_flow_index = 0;
        return;
    }

    const auto& group = groups[active_group_index];
    if(group.flows.empty())
    {
        active_flow_index = 0;
        return;
    }

    active_flow_index = std::clamp(i, 0, static_cast<int>(group.flows.size()) - 1);
}

void Session::SetActiveSelection(int group_i, int flow_i)
{
    if(groups.empty())
    {
        active_group_index = 0;
        active_flow_index = 0;
        return;
    }

    active_group_index = std::clamp(group_i, 0, static_cast<int>(groups.size()) - 1);

    const auto& group = groups[active_group_index];
    if(group.flows.empty())
        active_flow_index = 0;
    else
        active_flow_index = std::clamp(flow_i, 0, static_cast<int>(group.flows.size()) - 1);
}

void Session::SetActiveIndex(int i)
{
    SetActiveFlowIndex(i);
}

int Session::GetActiveIndex() const {return active_flow_index;}

bool Session::HasFlow() const
{
    if(groups.empty())
        return false;

    return GroupHasLoadedFlows(groups[active_group_index]);
}

int Session::GetFlowCount() const
{
    if(groups.empty())
        return 0;

    return static_cast<int>(groups[active_group_index].flows.size());
}

const std::vector<std::string>& Session::GetFlowPaths() const
{
    static const std::vector<std::string> empty_paths;
    if(groups.empty())
        return empty_paths;

    return groups[active_group_index].flow_paths;
}

const VectorField& Session::GetCorrelationField() const
{
    return groups[active_group_index].correlation_field[active_flow_index];
}

const VectorField& Session::GetRawCorrelationField() const
{
    return groups[active_group_index].raw_correlation_field[active_flow_index];
}

const VectorField& Session::GetValField() const
{
    return groups[active_group_index].val_field[active_flow_index];
}

const VectorField& Session::GetRawValField() const
{
    return groups[active_group_index].raw_val_field[active_flow_index];
}

const Eigen::MatrixXf& Session::GetSurface() const
{
    return groups[active_group_index].surface[active_flow_index];
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
