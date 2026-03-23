#include <water/ui.h>

UI::UI(Session& session, SDL_Renderer* renderer)
    : session(session), renderer(renderer)
{
    piv_textures.resize(3);
}

void UI::SetRenderer(SDL_Renderer* renderer)
{
    this->renderer = renderer;
    
    surf = SDL_LoadPNG((std::string(PROJECT_DIR) + "/rsc/background.png").c_str());
    texture_bg = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_SetTextureBlendMode(texture_bg, SDL_BLENDMODE_BLEND);
    SDL_DestroySurface(surf);
}

void UI::Draw()
{
    //Start dockign window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##dockspace", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBackground);
    ImGui::DockSpace(ImGui::GetID("MainDock"), ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(3);

    //Set background
    ImGui::GetBackgroundDrawList()->AddImage(
        (ImTextureID)texture_bg,
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.35f, ImGui::GetIO().DisplaySize.y * 0.35f),
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.65f, ImGui::GetIO().DisplaySize.y * 0.65f),
        ImVec2(0, 0), ImVec2(1, 1),
        IM_COL32_WHITE);
    
    DrawLoadPanel();

    DrawParametersPanel();

    DrawCalculationsPanel();

    DrawPipelinePanel();

    DrawFlowPanel();
    DrawRefPanel();

    DrawSettingsPanel();

    DrawVisualizationPanel();
}

void UI::DrawLoadPanel()
{
    if(!pending_ref_path.empty())
    {
        session.LoadRef(pending_ref_path);

        surf = SDL_LoadBMP(pending_ref_path.c_str());
        if(ref_tex) SDL_DestroyTexture(ref_tex);
        ref_tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);

        pending_ref_path.clear();
        file_dialog_open = false;
    }
    
    if(!pending_flow_path.empty())
    {
        session.LoadFlow(pending_flow_path);

        surf = SDL_LoadBMP(pending_flow_path.c_str());
        if(flow_tex) SDL_DestroyTexture(flow_tex);
        flow_tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);

        pending_flow_path.clear();
        file_dialog_open = false;
    }

    //Locked Positions
    /*ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(300.0f, io.DisplaySize.x / 6), std::max(200.0f, io.DisplaySize.y / 3)), ImGuiCond_Always);
    ImGui::Begin("Load Images", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);*/

    ImGui::Begin("Load Images", nullptr);

    ImGui::SeparatorText("Reference Image");

    if(session.GetRef().GetLoaded())
        ImGui::TextColored(ImVec4(0,1,0,1), "[OK]");
    else
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "[!]");

    ImGui::SameLine();
    if(session.GetRef().GetLoaded())
        ImGui::TextWrapped("%s", session.GetRefPath().c_str());
    else
        ImGui::TextDisabled("No file loaded");

    bool was_open = file_dialog_open;
    if(was_open) ImGui::BeginDisabled();

    if(ImGui::Button("Browse##ref"))
    {
        file_dialog_open = true;
        SDL_DialogFileFilter filters[] = {{"BMP", "bmp"}, {"PNG", "png"}, {"All File", "*"}};
        SDL_ShowOpenFileDialog(OnRefSelected, this, nullptr, filters, 2, nullptr, false);
    }

    if(was_open) ImGui::EndDisabled();

    ImGui::SeparatorText("Flow Image");

    if(session.GetFlow().GetLoaded())
        ImGui::TextColored(ImVec4(0,1,0,1), "[OK]");
    else
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "[!]");
        
    ImGui::SameLine();
    if(session.GetFlow().GetLoaded())
        ImGui::TextWrapped("%s", session.GetFlowPath().c_str());
    else
        ImGui::TextDisabled("No file loaded");

    if(was_open) ImGui::BeginDisabled();

    if(ImGui::Button("Browse##flow"))
    {
        file_dialog_open = true;
        SDL_DialogFileFilter filters[] = {{"BMP", "bmp"}, {"PNG", "png"}, {"All File", "*"}};
        SDL_ShowOpenFileDialog(OnFlowSelected, this, nullptr, filters, 2, nullptr, false);
    }

    if(was_open) ImGui::EndDisabled();

    ImGui::End();
}

void UI::DrawParametersPanel()
{
    /*ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, std::max(200.0f, io.DisplaySize.y / 3)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(300.0f, io.DisplaySize.x / 6), std::max(200.0f, io.DisplaySize.y / 3)), ImGuiCond_Always);
    ImGui::Begin("Parameters", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);*/

    ImGui::Begin("Parameters", nullptr);

    ImGui::SeparatorText("PIV Parameters");
    ImGui::PushItemWidth(-100.0f);

    ImGui::SliderInt("Window Size", &session.pivparameters.window_size, 0, 164);
    ImGui::SliderInt("Overlap", &session.pivparameters.overlap, 0, session.pivparameters.window_size - 2);
    ImGui::SliderInt("Search Size", &session.pivparameters.search_size, 0, 196);

    ImGui::PopItemWidth();
    ImGui::SeparatorText("Experimental Parameters");
    ImGui::PushItemWidth(-230.0f);

    bool params_changed = false;
    params_changed |= ImGui::InputFloat("Sample Thickness (mm)", &session.opticalparameters.t, 0.1f);
    params_changed |= ImGui::InputFloat("Sensor Pixel Pitch (um)", &session.opticalparameters.P_px, 0.05f);
    params_changed |= ImGui::InputFloat("Background -> Sample (mm)", &session.opticalparameters.Z_d, 25.0f);
    params_changed |= ImGui::InputFloat("Sample -> Lens (mm)", &session.opticalparameters.Z_a, 25.0f);
    params_changed |= ImGui::InputFloat("Lens Focal Length (mm)", &session.opticalparameters.f, 5.0f);
    ImGui::InputFloat("Aperture Diameter (mm)", &session.opticalparameters.d_a, 1.0f);

    ImGui::PopItemWidth();
    ImGui::SeparatorText("Mask Parameters");
    ImGui::PushItemWidth(-100.0f);

    ImGui::Checkbox("Mask Enabled", &session.mask_apply);

    ImGui::SliderInt("X Position", &session.posx, 0, 5640);
    ImGui::SliderInt("Y Position", &session.posy, 0, 5640);
    ImGui::SliderInt("Radius", &session.radius, 0, 2820);

    ImGui::PopItemWidth();
    ImGui::SeparatorText("Background Pattern");
    ImGui::PushItemWidth(-230.0f);
    ImGui::InputFloat("Dot Diameter (mm)", &session.opticalparameters.d_bg, 0.05f);
    ImGui::PopItemWidth();

    ImGui::End();

    //Scale fields and null textures so they are rebuilt next frame
    if(params_changed)
    {
        session.ScaleFields();
        // Null textures so Draw functions rebuild them next frame
        for(int i = 0; i < 3; i++) { SDL_DestroyTexture(piv_textures[i]); piv_textures[i] = nullptr; }
        for(int i = 0; i < 3; i++) { SDL_DestroyTexture(val_textures[i]); val_textures[i] = nullptr; }
        SDL_DestroyTexture(surf_texture); surf_texture = nullptr;
    }
}

