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
add_library(dune_core)
target_sources(dune_core PRIVATE
    src/session.cpp
    src/grains/correlator-cmm.cpp
    src/grains/correlator-pid.cpp
    src/grains/correlator.cpp
    src/grains/reconstruction.cpp
    src/grains/validation.cpp
    src/sun/colormap.cpp
    src/sun/image.cpp
    src/sun/mask.cpp
    src/sun/lib/stb.cpp
    src/water/app.cpp
    src/water/ui.cpp
    src/wind/vectorfield.cpp
)
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
add_custom_command(TARGET sand_dune POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/rsc
        $<TARGET_FILE_DIR:sand_dune>/rsc
    COMMENT "Copying runtime resources"
)

# Tests
add_executable(run_tests
    tests/test.cpp
)
target_link_libraries(run_tests PRIVATE dune_core)
