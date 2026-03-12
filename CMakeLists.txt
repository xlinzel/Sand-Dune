cmake_minimum_required(VERSION 3.16)

project(sand_dune)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

if(WIN32)
    find_package(imgui REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8
    )
    FetchContent_MakeAvailable(imgui)
    add_library(imgui_fetched STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
    target_include_directories(imgui_fetched PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_fetched PUBLIC SDL3::SDL3)
    add_library(imgui::imgui ALIAS imgui_fetched)
endif()

find_package(Eigen3 REQUIRED)
if(NOT WIN32)
    set(FFTW3f_DIR "/usr/lib/cmake/fftw3")
endif()
find_package(FFTW3f REQUIRED)
if(NOT TARGET FFTW3::fftw3f)
    add_library(FFTW3::fftw3f SHARED IMPORTED)
    set_target_properties(FFTW3::fftw3f PROPERTIES
        IMPORTED_LOCATION "/usr/lib/libfftw3f.so"
        INTERFACE_INCLUDE_DIRECTORIES "/usr/include"
    )
endif()
# Core library
file(GLOB_RECURSE SOURCES src/**/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(dune_core ${SOURCES})
target_include_directories(dune_core PUBLIC include)
target_link_libraries(dune_core PUBLIC SDL3::SDL3)
target_link_libraries(dune_core PUBLIC Eigen3::Eigen)
target_link_libraries(dune_core PUBLIC FFTW3::fftw3f)

# Main executable
add_executable(sand_dune src/main.cpp)
target_link_libraries(sand_dune PRIVATE
    dune_core
    SDL3::SDL3
    SDL3_image::SDL3_image
    imgui::imgui
)

# Tests
file(GLOB TEST_SOURCES "tests/*.cpp")
add_executable(run_tests ${TEST_SOURCES})
target_link_libraries(run_tests PRIVATE dune_core)

target_compile_definitions(run_tests PRIVATE 
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)