void UI::DrawCalculationsPanel()
{
    ImGui::Begin("Calculations");

    const auto& op = session.opticalparameters;

    const float lambda = 550e-9f;
    float f    = op.f    * 1e-3f;
    float Z_a  = op.Z_a  * 1e-3f;
    float Z_d  = op.Z_d  * 1e-3f;
    float d_a  = op.d_a  * 1e-3f;
    float dot  = op.d_bg * 1e-3f;
    float P_px = op.P_px * 1e-6f;
    float Z_B  = Z_a + Z_d;

    ImVec4 col_g = ImVec4(0.2f, 0.85f, 0.3f,  1.0f);
    ImVec4 col_y = ImVec4(1.0f, 0.85f, 0.1f,  1.0f);
    ImVec4 col_r = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    ImVec4 col_d = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

    // Helper: label on left, colored value on right
    auto Val = [&](const char* label, ImVec4 col, const char* fmt, ...)
    {
        ImGui::TextColored(col_d, "%s", label);
        ImGui::SameLine(180.0f);
        va_list args; va_start(args, fmt);
        char buf[64]; vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
        ImGui::TextColored(col, "%s", buf);
    };

    if(Z_B <= f * 1.001f)
    {
        ImGui::TextColored(col_r, "INVALID: Z_B (%.0f mm) must be > f (%.0f mm)", Z_B * 1e3f, f * 1e3f);
        ImGui::End();
        return;
    }

    float z_i      = f * Z_B / (Z_B - f);
    float M_bg     = z_i / Z_B;
    float M_a      = z_i / Z_a;
    float dphys_px = (dot * M_bg) / P_px;
    float dd_px    = 2.44f * (f / d_a) * (M_bg + 1.0f) * lambda / P_px;
    float deff_px  = std::sqrt(dphys_px * dphys_px + dd_px * dd_px);
    int   win      = 16; while(win < (int)(deff_px * 4.0f)) win *= 2;

    // Physical size of each output grid cell in the sample plane
    int   step     = session.pivparameters.window_size - session.pivparameters.overlap;
    float res_mm   = step * P_px * Z_a / z_i * 1e3f;

    //ImGui::SeparatorText("Inputs Used");
    //ImGui::TextColored(col_d, "f=%.1fmm  Z_a=%.0fmm  Z_d=%.0fmm  d_a=%.1fmm  d_bg=%.2fmm  P_px=%.2fum",
    //    op.f, op.Z_a, op.Z_d, op.d_a, op.d_bg, op.P_px);

    ImGui::SeparatorText("Optics");
    Val("z_i (sensor dist)",   col_g,                                     "%.2f mm",  z_i  * 1e3f);
    Val("M_background",        col_d,                                     "%.5f",     M_bg);
    Val("M_aerogel",           col_d,                                     "%.5f",     M_a);

    ImGui::SeparatorText("Blur");
    Val("d_physical",  dphys_px < 0.5f ? col_r : dphys_px > 5.0f ? col_y : col_g,   "%.2f px",  dphys_px);
    Val("d_diffraction", dd_px > 7.0f  ? col_r : dd_px    > 4.0f ? col_y : col_g,   "%.2f px",  dd_px);
    Val("d_eff",       deff_px < 1.0f || deff_px > 8.0f ? col_r : deff_px > 4.0f ? col_y : col_g, "%.2f px", deff_px);

    ImGui::SeparatorText("PIV");
    Val("Suggested window",    col_g,                                     "%d px",    win);
    Val("Spatial resolution",  res_mm > 5.0f ? col_r : res_mm > 2.0f ? col_y : col_g, "%.2f mm", res_mm);

    ImGui::End();
}

