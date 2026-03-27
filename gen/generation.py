# BOS jittered-grid hard-dot generator
# Paper-style placement method, but with hard binary circular dots instead of Gaussian dots.

import os
import math
import random
import numpy as np
from PIL import Image
from reportlab.pdfgen import canvas as pdf_canvas
from reportlab.lib.units import inch

# =========================
# CONFIG
# =========================
DPI = 2400
PAGE_IN = 12.0
LABEL_IN = 0.25  # white strip at bottom for readable label

PAGE_PX = int(round(DPI * PAGE_IN))          # 28800
LABEL_PX = int(round(DPI * LABEL_IN))        # 600
PATTERN_H_PX = PAGE_PX - LABEL_PX            # 28200
PATTERN_W_PX = PAGE_PX                       # 28800

OUTPUT_DIR = "bos_pattern"

DOT_SIZES_MM = [0.05, 0.075, 0.1]
DENSITIES = [0.4, 0.6]

# Random jitter as fraction of grid spacing.
# Paper-style bounded perturbation. 0.35 is a good compromise:
# enough to hide the grid, not so much that the pattern gets chaotic.
JITTER_FRAC = 0.35

# Set to an integer for repeatable output, or None for fresh randomness each run.
RANDOM_SEED = None

os.makedirs(OUTPUT_DIR, exist_ok=True)

# =========================
# HELPERS
# =========================
def mm_to_px(mm: float) -> int:
    return max(1, int(round(mm / 25.4 * DPI)))


def disk_mask(diameter_px: int):
    """
    Build a hard-edged binary disk mask for the requested diameter.
    Returns:
        mask: boolean array
        radius_pad: half-size padding used around the center
    """
    radius = diameter_px / 2.0
    half = math.ceil(radius)
    yy, xx = np.mgrid[-half:half + 1, -half:half + 1]
    mask = (xx.astype(np.float64) ** 2 + yy.astype(np.float64) ** 2) <= radius ** 2
    return mask, half


def nominal_spacing_px(dot_diam_px: int, target_density: float) -> int:
    """
    Use the paper-style idea: pick a mean spacing from nominal density.
    Since we are using hard binary circles instead of Gaussian spots,
    estimate spacing from:
        density ~= disk_area / spacing^2
    """
    radius = dot_diam_px / 2.0
    disk_area = math.pi * radius * radius
    spacing = math.sqrt(disk_area / target_density)
    return max(dot_diam_px, int(round(spacing)))


def make_jittered_grid_centers(width_px: int, height_px: int, spacing_px: int, jitter_frac: float):
    """
    Create a regular grid, then jitter each node by a bounded random offset.
    The jitter is bounded in both x and y by:
        +/- jitter_frac * spacing_px
    """
    if RANDOM_SEED is not None:
        random.seed(RANDOM_SEED)

    jitter_px = int(round(jitter_frac * spacing_px))

    # Put grid points near the centers of their cells
    x0 = spacing_px // 2
    y0 = spacing_px // 2

    xs = list(range(x0, width_px, spacing_px))
    ys = list(range(y0, height_px, spacing_px))

    centers = []

    total = len(xs) * len(ys)
    done = 0
    last_pct = -1

    print(f"  Grid spacing: {spacing_px} px")
    print(f"  Jitter bound: +/-{jitter_px} px")
    print(f"  Grid nodes: {total:,}")

    for y in ys:
        for x in xs:
            jx = random.randint(-jitter_px, jitter_px) if jitter_px > 0 else 0
            jy = random.randint(-jitter_px, jitter_px) if jitter_px > 0 else 0

            cx = x + jx
            cy = y + jy

            # Keep centers inside image bounds. Clamping is fine here.
            cx = min(max(cx, 0), width_px - 1)
            cy = min(max(cy, 0), height_px - 1)

            centers.append((cx, cy))

            done += 1
            pct = int(100 * done / total)
            if pct != last_pct and pct % 10 == 0:
                print(f"    center generation: {pct}% ({done:,}/{total:,})")
                last_pct = pct

    return centers


