# ## Data & Library Usage

- **`std::vector<unsigned char>`** — used only in `Image` for raw pixel storage. No math, no Eigen.
- **Eigen matrices** — used everywhere past `Image`. The conversion from unsigned char to Eigen happens once inside `PIV` when it reads from the two `Image` objects.
    - `PIV` — u/v displacement grids, sig2noise
    - `Validation` — outlier flagging and replacement
    - `Reconstruction` — gradients, integration, delta_n
    - `Mask` — 2D float array of 1.0/NaN values
    - `Session` — stores all results as Eigen matrices ready for the GUI

---

The goal of this phase is a working CLI tool equivalent to the Python script.

1. **`Image` class** — Represents a single image. One instance per image loaded. Verify pixel data is correct in tests
2. **`Mask` class** — Generate circle mask, verify dimensions and NaN boundary in tests
3. **`PIV` class** — Cross correlate two images, return u/v/sig2noise vector field
4. **`Validation` class** — Flag and replace outliers, verify with synthetic known data
5. **`Reconstruction` class** — Take vector field, return delta_n map
6. **`Session` class** — Owns all state: two separate `Image` instances (ref and scaffold), parameters, vector field, and delta_n results. Acts as the single point of contact between the GUI and the processing pipeline
7. **`main.cpp`** — Wire it all together and save output images to disk

---

## Phase 2 — GUI Shell

The goal of this phase is a functional window with controls but no live pipeline yet.

7. SDL3 window opens, renders a blank ImGui layout
8. File picker buttons load ref and scaffold images, display them as textures
9. Parameter sliders for Z_B, Z_D, px_size, window size, overlap
10. Circle mask selector drawn over the image, draggable

---

## Phase 3 — Live Pipeline

The goal of this phase is a fully interactive tool.

11. Run PIV button triggers the pipeline, results display in the window
12. Toggle between magnitude, u, v, delta_n views
13. Colormap applied to result textures
14. Save output images from within the GUI

---

## Phase 4 — Polish

15. Progress indicator during PIV (will be slow on 4K images)
16. Multithreading — run pipeline on a background thread so GUI doesn't freeze
17. Config save/load so parameters don't need to be re-entered every run