void UI::DrawPipelinePanel()
{
    ImGui::Begin("Pipeline");

    //----------------------------
    //PIV
    //----------------------------
    ImGui::SeparatorText("PIV");
    
    ImGui::Text("Status: ");
    ImGui::SameLine();
    switch (session.GetStageState(STAGE_PIV))
    {
        case Idle:
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "[Idle]"); break;
        case Ready:
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1), "[Ready]"); break;
        case Busy:
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1), "[Busy]"); break;
        case Done:
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1), "[Done]"); break;
        case Dirty:
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1), "[Dirty]"); break;
    }

    bool was_disabled = (session.GetStageState(STAGE_PIV) != Ready && session.GetStageState(STAGE_PIV) != Done && session.GetStageState(STAGE_PIV) != Dirty) || session.IsRunning();
    if(was_disabled)
        ImGui::BeginDisabled();

    if(ImGui::Button("Run PIV"))
    {
        session.RunPIVAsync();
    }
    
    if(was_disabled)
        ImGui::EndDisabled();

    //----------------------------
    //Validation
    //----------------------------
    ImGui::SeparatorText("Validation");
    
    ImGui::Text("Status: ");
    ImGui::SameLine();
    switch (session.GetStageState(STAGE_VAL))
    {
        case Idle:
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "[Idle]"); break;
        case Ready:
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1), "[Ready]"); break;
        case Busy:
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1), "[Busy]"); break;
        case Done:
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1), "[Done]"); break;
        case Dirty:
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1), "[Dirty]"); break;
    }

    was_disabled = (session.GetStageState(STAGE_VAL) != Ready && session.GetStageState(STAGE_VAL) != Done && (session.GetStageState(STAGE_VAL) != Dirty || session.GetStageState(STAGE_PIV) != Done)) || session.IsRunning();
    if(was_disabled)
        ImGui::BeginDisabled();

    if(ImGui::Button("Run Validation"))
    {
        session.RunValidationAsync();
    }
    
    if(was_disabled)
        ImGui::EndDisabled();

    //----------------------------
    //Reconstruction
    //----------------------------
    ImGui::SeparatorText("Reconstruction");
    
    ImGui::Text("Status: ");
    ImGui::SameLine();
    switch (session.GetStageState(STAGE_RECON))
    {
        case Idle:
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "[Idle]"); break;
        case Ready:
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1), "[Ready]"); break;
        case Busy:
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1), "[Busy]"); break;
        case Done:
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1), "[Done]"); break;
        case Dirty:
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1), "[Dirty]"); break;
    }

    was_disabled = (session.GetStageState(STAGE_RECON) != Ready && session.GetStageState(STAGE_RECON) != Done && (session.GetStageState(STAGE_RECON) != Dirty || session.GetStageState(STAGE_VAL) != Done)) || session.IsRunning();
    if(was_disabled)
        ImGui::BeginDisabled();

    if(ImGui::Button("Run Reconstruction"))
    {
        session.RunReconstructionAsync();
    }
    
    if(was_disabled)
        ImGui::EndDisabled();

    ImGui::End();
}

void UI::DrawVisualizationPanel()
{
    // Track state transitions here so Draw* functions see them even when not called during Busy
    static StageState last_piv  = Idle;
    static StageState last_val  = Idle;
    static StageState last_recon = Idle;

    StageState cur_piv   = session.GetStageState(STAGE_PIV);
    StageState cur_val   = session.GetStageState(STAGE_VAL);
    StageState cur_recon = session.GetStageState(STAGE_RECON);

    if(cur_piv == Done && last_piv != Done)
        for(int i = 0; i < 3; i++) { SDL_DestroyTexture(piv_textures[i]); piv_textures[i] = nullptr; }
    if(cur_val == Done && last_val != Done)
        for(int i = 0; i < 3; i++) { SDL_DestroyTexture(val_textures[i]); val_textures[i] = nullptr; }
    if(cur_recon == Done && last_recon != Done)
        { SDL_DestroyTexture(surf_texture); surf_texture = nullptr; }

    last_piv   = cur_piv;
    last_val   = cur_val;
    last_recon = cur_recon;

    if(cur_piv == Done)
        DrawPIV();

    if(cur_val == Done)
        DrawVal();

    if(cur_recon == Done)
        DrawSurf();
}