def render_pattern(width_px: int, height_px: int, dot_diam_px: int, spacing_px: int, jitter_frac: float):
    """
    Render a jittered-grid hard-dot pattern.
    """
    bitmap = np.ones((height_px, width_px), dtype=np.uint8)
    mask, half = disk_mask(dot_diam_px)
    centers = make_jittered_grid_centers(width_px, height_px, spacing_px, jitter_frac)

    total = len(centers)
    last_pct = -1

    print(f"  Rendering {total:,} hard dots...")

    for i, (cx, cy) in enumerate(centers, start=1):
        y0 = cy - half
        y1 = cy + half + 1
        x0 = cx - half
        x1 = cx + half + 1

        # Clip to image bounds
        iy0 = max(y0, 0)
        iy1 = min(y1, height_px)
        ix0 = max(x0, 0)
        ix1 = min(x1, width_px)

        # Corresponding region in mask
        my0 = iy0 - y0
        my1 = my0 + (iy1 - iy0)
        mx0 = ix0 - x0
        mx1 = mx0 + (ix1 - ix0)

        # Stamp black disk
        bitmap[iy0:iy1, ix0:ix1][mask[my0:my1, mx0:mx1]] = 0

        pct = int(100 * i / total)
        if pct != last_pct and pct % 10 == 0:
            print(f"    rendering: {pct}% ({i:,}/{total:,})")
            last_pct = pct

    return bitmap, len(centers)


def save_outputs(pattern_bitmap: np.ndarray, label_text: str, filename_base: str):
    """
    Save:
      - 1-bit TIFF
      - one-page PDF at 12in x 12in
    """
    # Full page with white label strip at bottom
    full_img = np.ones((PAGE_PX, PAGE_PX), dtype=np.uint8)
    full_img[:PATTERN_H_PX, :] = pattern_bitmap

    # Convert to true 1-bit image
    pil_img = Image.fromarray((full_img * 255).astype(np.uint8))
    pil_img = pil_img.point(lambda x: 255 if x > 0 else 0, mode='1')

    tiff_path = os.path.join(OUTPUT_DIR, filename_base + ".tiff")
    pil_img.save(tiff_path, compression="group4", dpi=(DPI, DPI))

    pdf_path = os.path.join(OUTPUT_DIR, filename_base + ".pdf")
    c = pdf_canvas.Canvas(pdf_path, pagesize=(PAGE_IN * inch, PAGE_IN * inch))

    # Place the raster image so it fills the full page
    c.drawInlineImage(
        pil_img,
        0,
        0,
        width=PAGE_IN * inch,
        height=PAGE_IN * inch
    )

    # Add readable vector label in the white strip
    c.setFont("Helvetica", 10)
    c.drawString(0.2 * inch, 0.08 * inch, label_text)

    c.showPage()
    c.save()

    return tiff_path, pdf_path


def measured_black_fraction(bitmap: np.ndarray) -> float:
    return float(np.mean(bitmap))


# =========================
# MAIN
# =========================
def main():
    print("BOS jittered-grid hard-dot generator")
    print(f"Output directory: {OUTPUT_DIR}")
    print(f"Page: {PAGE_IN} in x {PAGE_IN} in at {DPI} DPI")
    print(f"Pixel dimensions: {PAGE_PX} x {PAGE_PX}")
    print(f"Pattern area: {PATTERN_W_PX} x {PATTERN_H_PX}")
    print()

    for d_mm in DOT_SIZES_MM:
        for density in DENSITIES:
            dot_px = max(mm_to_px(d_mm), 3)
            spacing_px = nominal_spacing_px(dot_px, density)

            print("=" * 72)
            print(f"Generating pattern:")
            print(f"  Dot size: {d_mm:.3f} mm")
            print(f"  Dot diameter: {dot_px} px")
            print(f"  Nominal density request: {int(round(density * 100))}%")
            print(f"  Nominal spacing from method: {spacing_px} px")

            pattern, n_dots = render_pattern(
                width_px=PATTERN_W_PX,
                height_px=PATTERN_H_PX,
                dot_diam_px=dot_px,
                spacing_px=spacing_px,
                jitter_frac=JITTER_FRAC
            )

            frac_pattern = measured_black_fraction(pattern)
            frac_fullpage = (pattern.sum() / (PAGE_PX * PAGE_PX))

            label = (
                f"BOS | dot {d_mm:.3f} mm ({dot_px} px) | "
                f"nominal density {int(round(density * 100))}% | "
                f"spacing {spacing_px} px | jitter {JITTER_FRAC:.2f}"
            )

            fname = f"BOS_jittered_d{int(round(d_mm * 1000)):03d}um_cov{int(round(density * 100)):02d}pct"

            print(f"  Dots placed: {n_dots:,}")
            print(f"  Measured black fraction in pattern region: {100.0 * frac_pattern:.2f}%")
            print(f"  Measured black fraction over full page: {100.0 * frac_fullpage:.2f}%")
            print(f"  Saving: {fname}")

            tiff_path, pdf_path = save_outputs(pattern, label, fname)

            print(f"    wrote TIFF: {tiff_path}")
            print(f"    wrote PDF : {pdf_path}")
            print()

    print("Done.")


if __name__ == "__main__":
    main()