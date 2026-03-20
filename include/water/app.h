#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <session.h>
#include <water/ui.h>

class App
{
public:
    App();

    void Init();
    void Run();
    void Shutdown();

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Session session;

    UI ui;

    bool running = true;
};