void UI::RebuildPIVTextures()
{
    const VectorField& field = session.GetPIVField();
    const std::vector<const Eigen::MatrixXf*> data = {&field.u, &field.v, &field.s2n};
    int w = field.width, h = field.height;

    // Build RGBA pixel buffer (temporary — SDL copies it)
    std::vector<uint8_t> pixels(w * h * 4);

    ImPlot::PushColormap(ImPlotColormap_Viridis);
    for(int i = 0; i < 3; i++)
    {
        float range = piv_cmap_max[i] - piv_cmap_min[i];

        for(int r = 0; r < h; r++)
        {
            for(int c = 0; c < w; c++)
            {
                float t = (range != 0.0f) ? ((*data[i])(r, c) - piv_cmap_min[i]) / range : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                ImVec4 col = ImPlot::SampleColormap(t);
                int idx = (r * w + c) * 4;
                pixels[idx+0] = (uint8_t)(col.x * 255);
                pixels[idx+1] = (uint8_t)(col.y * 255);
                pixels[idx+2] = (uint8_t)(col.z * 255);
                pixels[idx+3] = 255;
            }
        }

        // Upload to GPU — nearest-neighbor scaling for hard pixel boundaries
        if(piv_textures[i]) SDL_DestroyTexture(piv_textures[i]);
        piv_textures[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_SetTextureScaleMode(piv_textures[i], SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(piv_textures[i], nullptr, pixels.data(), w * 4);
    }
    ImPlot::PopColormap();
}

void UI::DrawPIV()
{
    ImGui::Begin("PIV Results");

    const VectorField& field = session.GetPIVField();

    // --- Rebuild textures if invalidated or colormap range changed ---
    static float last_min[3] = {0,0,0}, last_max[3] = {0,0,0};
    if(piv_textures[0] == nullptr)
    {
        // Auto-range all three maps on first build
        const Eigen::MatrixXf* maps[3] = {&field.u, &field.v, &field.s2n};
        for(int i = 0; i < 3; i++)
        {
            piv_cmap_min[i] = maps[i]->minCoeff();
            piv_cmap_max[i] = maps[i]->maxCoeff();
        }
    }
    bool dirty = (piv_textures[0] == nullptr);
    for(int i = 0; i < 3; i++)
        dirty |= (piv_cmap_min[i] != last_min[i] || piv_cmap_max[i] != last_max[i]);
    if(dirty)
    {
        RebuildPIVTextures();
        for(int i = 0; i < 3; i++) { last_min[i] = piv_cmap_min[i]; last_max[i] = piv_cmap_max[i]; }
    }

    // --- Physical scale: PIV cell spacing -> mm ---
    float Z_i      = session.opticalparameters.f * (session.opticalparameters.Z_a + session.opticalparameters.Z_d)
                     / (session.opticalparameters.Z_a + session.opticalparameters.Z_d - session.opticalparameters.f);
    float px_to_mm = (session.pivparameters.window_size - session.pivparameters.overlap)
                     * session.opticalparameters.P_px * session.opticalparameters.Z_a / Z_i / 1000.0f;
    float field_w_mm = field.width  * px_to_mm;
    float field_h_mm = field.height * px_to_mm;

    // --- Layout ---
    float scale_w   = 85.0f;
    float controls_h = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
    float avail_w   = ImGui::GetContentRegionAvail().x - scale_w - ImGui::GetStyle().ItemSpacing.x;
    float avail_h   = ImGui::GetContentRegionAvail().y - controls_h;
    float ratio     = (float)field.height / (float)field.width;
    float plot_h    = std::min(avail_h, avail_w * ratio);
    float plot_w    = plot_h / ratio;

    // --- Map selection (mutually exclusive, no rebuild) ---
    ImGui::RadioButton("u",   &piv_map, 0); ImGui::SameLine();
    ImGui::RadioButton("v",   &piv_map, 1); ImGui::SameLine();
    ImGui::RadioButton("s2n", &piv_map, 2);

    // --- Heatmap plot ---
    if(ImPlot::BeginPlot("##pivresults", ImVec2(plot_w, plot_h), ImPlotFlags_Equal))
    {
        ImPlot::SetupAxes("x (mm)", "y (mm)");
        ImPlot::SetupAxesLimits(0, field_w_mm, 0, field_h_mm, ImGuiCond_Once);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, field_w_mm);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, field_h_mm);

        if(ImPlot::IsPlotHovered())
        {
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            int col = (int)(mouse.x / px_to_mm);
            int row = field.height - 1 - (int)(mouse.y / px_to_mm); // flip: plot y is bottom-up, matrix row is top-down
            const Eigen::MatrixXf* maps[3] = {&field.u, &field.v, &field.s2n};
            if(col >= 0 && col < field.width && row >= 0 && row < field.height)
            {
                const char* unit = (piv_map == 0) ? " dn/du" : (piv_map == 1) ? " dn/dv" : "";
                ImGui::SetTooltip("%.4f%s", (*maps[piv_map])(row, col), unit);
            }
        }

        ImPlot::PlotImage("##heatmap", (ImTextureID)piv_textures[piv_map],
                          ImPlotPoint(0, 0), ImPlotPoint(field_w_mm, field_h_mm));
        ImPlot::EndPlot();
    }

    // --- Colormap scale bar ---
    ImGui::SameLine();
    ImPlot::PushColormap(ImPlotColormap_Viridis);
    ImPlot::ColormapScale("##scale", piv_cmap_min[piv_map], piv_cmap_max[piv_map], ImVec2(scale_w, plot_h));
    ImPlot::PopColormap();

    // --- Range controls: Min | Max | Auto (per-map) ---
    float auto_btn_w = ImGui::CalcTextSize("Auto").x + ImGui::GetStyle().FramePadding.x * 2;
    float half_w     = (plot_w - ImGui::GetStyle().ItemSpacing.x * 2 - auto_btn_w) * 0.5f;

    {
        const Eigen::MatrixXf* maps[3] = {&field.u, &field.v, &field.s2n};
        float data_min = maps[piv_map]->minCoeff();
        float data_max = maps[piv_map]->maxCoeff();
        float step = (data_max - data_min) / 200.0f;
        int decimals = std::max(0, (int)std::ceil(-std::log10(step)));
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Min##cmap", &piv_cmap_min[piv_map], data_min, piv_cmap_max[piv_map], fmt);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Max##cmap", &piv_cmap_max[piv_map], piv_cmap_min[piv_map], data_max, fmt);
        ImGui::SameLine();
        if(ImGui::Button("Auto##cmap"))
        {
            piv_cmap_min[piv_map] = data_min;
            piv_cmap_max[piv_map] = data_max;
        }
    }

    ImGui::End();
}

