#include <water/ui.h>

UI::UI(Session& session, SDL_Renderer* renderer)
    : session(session), renderer(renderer)
{
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
    ImGui::SliderInt("Overlap", &session.pivparameters.overlap, 0, 32);
    ImGui::SliderInt("Search Size", &session.pivparameters.search_size, 0, 196);

    ImGui::PopItemWidth();
    ImGui::SeparatorText("Experimental Parameters");
    ImGui::PushItemWidth(-230.0f);

    ImGui::InputFloat("Sample Thickness (mm)", &session.opticalparameters.t, 0.1f);
    ImGui::InputFloat("Sensor Pixel Pitch (um)", &session.opticalparameters.P_px, 0.05f);
    ImGui::InputFloat("Background -> Sample (mm)", &session.opticalparameters.Z_d, 25.0f);
    ImGui::InputFloat("Sample -> Lens (mm)", &session.opticalparameters.Z_a, 25.0f);
    ImGui::InputFloat("Lens Focal Length (mm)", &session.opticalparameters.f, 5.0f);
    ImGui::InputFloat("Aperture Diameter (mm)", &session.opticalparameters.d_a, 1.0f);
    ImGui::InputFloat("Background Dot Diameter (mm)", &session.opticalparameters.d_bg, 0.05f);
    
    ImGui::PopItemWidth();
    ImGui::SeparatorText("Mask Parameters");
    ImGui::PushItemWidth(-100.0f);

    ImGui::Checkbox("Mask Enabled", &session.mask_apply);

    ImGui::SliderInt("X Position", &session.posx, 0, 5640);
    ImGui::SliderInt("Y Position", &session.posy, 0, 5640);
    ImGui::SliderInt("Radius", &session.radius, 0, 2820);

    ImGui::PopItemWidth();

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

    bool was_disabled = session.GetStageState(STAGE_PIV) != Ready || session.IsRunning();
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

    was_disabled = session.GetStageState(STAGE_VAL) != Ready || session.IsRunning();
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

    was_disabled = session.GetStageState(STAGE_RECON) != Ready || session.IsRunning();
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
    if(session.GetStageState(STAGE_PIV) == Done)
    {
        DrawPIV();
    }
}

void UI::DrawPIV()
{
    ImGui::Begin("PIV Results");

        static float cmap_min = -2.0f;
        static float cmap_max =  2.0f;

        float controls_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
        float scale_w    = 65.0f;
        float avail_w    = ImGui::GetContentRegionAvail().x - scale_w - ImGui::GetStyle().ItemSpacing.x;
        float avail_h    = ImGui::GetContentRegionAvail().y - controls_h;
        float ratio      = (float)session.GetRawField().height / (float)session.GetRawField().width;

        // Fit to available space while preserving aspect ratio
        float plot_h = std::min(avail_h, avail_w * ratio);
        float plot_w = plot_h / ratio;

        //--------------------------------------------------
        // Main PIV Plot
        //--------------------------------------------------

        //TODO: Move somewhere else
        float Z_i = session.opticalparameters.f * (session.opticalparameters.Z_a + session.opticalparameters.Z_d)
                    / (session.opticalparameters.Z_a + session.opticalparameters.Z_d - session.opticalparameters.f);

        float px_to_um = (session.pivparameters.window_size - session.pivparameters.overlap) 
                            * session.opticalparameters.P_px * session.opticalparameters.Z_a / Z_i;

        float px_to_mm = px_to_um / 1000;

        if(ImPlot::BeginPlot("##pivmain", ImVec2(plot_w, plot_h), ImPlotFlags_Equal))
        {
            ImPlot::SetupAxes("x (mm)", "y (mm)", 0, 0);
            ImPlot::SetupAxesLimits(0, session.GetRawField().width * px_to_mm, 0, session.GetRawField().height * px_to_mm, ImGuiCond_Once);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, session.GetRawField().width * px_to_mm);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, session.GetRawField().height * px_to_mm);
            
            if(ImPlot::IsPlotHovered())
            {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                float real_x = mouse.x * px_to_mm;
                float real_y = mouse.y * px_to_mm;
                ImGui::SetTooltip("%.2f mm, %.2f mm", real_x, real_y);
            }

            ImPlot::PushColormap(ImPlotColormap_Viridis);
            ImPlot::PlotHeatmap(
                "u",
                session.GetRawField().u.data(),
                session.GetRawField().height,
                session.GetRawField().width,
                cmap_min, cmap_max,
                nullptr,
                ImPlotPoint(0, 0),
                ImPlotPoint(session.GetRawField().width * px_to_mm, session.GetRawField().height * px_to_mm),
                {ImPlotHeatmapFlags_ColMajor}
            );

            ImPlot::PopColormap();

            ImPlot::EndPlot();
        }

        ImGui::SameLine();
        ImPlot::PushColormap(ImPlotColormap_Viridis);
        ImPlot::ColormapScale("##scale", cmap_min, cmap_max, ImVec2(scale_w, plot_h));
        ImPlot::PopColormap();

        //--------------------------------------------------
        // Colormap range controls
        //--------------------------------------------------
        float half_w = (plot_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Min##cmap", &cmap_min, -10.0f, 0.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half_w);
        ImGui::SliderFloat("Max##cmap", &cmap_max, 0.0f, 10.0f);

        ImGui::End();
}

void UI::DrawSettingsPanel()
{
    ImGui::Begin("Settings");

    static int theme = 0; // 0=Dark, 1=Light, 2=Classic
    if(ImGui::RadioButton("Dark",    &theme, 0)) ImGui::StyleColorsDark();
    ImGui::SameLine();
    if(ImGui::RadioButton("Light",   &theme, 1)) ImGui::StyleColorsLight();
    ImGui::SameLine();
    if(ImGui::RadioButton("Classic", &theme, 2)) ImGui::StyleColorsClassic();

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
