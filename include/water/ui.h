#pragma once

#include <session.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <implot.h>
#include <iostream>

class UI
{
public:
    UI(Session& session, SDL_Renderer* renderer);

    void SetRenderer(SDL_Renderer* renderer);

    void Draw();
    void DrawLoadPanel();
    void DrawParametersPanel();
    void DrawCalculationsPanel();
    void DrawPipelinePanel();
    void DrawCalcualtionsPanel();
    void DrawVisualizationPanel();

    void DrawPIV();
    void DrawVal();
    void DrawSurf();

    void DrawSettingsPanel();
    void DrawSavePanel();

    void DrawRefPanel();
    void DrawFlowPanel();

    static void ApplyDarkTheme();
    static void ApplyLightTheme();

    static void OnRefSelected(void* userdata, const char* const* filelist, int filter);
    static void OnFlowSelected(void* userdata, const char* const* filelist, int filter);
    static void OnSaveDirSelected(void* userdata, const char* const* filelist, int filter);

private:
    void RebuildPIVTextures();
    void RebuildValTextures();
    void RebuildSurfTexture();

    Session& session;
    SDL_Renderer* renderer = nullptr;

    //Original Images
    SDL_Texture* ref_tex = nullptr;
    SDL_Texture* flow_tex = nullptr;
    SDL_Texture* surface_tex = nullptr;

    //Generated Images and tracking
    std::vector<SDL_Texture*> piv_textures = {nullptr, nullptr, nullptr};
    int   piv_map  = 0;                          // 0=u, 1=v, 2=s2n
    float piv_cmap_min[3] = {-2.0f, -2.0f, 0.0f};
    float piv_cmap_max[3] = { 2.0f,  2.0f,  2.0f};
    
    std::vector<SDL_Texture*> val_textures = {nullptr, nullptr, nullptr};
    int   val_map  = 0;                          // 0=u, 1=v, 2=s2n
    float val_cmap_min[3] = {-2.0f, -2.0f, 0.0f};
    float val_cmap_max[3] = { 2.0f,  2.0f,  2.0f};
    
    SDL_Texture* surf_texture = nullptr;
    float surf_cmap_min = -0.1f;
    float surf_cmap_max = 0.1f;

    std::string pending_ref_path;
    std::vector<std::string> pending_flow_paths;
    bool file_dialog_open = false;

    std::string save_dir = ".";
    bool save_dir_dialog_open = false;

    //Background image rendering
    SDL_Surface* surf = nullptr;
    SDL_Texture* texture_bg = nullptr;
};