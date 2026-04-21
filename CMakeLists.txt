cmake_minimum_required(VERSION 3.16)

project(sand_dune LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(FFTW3 CONFIG REQUIRED)

if(TARGET SDL3_image::SDL3_image)
    set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
elseif(TARGET SDL3_image::SDL3_image-shared)
    set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image-shared)
elseif(TARGET SDL3_image::SDL3_image-static)
    set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image-static)
else()
    message(FATAL_ERROR "Could not determine the SDL3_image target exported by the installed package.")
endif()

if(TARGET FFTW3::fftw3f)
    set(FFTW3F_LINK_TARGET FFTW3::fftw3f)
else()
    find_library(FFTW3F_LIBRARY_RELEASE NAMES fftw3f REQUIRED PATH_SUFFIXES lib)
    find_library(FFTW3F_LIBRARY_DEBUG NAMES fftw3f PATH_SUFFIXES debug/lib)
endif()

# Core library
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(dune_core ${SOURCES})
target_include_directories(dune_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(DEFINED FFTW3_INCLUDE_DIRS)
    target_include_directories(dune_core PUBLIC ${FFTW3_INCLUDE_DIRS})
endif()

target_link_libraries(dune_core
    PUBLIC
        SDL3::SDL3
        ${SDL3_IMAGE_TARGET}
        imgui::imgui
        implot::implot
        Eigen3::Eigen
)

if(DEFINED FFTW3F_LINK_TARGET)
    target_link_libraries(dune_core PUBLIC ${FFTW3F_LINK_TARGET})
elseif(FFTW3F_LIBRARY_DEBUG)
    target_link_libraries(dune_core PUBLIC optimized "${FFTW3F_LIBRARY_RELEASE}" debug "${FFTW3F_LIBRARY_DEBUG}")
else()
    target_link_libraries(dune_core PUBLIC "${FFTW3F_LIBRARY_RELEASE}")
endif()

# Main executable
add_executable(sand_dune src/main.cpp)
target_link_libraries(sand_dune PRIVATE dune_core)

# Tests
file(GLOB TEST_SOURCES "tests/*.cpp")
add_executable(run_tests ${TEST_SOURCES})
target_link_libraries(run_tests PRIVATE dune_core)

target_compile_definitions(run_tests PRIVATE 
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_definitions(dune_core PRIVATE 
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
