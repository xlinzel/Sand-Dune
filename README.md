# Sand Dune

Sand Dune is a **Background Oriented Schlieren (BOS)** measurement system for reconstructing weak optical disturbances in transparent media from image displacement. The repository contains:

- a desktop application for loading BOS image pairs, configuring the optical model, running the full processing pipeline, and visualizing results
- the correlation, validation, scaling, and reconstruction code that converts image displacement into refractive-index or thickness-variation maps
- software-verification tests for sub-pixel accuracy, deformation correction, and image-pair diagnostics
- technical documentation and presentation sources covering the hardware, theory, implementation, and validation of the system

## What the Repository Does

At a high level, Sand Dune takes a **reference image** and a **flow image**, measures the apparent background displacement caused by a transparent sample, and reconstructs a physical map from that displacement.

The BOS processing chain is:

1. Load reference and flow images
2. Compute displacement with FFT-based cross-correlation
3. Refine the peak with CMM sub-pixel estimation
4. Optionally apply PID window deformation
5. Validate the vector field using S2N and normalized residual checks
6. Scale the displacement field into physical gradient units
7. Reconstruct a relative surface using masked Poisson reconstruction by default, or Frankot-Chellappa as an alternative
8. Display and export the correlation field, validated field, and reconstructed surface

The main code is organized as follows:

- `src/main.cpp`
  Application entry point
- `src/session.cpp`
  High-level pipeline orchestration, scaling, correction, state tracking, and export
- `src/grains/`
  Correlation, PID, validation, and reconstruction code
- `src/water/`
  UI and app-level interaction logic
- `src/sun/`
  Images, masks, and colormap utilities
- `include/`
  Public headers matching the application and processing modules
- `tests/test.cpp`
  Verification tests built into `run_tests`
- `latex/`
  LaTeX report and presentation sources
- `docs/`
  Doxygen configuration and generated API output

## Quick Start (Windows)

The repository now carries its own dependency manifest (`vcpkg.json`), CMake presets (`CMakePresets.json`), and bootstrap script. For a normal Windows setup, the intended path is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap.ps1
```

That single command will:

1. clone `vcpkg` into `.deps/vcpkg` if it is not already present
2. check out the pinned vcpkg baseline used by this repo
3. bootstrap `vcpkg`
4. install the C/C++ dependencies declared in `vcpkg.json`
5. configure the project with the repo's `windows-x64-debug` preset
6. build the project

The first run can take several minutes because it may need to download and build the full dependency set. After the initial setup, later reconfigures and rebuilds are much faster.

When it finishes, the main application will be at:

```text
build/windows-x64-debug/sand_dune.exe
```

Then run it with:

```powershell
.\build\windows-x64-debug\sand_dune.exe
```

This is the intended "pull the repo and run one command" path on Windows.

### Optional bootstrap variants

Build release instead of debug:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap.ps1 -Configuration Release
```

Build `x86-windows` instead of `x64-windows`:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap.ps1 -Triplet x86-windows
```

Configure only, without building:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap.ps1 -NoBuild
```

## Prerequisites

The bootstrap flow is intended to avoid manual vcpkg setup, but a few base tools still need to exist on the machine:

- **Git**
- **Visual Studio 2022** or **Visual Studio Build Tools 2022** with the Desktop C++ workload
- **PowerShell**

The script will automatically use the Visual Studio developer environment and the CMake/Ninja tools bundled with that installation when available. It does **not** require you to hardcode local toolchain paths in the repo.

If you are using **VS Code**, the **CMake Tools** extension still makes target selection and debugging much easier, but it is no longer the thing that makes the build work in the first place.

### Fresh machine checklist

On a fresh Windows machine, the simplest path is:

1. install Git
2. install Visual Studio 2022 or Build Tools 2022 with the Desktop C++ workload
3. clone the repository
4. run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap.ps1
```

After that, the repo should have:

- `.deps/vcpkg/` populated locally
- `build/windows-x64-debug/` configured
- `build/windows-x64-debug/sand_dune.exe` built

## Manual Build Workflow

If you want to drive CMake directly after bootstrapping, the repo already includes presets:

```powershell
cmake --preset windows-x64-debug
cmake --build --preset build-windows-x64-debug
```

For a release build:

```powershell
cmake --preset windows-x64-release
cmake --build --preset build-windows-x64-release
```

Other available presets are:

- `windows-x64-release`
- `windows-x86-debug`
- `windows-x86-release`

## VS Code Workflow

If you want to work from VS Code after the bootstrap step:

1. open the repository folder in VS Code
2. install the **CMake Tools** extension if it is not already installed
3. run `CMake: Select a Kit`
4. choose an **x64 MSVC** kit
5. run `CMake: Delete Cache and Reconfigure` once if the workspace was previously configured with stale settings
6. use `CMake: Build`, `CMake: Run Without Debugging`, or `CMake: Debug`

With the current workspace settings, saving `CMakeLists.txt` should trigger reconfigure automatically.

## Running the Application

After building:

```powershell
.\build\windows-x64-debug\sand_dune.exe
```

If you built with a different preset, run the executable from the matching build directory.

## How to Use the Software

The typical workflow inside the application is:

### 1. Load images

Use the **Load** panel to load:

- one reference image
- one or more flow images

All loaded images must have the same pixel dimensions.

### 2. Set correlation parameters

In the **Parameters** panel, choose:

- interrogation window size
- overlap
- whether PID is enabled
- PID iteration count and relaxation settings if needed

These parameters control the balance between spatial resolution, robustness, and deformation tolerance.

### 3. Set optical and sample parameters

Enter the physical parameters used for BOS scaling:

- `P_px` - sensor pixel pitch
- `Z_d` - background-to-sample distance
- `Z_a` - sample-to-lens distance
- `f` - focal length
- `n` - sample refractive index
- `t` - sample thickness

These values directly control:

- the physical field of view shown on the axes
- the conversion from pixel displacement to physical gradient units
- the final reconstructed surface amplitude

### 4. Choose reconstruction mode

Select whether the software should reconstruct:

- refractive-index variation (`dn`)
- thickness variation (`mm`)

Then choose the reconstruction backend:

- `Masked Poisson` - default and preferred for masked sample regions
- `Frankot-Chellappa` - optional FFT-based integration for comparison or full-field use

### 5. Configure the mask

If only part of the image contains the valid sample region, enable the circular mask and set:

- mask center
- mask radius

This restricts reconstruction to the trusted sample aperture and excludes holder/background clutter.

### 6. Run the pipeline

You can run:

- correlation
- validation
- reconstruction

individually, or use:

- `Run Full Pipeline`

to execute the complete chain in sequence.

### 7. Inspect the results

The UI exposes separate views for:

- the raw correlation field
- the validated field
- the reconstructed surface

Use these views together. The gradient fields are best for locating localized defects and steep transitions, while the reconstructed surface is best for overall uniformity and large-scale variation.

### 8. Export results

Use the save/export controls to write CSV outputs for each processed image. These exports correspond to the major BOS stages:

- correlation field
- validated field
- reconstructed surface

These outputs are intended for external plotting, statistics, archival, or comparison across runs.

## Running Tests

Build the test executable:

```powershell
cmake --build --preset build-windows-x64-debug --target run_tests
```

Run the full verification suite:

```powershell
ctest --preset test-windows-x64-debug
```

Run the test binary directly:

```powershell
.\build\windows-x64-debug\run_tests.exe
```

Run a single doctest case by name:

```powershell
.\build\windows-x64-debug\run_tests.exe -tc="Slide Gradient Diagnostics"
```

The tests are development verification tools, not part of the normal user workflow. They are used to check:

- image loading
- synthetic sub-pixel displacement recovery
- PID affine deformation improvement
- validation behavior
- real image-pair precision and gradient diagnostics

## Generating Documentation

### API documentation

Generate Doxygen HTML output:

```powershell
doxygen Doxyfile
```

Generated output is written under:

```text
docs/output
```

### Technical report

From the `latex/` directory:

```powershell
latexmk -pdf documentation.tex
```

### Presentation

From the `latex/` directory:

```powershell
latexmk -pdf presentation.tex
```

If you do not want to maintain a local LaTeX installation, the simplest option is usually to copy the contents of `latex/` into an **Overleaf** project and build there. For both the report and the presentation, that is often much lower friction than maintaining a full local TeX environment.

### Force a clean rebuild

```powershell
latexmk -gg -pdf documentation.tex
latexmk -gg -pdf presentation.tex
```

## Calibration Notes

Accurate BOS reconstruction depends strongly on correct optical geometry. In practice, errors in:

- `Z_a`
- `Z_d`
- `f`
- `P_px`

affect both the displayed physical axis scale and the reconstructed amplitude.

For that reason, the system should be calibrated against a known physical field width rather than relying only on mechanical lens dimensions.

## Known Limitations

- BOS measures integrated optical-path effects and cannot independently recover both `n` and `t` without additional information
- the reconstructed surface is relative and includes an arbitrary constant offset unless externally referenced
- masked sparse Poisson reconstruction is more appropriate than Frankot-Chellappa when the valid sample occupies only part of the frame
- results depend strongly on background pattern quality, focus, illumination, geometry calibration, and correlation settings

## Project Status

Sand Dune is an active hardware-and-software BOS project combining:

- optical rig design
- image processing and reconstruction software
- quantitative verification
- technical documentation and presentation material

The repository is intended to hold both the working software and the supporting technical material used to design, validate, and present the system.
