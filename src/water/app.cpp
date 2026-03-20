#include <iostream>

#include <water/app.h>

App::App()
    : ui(session, nullptr)
{

}

void App::Init()
{
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        throw std::runtime_error("Error: SDL_Init(): " + std::string(SDL_GetError()) + "\n");
    }

    float mainscale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags windowflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    
    //Load saved window size .ini (thank you ImGui)
    int win_w = 1280, win_h = 800;
    FILE* f = fopen("window.ini", "r");
    if(f) { fscanf(f, "%d %d", &win_w, &win_h); fclose(f); }

    window = SDL_CreateWindow("Sand Dune", win_w, win_h, windowflags);

    if(window == nullptr)
    {
        throw std::runtime_error("Error: SDL_CreateWindow(): " + std::string(SDL_GetError()) + "\n");
    }

    renderer = SDL_CreateRenderer(window, nullptr);

    if(renderer == nullptr)
    {
        throw std::runtime_error("Error: SDL_CreateREnderer(): " + std::string(SDL_GetError()) + "\n");
    }

    SDL_SetRenderVSync(renderer, 1);

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    //Set nicer font
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f);

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 0.0f;
    style.WindowRounding = 10.0f;
    style.WindowPadding = ImVec2(10, 10);

    style.FrameRounding    = 4.0f;  // buttons, sliders, inputs
    style.GrabRounding     = 4.0f;  // slider grab
    style.ScrollbarRounding = 4.0f;

    //Remover header colours
    //ImVec4 title_color = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    //style.Colors[ImGuiCol_TitleBg]       = title_color;
    //style.Colors[ImGuiCol_TitleBgActive] = title_color;

    //Change from default colors
    ImVec4 gray       = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
    ImVec4 gray_hover = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    ImVec4 gray_act   = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);

    style.Colors[ImGuiCol_Button]        = gray;
    style.Colors[ImGuiCol_ButtonHovered] = gray_hover;
    style.Colors[ImGuiCol_ButtonActive]  = gray_act;

    style.Colors[ImGuiCol_SliderGrab]        = gray;
    style.Colors[ImGuiCol_SliderGrabActive]  = gray_act;

    style.Colors[ImGuiCol_FrameBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive]   = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);

    style.Colors[ImGuiCol_CheckMark]       = gray_act;
    style.Colors[ImGuiCol_Header]          = gray;
    style.Colors[ImGuiCol_HeaderHovered]   = gray_hover;
    style.Colors[ImGuiCol_HeaderActive]    = gray_act;
    
    style.Colors[ImGuiCol_TitleBg] = style.Colors[ImGuiCol_TitleBgActive];
    style.ScaleAllSizes(mainscale);

    //----------------------

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    //TODO: Change font
    

    ui.SetRenderer(renderer);
}

void App::Run()
{
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    while(running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ui.Draw();

        ImGui::Render();

        SDL_SetRenderScale(renderer,
            ImGui::GetIO().DisplayFramebufferScale.x,
            ImGui::GetIO().DisplayFramebufferScale.y);

        SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }
}

void App::Shutdown()
{
    //Save window size (finally noi mor resizing on launch)
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    FILE* f = fopen("window.ini", "w");
    if(f) { fprintf(f, "%d %d", w, h); fclose(f); }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}