void UI::RebuildValTextures()
{
    const VectorField& field = session.GetValField();
    const std::vector<const Eigen::MatrixXf*> data = {&field.u, &field.v, &field.s2n};
    int w = field.width, h = field.height;

    // Build RGBA pixel buffer (temporary — SDL copies it)
    std::vector<uint8_t> pixels(w * h * 4);

    ImPlot::PushColormap(ImPlotColormap_Viridis);
    for(int i = 0; i < 3; i++)
    {
        float range = val_cmap_max[i] - val_cmap_min[i];

        for(int r = 0; r < h; r++)
        {
            for(int c = 0; c < w; c++)
            {
                float t = (range != 0.0f) ? ((*data[i])(r, c) - val_cmap_min[i]) / range : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                ImVec4 col = ImPlot::SampleColormap(t);
                int idx = (r * w + c) * 4;
                pixels[idx+0] = (uint8_t)(col.x * 255);
                pixels[idx+1] = (uint8_t)(col.y * 255);
                pixels[idx+2] = (uint8_t)(col.z * 255);
                pixels[idx+3] = 255;
            }
        }

        // Upload to GPU — nearest-neighbor scaling for hard pixel boundaries
        if(val_textures[i]) SDL_DestroyTexture(val_textures[i]);
        val_textures[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_SetTextureScaleMode(val_textures[i], SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(val_textures[i], nullptr, pixels.data(), w * 4);
    }
    ImPlot::PopColormap();
}

void UI::DrawVal()
{
    ImGui::Begin("Val Results");

    const VectorField& field = session.GetValField();

    // --- Invalidate textures when Validation reruns ---
    static StageState last_val_state = Idle;
    StageState cur_val_state = session.GetStageState(STAGE_VAL);
    if(cur_val_state == Done && last_val_state != Done)
        for(int i = 0; i < 3; i++) { SDL_DestroyTexture(val_textures[i]); val_textures[i] = nullptr; }
    last_val_state = cur_val_state;

    // --- Rebuild textures if invalidated or colormap range changed ---
    static float last_min[3] = {0,0,0}, last_max[3] = {0,0,0};
    if(val_textures[0] == nullptr)
    {
        // Auto-range all three maps on first build
        const Eigen::MatrixXf* maps[3] = {&field.u, &field.v, &field.s2n};
        for(int i = 0; i < 3; i++)
        {
            val_cmap_min[i] = maps[i]->minCoeff();
            val_cmap_max[i] = maps[i]->maxCoeff();
        }
    }
    bool dirty = (val_textures[0] == nullptr);
    for(int i = 0; i < 3; i++)
        dirty |= (val_cmap_min[i] != last_min[i] || val_cmap_max[i] != last_max[i]);
    if(dirty)
    {
        RebuildValTextures();
        for(int i = 0; i < 3; i++) { last_min[i] = val_cmap_min[i]; last_max[i] = val_cmap_max[i]; }
    }

    // --- Physical scale: Val cell spacing -> mm ---
    float Z_i      = session.opticalparameters.f * (session.opticalparameters.Z_a + session.opticalparameters.Z_d)
                     / (session.opticalparameters.Z_a + session.opticalparameters.Z_d - session.opticalparameters.f);
    float px_to_mm = (session.pivparameters.window_size - session.pivparameters.overlap)
                     * session.opticalparameters.P_px * session.opticalparameters.Z_a / Z_i / 1000.0f;
    float field_w_mm = field.width  * px_to_mm;
    float field_h_mm = field.height * px_to_mm;

    // --- Layout ---
    float scale_w   = 85.0f;
    float controls_h = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
    float avail_w   = ImGui::GetContentRegionAvail().x - scale_w - ImGui::GetStyle().ItemSpacing.x;
    float avail_h   = ImGui::GetContentRegionAvail().y - controls_h;
    float ratio     = (float)field.height / (float)field.width;
    float plot_h    = std::min(avail_h, avail_w * ratio);
    float plot_w    = plot_h / ratio;

    // --- Map selection (mutually exclusive, no rebuild) ---
    ImGui::RadioButton("u",   &val_map, 0); ImGui::SameLine();
    ImGui::RadioButton("v",   &val_map, 1); ImGui::SameLine();
    ImGui::RadioButton("s2n", &val_map, 2);

    // --- Heatmap plot ---
    if(ImPlot::BeginPlot("##valresults", ImVec2(plot_w, plot_h), ImPlotFlags_Equal))
    {
        ImPlot::SetupAxes("x (mm)", "y (mm)");
        ImPlot::SetupAxesLimits(0, field_w_mm, 0, field_h_mm, ImGuiCond_Once);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, field_w_mm);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, field_h_mm);

        if(ImPlot::IsPlotHovered())
        {
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            int col = (int)(mouse.x / px_to_mm);
            int row = field.height - 1 - (int)(mouse.y / px_to_mm);
            const Eigen::MatrixXf* maps[3] = {&field.u, &field.v, &field.s2n};
            if(col >= 0 && col < field.width && row >= 0 && row < field.height)
            {
                const char* unit = (val_map == 0) ? " dn/du" : (val_map == 1) ? " dn/dv" : "";
                ImGui::SetTooltip("%.4f%s", (*maps[val_map])(row, col), unit);
            }
        }

        ImPlot::PlotImage("##heatmap", (ImTextureID)val_textures[val_map],
                          ImPlotPoint(0, 0), ImPlotPoint(field_w_mm, field_h_mm));
        ImPlot::EndPlot();
    }

    // --- Colormap scale bar ---
    ImGui::SameLine();
    ImPlot::PushColormap(ImPlotColormap_Viridis);
    ImPlot::ColormapScale("##scale", val_cmap_min[val_map], val_cmap_max[val_map], ImVec2(scale_w, plot_h));
    ImPlot::PopColormap();

    // --- Range controls: Min | Max | Auto (per-map) ---
    float auto_btn_w = ImGui::CalcTextSize("Auto").x + ImGui::GetStyle().FramePadding.x * 2;
    float half_w     = (plot_w - ImGui::GetStyle().ItemSpacing.x * 2 - auto_btn_w) * 0.5f;

    {
        const Eigen::MatrixXf* maps[3] = {&field.u, &field.v, &field.s2n};
        float data_min = maps[val_map]->minCoeff();
        float data_max = maps[val_map]->maxCoeff();
        float step = (data_max - data_min) / 200.0f;
        int decimals = std::max(0, (int)std::ceil(-std::log10(step)));
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Min##cmap", &val_cmap_min[val_map], data_min, val_cmap_max[val_map], fmt);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Max##cmap", &val_cmap_max[val_map], val_cmap_min[val_map], data_max, fmt);
        ImGui::SameLine();
        if(ImGui::Button("Auto##cmap"))
        {
            val_cmap_min[val_map] = data_min;
            val_cmap_max[val_map] = data_max;
        }
    }

    ImGui::End();
}

