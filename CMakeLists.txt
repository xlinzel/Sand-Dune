cmake_minimum_required(VERSION 3.16)
project(sand_dune)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(imgui REQUIRED)
find_package(Eigen3 REQUIRED)

# Core library
file(GLOB_RECURSE SOURCES src/**/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(dune_core ${SOURCES})
target_include_directories(dune_core PUBLIC include)
target_link_libraries(dune_core PUBLIC SDL3::SDL3)
target_link_libraries(dune_core PUBLIC Eigen3::Eigen)

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