void UI::RebuildSurfTexture()
{
    const Eigen::MatrixXf& surface = session.GetSurface();
    int w = (int)surface.cols();
    int h = (int)surface.rows();

    float range = surf_cmap_max - surf_cmap_min;

    std::vector<uint8_t> pixels(w * h * 4);

    ImPlot::PushColormap(ImPlotColormap_Viridis);
    for(int r = 0; r < h; r++)
    {
        for(int c = 0; c < w; c++)
        {
            float t = (range != 0.0f) ? (surface(r, c) - surf_cmap_min) / range : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            ImVec4 col = ImPlot::SampleColormap(t);
            int idx = (r * w + c) * 4;
            pixels[idx+0] = (uint8_t)(col.x * 255);
            pixels[idx+1] = (uint8_t)(col.y * 255);
            pixels[idx+2] = (uint8_t)(col.z * 255);
            pixels[idx+3] = 255;
        }
    }
    ImPlot::PopColormap();

    if(surf_texture) SDL_DestroyTexture(surf_texture);
    surf_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_SetTextureScaleMode(surf_texture, SDL_SCALEMODE_NEAREST);
    SDL_UpdateTexture(surf_texture, nullptr, pixels.data(), w * 4);
}

void UI::DrawSurf()
{
    ImGui::Begin("Surface");

    const Eigen::MatrixXf& surface = session.GetSurface();

    // --- Invalidate texture when reconstruction reruns ---
    static StageState last_recon_state = Idle;
    StageState cur_recon_state = session.GetStageState(STAGE_RECON);
    if(cur_recon_state == Done && last_recon_state != Done)
    { SDL_DestroyTexture(surf_texture); surf_texture = nullptr; }
    last_recon_state = cur_recon_state;

    // --- Rebuild if invalidated or colormap range changed ---
    static float last_min = 0, last_max = 0;
    if(surf_texture == nullptr)
    {
        surf_cmap_min = surface.minCoeff();
        surf_cmap_max = surface.maxCoeff();
    }
    if(surf_texture == nullptr || surf_cmap_min != last_min || surf_cmap_max != last_max)
    {
        RebuildSurfTexture();
        last_min = surf_cmap_min; last_max = surf_cmap_max;
    }

    // --- Physical scale: same PIV cell spacing -> mm ---
    float Z_i      = session.opticalparameters.f * (session.opticalparameters.Z_a + session.opticalparameters.Z_d)
                     / (session.opticalparameters.Z_a + session.opticalparameters.Z_d - session.opticalparameters.f);
    float px_to_mm = (session.pivparameters.window_size - session.pivparameters.overlap)
                     * session.opticalparameters.P_px * session.opticalparameters.Z_a / Z_i / 1000.0f;
    float field_w_mm = surface.cols() * px_to_mm;
    float field_h_mm = surface.rows() * px_to_mm;

    // --- Layout ---
    float scale_w    = 85.0f;
    float controls_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    float avail_w    = ImGui::GetContentRegionAvail().x - scale_w - ImGui::GetStyle().ItemSpacing.x;
    float avail_h    = ImGui::GetContentRegionAvail().y - controls_h;
    float ratio      = (float)surface.rows() / (float)surface.cols();
    float plot_h     = std::min(avail_h, avail_w * ratio);
    float plot_w     = plot_h / ratio;

    // --- Surface plot ---
    if(ImPlot::BeginPlot("##surfplot", ImVec2(plot_w, plot_h), ImPlotFlags_Equal))
    {
        ImPlot::SetupAxes("x (mm)", "y (mm)");
        ImPlot::SetupAxesLimits(0, field_w_mm, 0, field_h_mm, ImGuiCond_Once);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, field_w_mm);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, field_h_mm);

        if(ImPlot::IsPlotHovered())
        {
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            int col = (int)(mouse.x / px_to_mm);
            int row = (int)surface.rows() - 1 - (int)(mouse.y / px_to_mm);
            if(col >= 0 && col < surface.cols() && row >= 0 && row < surface.rows())
                ImGui::SetTooltip("%.4f dn", surface(row, col));
        }

        ImPlot::PlotImage("##surfimage", (ImTextureID)surf_texture,
                          ImPlotPoint(0, 0), ImPlotPoint(field_w_mm, field_h_mm));
        ImPlot::EndPlot();
    }

    // --- Colormap scale bar ---
    ImGui::SameLine();
    ImPlot::PushColormap(ImPlotColormap_Viridis);
    ImPlot::ColormapScale("##surfscale", surf_cmap_min, surf_cmap_max, ImVec2(scale_w, plot_h));
    ImPlot::PopColormap();

    // --- Range controls: Min | Max | Auto ---
    float auto_btn_w = ImGui::CalcTextSize("Auto").x + ImGui::GetStyle().FramePadding.x * 2;
    float half_w     = (plot_w - ImGui::GetStyle().ItemSpacing.x * 2 - auto_btn_w) * 0.5f;

    {
        float data_min = surface.minCoeff();
        float data_max = surface.maxCoeff();
        float step = (data_max - data_min) / 200.0f;
        int decimals = std::max(0, (int)std::ceil(-std::log10(step)));
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Min##surfcmap", &surf_cmap_min, data_min, surf_cmap_max, fmt);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Max##surfcmap", &surf_cmap_max, surf_cmap_min, data_max, fmt);
        ImGui::SameLine();
        if(ImGui::Button("Auto##surfcmap"))
        {
            surf_cmap_min = data_min;
            surf_cmap_max = data_max;
        }
    }

    ImGui::End();
}

void UI::ApplyDarkTheme()
{
    ImGui::StyleColorsDark();

    ImVec4 bg0     = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    ImVec4 bg1     = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
    ImVec4 bg2     = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    ImVec4 bg3     = ImVec4(0.24f, 0.24f, 0.32f, 1.00f);
    ImVec4 bg4     = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
    ImVec4 accent  = ImVec4(0.28f, 0.60f, 0.90f, 1.00f);
    ImVec4 acc_hov = ImVec4(0.38f, 0.70f, 1.00f, 1.00f);
    ImVec4 acc_act = ImVec4(0.20f, 0.50f, 0.80f, 1.00f);
    ImVec4 text    = ImVec4(0.88f, 0.88f, 0.92f, 1.00f);
    ImVec4 text_d  = ImVec4(0.50f, 0.50f, 0.56f, 1.00f);
    ImVec4 sep     = ImVec4(0.22f, 0.22f, 0.30f, 1.00f);

    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = text_d;
    c[ImGuiCol_WindowBg]              = bg1;
    c[ImGuiCol_ChildBg]               = bg0;
    c[ImGuiCol_PopupBg]               = bg1;
    c[ImGuiCol_Border]                = sep;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = bg2;
    c[ImGuiCol_FrameBgHovered]        = bg3;
    c[ImGuiCol_FrameBgActive]         = bg4;
    c[ImGuiCol_TitleBg]               = bg0;
    c[ImGuiCol_TitleBgActive]         = bg0;
    c[ImGuiCol_TitleBgCollapsed]      = bg0;
    c[ImGuiCol_MenuBarBg]             = bg0;
    c[ImGuiCol_ScrollbarBg]           = bg0;
    c[ImGuiCol_ScrollbarGrab]         = bg3;
    c[ImGuiCol_ScrollbarGrabHovered]  = bg4;
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = acc_act;
    c[ImGuiCol_Button]                = bg3;
    c[ImGuiCol_ButtonHovered]         = bg4;
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = bg3;
    c[ImGuiCol_HeaderHovered]         = bg4;
    c[ImGuiCol_HeaderActive]          = accent;
    c[ImGuiCol_Separator]             = sep;
    c[ImGuiCol_SeparatorHovered]      = acc_hov;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = bg3;
    c[ImGuiCol_ResizeGripHovered]     = acc_hov;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = bg2;
    c[ImGuiCol_TabHovered]            = acc_hov;
    c[ImGuiCol_TabSelected]           = accent;
    c[ImGuiCol_TabDimmed]             = bg1;
    c[ImGuiCol_TabDimmedSelected]     = bg3;
    c[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    c[ImGuiCol_DockingEmptyBg]        = bg0;
    c[ImGuiCol_PlotLines]             = accent;
    c[ImGuiCol_PlotLinesHovered]      = acc_hov;
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = acc_hov;
    c[ImGuiCol_TableHeaderBg]         = bg2;
    c[ImGuiCol_TableBorderStrong]     = sep;
    c[ImGuiCol_TableBorderLight]      = bg3;
    c[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight]          = accent;
}

void UI::ApplyLightTheme()
{
    ImGui::StyleColorsLight();

    ImVec4 bg0     = ImVec4(0.90f, 0.90f, 0.94f, 1.00f);
    ImVec4 bg1     = ImVec4(0.95f, 0.95f, 0.98f, 1.00f);
    ImVec4 bg2     = ImVec4(0.98f, 0.98f, 1.00f, 1.00f);
    ImVec4 bg3     = ImVec4(0.84f, 0.84f, 0.90f, 1.00f);
    ImVec4 bg4     = ImVec4(0.76f, 0.76f, 0.84f, 1.00f);
    ImVec4 accent  = ImVec4(0.10f, 0.48f, 0.82f, 1.00f);
    ImVec4 acc_hov = ImVec4(0.08f, 0.38f, 0.70f, 1.00f);
    ImVec4 acc_act = ImVec4(0.06f, 0.28f, 0.58f, 1.00f);
    ImVec4 text    = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    ImVec4 text_d  = ImVec4(0.50f, 0.50f, 0.58f, 1.00f);
    ImVec4 sep     = ImVec4(0.74f, 0.74f, 0.80f, 1.00f);

    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = text_d;
    c[ImGuiCol_WindowBg]              = bg1;
    c[ImGuiCol_ChildBg]               = bg0;
    c[ImGuiCol_PopupBg]               = bg2;
    c[ImGuiCol_Border]                = sep;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = bg2;
    c[ImGuiCol_FrameBgHovered]        = bg3;
    c[ImGuiCol_FrameBgActive]         = bg4;
    c[ImGuiCol_TitleBg]               = bg0;
    c[ImGuiCol_TitleBgActive]         = bg0;
    c[ImGuiCol_TitleBgCollapsed]      = bg0;
    c[ImGuiCol_MenuBarBg]             = bg0;
    c[ImGuiCol_ScrollbarBg]           = bg0;
    c[ImGuiCol_ScrollbarGrab]         = bg3;
    c[ImGuiCol_ScrollbarGrabHovered]  = bg4;
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = acc_act;
    c[ImGuiCol_Button]                = bg3;
    c[ImGuiCol_ButtonHovered]         = bg4;
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = bg3;
    c[ImGuiCol_HeaderHovered]         = bg4;
    c[ImGuiCol_HeaderActive]          = accent;
    c[ImGuiCol_Separator]             = sep;
    c[ImGuiCol_SeparatorHovered]      = acc_hov;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = bg3;
    c[ImGuiCol_ResizeGripHovered]     = acc_hov;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = bg3;
    c[ImGuiCol_TabHovered]            = acc_hov;
    c[ImGuiCol_TabSelected]           = accent;
    c[ImGuiCol_TabDimmed]             = bg1;
    c[ImGuiCol_TabDimmedSelected]     = bg4;
    c[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.40f);
    c[ImGuiCol_DockingEmptyBg]        = bg0;
    c[ImGuiCol_PlotLines]             = accent;
    c[ImGuiCol_PlotLinesHovered]      = acc_hov;
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = acc_hov;
    c[ImGuiCol_TableHeaderBg]         = bg3;
    c[ImGuiCol_TableBorderStrong]     = sep;
    c[ImGuiCol_TableBorderLight]      = bg3;
    c[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    c[ImGuiCol_NavHighlight]          = accent;
}

void UI::DrawSettingsPanel()
{
    ImGui::Begin("Settings");

    static int theme = 0; // 0=Dark, 1=Light
    if(ImGui::RadioButton("Dark",  &theme, 0)) ApplyDarkTheme();
    ImGui::SameLine();
    if(ImGui::RadioButton("Light", &theme, 1)) ApplyLightTheme();

    ImGui::End();
}

void UI::DrawFlowPanel()
{

    ImGui::Begin("Flow Image", nullptr);

    if(flow_tex)
    {
        //Immovable chiuld is the only way to remove window movement on the main body
        if(session.mask_apply)
            ImGui::BeginChild("##flow_content", ImVec2(0, 0), false, ImGuiWindowFlags_NoMove);

        float tex_w, tex_h;
        SDL_GetTextureSize(flow_tex, &tex_w, &tex_h);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();

        float scale = std::min(avail.x / tex_w, avail.y / tex_h);
        ImVec2 size = ImVec2(tex_w * scale, tex_h * scale);
        ImVec2 offset = ImVec2((avail.x - size.x) * 0.5f, (avail.y - size.y) * 0.5f);

        //SetCursor defines offset in windo to draw?? Very odd
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + offset.x, ImGui::GetCursorPos().y + offset.y));
        ImGui::Image((ImTextureID)flow_tex, size);

        //-----------------------------------------------------------------
        //Interactive circle mask
        //-----------------------------------------------------------------
        
        if(session.mask_apply)
        {
            bool image_hovered = ImGui::IsItemHovered(); 

            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 mouse = ImGui::GetMousePos();

            // Map to screen space
            ImVec2 win_center = ImVec2(
                origin.x + offset.x + session.posx * scale,
                origin.y + offset.y + session.posy * scale);
            float screen_radius = session.radius * scale;

            // Draw
            draw->AddCircle(win_center, screen_radius, IM_COL32(255, 255, 255, 255), 64, 2.0f);
            draw->AddCircleFilled(win_center, 6.0f, IM_COL32(255, 255, 255, 255));

            // Hit detection
            float dist_to_center = std::hypot(mouse.x - win_center.x, mouse.y - win_center.y);
            bool near_center = dist_to_center < 20.0f;
            bool near_edge   = std::abs(dist_to_center - screen_radius) < 20.0f;

            static bool dragging_center = false;
            static bool dragging_radius = false;

            if(image_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if(near_center)     dragging_center = true;
                else if(near_edge)  dragging_radius = true;
            }

            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                dragging_center = false;
                dragging_radius = false;
            }

            if(dragging_center)
            {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                session.posx += (int)(delta.x / scale);
                session.posy += (int)(delta.y / scale);
            }
            if(dragging_radius)
            {
                int new_radius = (int)(dist_to_center / scale);
                session.radius = std::max(128, new_radius);
            }
        }

        if(session.mask_apply)
            ImGui::EndChild();
    }
    else
    {
        ImGui::TextDisabled("No flow image loaded");
    }
    
    ImGui::End();
}

void UI::DrawRefPanel()
{

    ImGui::Begin("Reference Image", nullptr);

    if(ref_tex)
    {
        //Immovable chiuld is the only way to remove window movement on the main body
        if(session.mask_apply)
            ImGui::BeginChild("##ref_content", ImVec2(0, 0), false, ImGuiWindowFlags_NoMove);

        float tex_w, tex_h;
        SDL_GetTextureSize(ref_tex, &tex_w, &tex_h);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();

        float scale = std::min(avail.x / tex_w, avail.y / tex_h);
        ImVec2 size = ImVec2(tex_w * scale, tex_h * scale);
        ImVec2 offset = ImVec2((avail.x - size.x) * 0.5f, (avail.y - size.y) * 0.5f);

        //SetCursor defines offset in windo to draw?? Very odd
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + offset.x, ImGui::GetCursorPos().y + offset.y));
        ImGui::Image((ImTextureID)ref_tex, size);

        //-----------------------------------------------------------------
        //Interactive circle mask
        //-----------------------------------------------------------------
        
        if(session.mask_apply)
        {
            bool image_hovered = ImGui::IsItemHovered(); 

            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 mouse = ImGui::GetMousePos();

            // Map to screen space
            ImVec2 win_center = ImVec2(
                origin.x + offset.x + session.posx * scale,
                origin.y + offset.y + session.posy * scale);
            float screen_radius = session.radius * scale;

            // Draw
            draw->AddCircle(win_center, screen_radius, IM_COL32(255, 255, 255, 255), 64, 2.0f);
            draw->AddCircleFilled(win_center, 6.0f, IM_COL32(255, 255, 255, 255));

            // Hit detection
            float dist_to_center = std::hypot(mouse.x - win_center.x, mouse.y - win_center.y);
            bool near_center = dist_to_center < 20.0f;
            bool near_edge   = std::abs(dist_to_center - screen_radius) < 20.0f;

            static bool dragging_center = false;
            static bool dragging_radius = false;

            if(image_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if(near_center)     dragging_center = true;
                else if(near_edge)  dragging_radius = true;
            }

            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                dragging_center = false;
                dragging_radius = false;
            }

            if(dragging_center)
            {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                session.posx += (int)(delta.x / scale);
                session.posy += (int)(delta.y / scale);
            }
            if(dragging_radius)
            {
                int new_radius = (int)(dist_to_center / scale);
                session.radius = std::max(128, new_radius);
            }
        }

        if(session.mask_apply)
            ImGui::EndChild();
    }
    else
    {
        ImGui::TextDisabled("No reference image loaded");
    }
    
    ImGui::End();
}


void UI::OnRefSelected(void* userdata, const char* const* filelist, int filter)
{
    UI* ui = static_cast<UI*>(userdata);
    if(filelist && filelist[0])
        ui->pending_ref_path = filelist[0];
}

void UI::OnFlowSelected(void* userdata, const char* const* filelist, int filter)
{
    UI* ui = static_cast<UI*>(userdata);
    if(filelist && filelist[0])
        ui->pending_flow_path = filelist[0];
}
