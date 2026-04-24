"""Generate a Marlin-native Pressure Advance sweep test for a Prusa Nextruder.

Why a local generator (instead of Voron's web tool + klipper_to_marlin.py)?
- We emit Marlin G-code directly (no translation step, no SET_PRESSURE_ADVANCE).
- We can drop the result straight into `pa_gui.py`'s PA test preview tab and
  inspect each line's K-factor in the colour-mapped preview before printing.
- The CLI defaults are tuned for Prusa MK4S + Nextruder + Prusament PLA, but
  every knob is overridable.

Geometry (per K line, per layer):

    ┌── slow ──┬────── fast ──────┬── slow ──┐
    │ 25 mm    │ 30 mm             │ 25 mm   │
    │ 20 mm/s  │ 100 mm/s          │ 20 mm/s │

The PA-sensitive feature is the deceleration corner where `fast` meets the
trailing `slow` segment: over-advance pushes a bulge, under-advance leaves a
gap. By sweeping K across many lines, the cleanest corner corresponds to the
optimal K-factor. This is the same visual read-out as Ellis's / Voron's PA
calibration prints, but emitted as Marlin-native M900 K<v> commands so it runs
directly on Prusa-Firmware-Buddy without any translation layer.

Usage:
    python pa_dev/tools/generate_pa_test.py
    python pa_dev/tools/generate_pa_test.py --k-min 0.00 --k-max 0.1 --k-step 0.002
    python pa_dev/tools/generate_pa_test.py --output pa_dev/gcode/pa_test.gcode

The output file is written to `pa_dev/gcode/pa_test_<timestamp>.gcode` by
default (the folder is created if missing).
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT_DIR = REPO_ROOT / "pa_dev" / "gcode"


@dataclass
class PATestParams:
    # K-factor sweep (Marlin M900 K values, seconds-per-(mm/s) of filament feed).
    k_min: float = 0.000
    k_max: float = 0.080
    k_step: float = 0.002

    # Thermal + filament.
    hotend_c: int = 215
    bed_c: int = 60
    filament_mm: float = 1.75  # Prusa stock
    extrusion_multiplier: float = 1.0

    # Line geometry (mm). The fast segment must be long enough for the toolhead
    # to actually reach commanded speed given the accel setting below, or PA
    # won't have time to express itself.
    slow_len_mm: float = 25.0
    fast_len_mm: float = 30.0
    slow_feed_mm_s: float = 20.0
    fast_feed_mm_s: float = 100.0
    travel_feed_mm_s: float = 120.0
    first_layer_feed_mm_s: float = 20.0

    # Stack height. 3 layers × 0.2 mm gives enough wall for the bulge/gap to be
    # read by eye without wasting filament.
    layer_height_mm: float = 0.2
    layer_count: int = 3
    # 0.55 mm commanded line on a 0.4 mm nozzle (1.375× nozzle) keeps us within
    # safe flow but puts noticeably more material down than the earlier 0.45,
    # so the PA bulge/gap at the decel corner reads clearly by eye. Volumetric
    # rate at fast-segment speed (100 mm/s) is 0.55·0.2·100 = 11 mm³/s, which
    # is at the upper edge of what stock Prusament PLA can melt — good for
    # amplifying the PA effect without stalling the hotend.
    line_width_mm: float = 0.55
    nozzle_d_mm: float = 0.4

    # Line spacing in Y across the K sweep.
    line_spacing_mm: float = 3.0

    # Bed centring (Prusa MK4/MK4S build plate is ~250 × 210).
    bed_size_x: float = 250.0
    bed_size_y: float = 210.0
    center_x: float | None = None  # default: bed center
    center_y: float | None = None

    # Acceleration knobs — kept explicit so reviewers see the assumptions the
    # fast-segment length was sized against.
    print_accel: int = 2500
    retract_accel: int = 1500
    travel_accel: int = 3000
    # Small unretract between lines so oozing doesn't pollute the fast segment.
    retract_mm: float = 0.4
    retract_feed_mm_s: float = 40.0

    # Optional: reset M900 K0 at the end so nothing downstream inherits the
    # last K from the sweep.
    restore_k: float | None = 0.0

    # Embed an M573 loadcell-pressure capture at the start of the print. Every
    # PA test then ships with its own paired baseline in the same stream log
    # — no separate pa_capture.py run needed for comparison. The M573 block
    # reuses the positioning from pa_dev/preflights/preflight_mk4s.gcode, which
    # is the sequence we've verified runs clean (1536 samples, force 18..6025).
    include_m573: bool = True
    m573_park_x: float = 10.0
    m573_park_y: float = 5.0
    m573_park_z: float = 5.0


def k_values(p: PATestParams) -> list[float]:
    """Return the sweep K-factor list (inclusive of both endpoints)."""
    if p.k_step <= 0:
        raise ValueError("k_step must be > 0")
    n = int(round((p.k_max - p.k_min) / p.k_step)) + 1
    return [round(p.k_min + i * p.k_step, 6) for i in range(n)]


def extrusion_mm_per_mm(p: PATestParams) -> float:
    """mm of filament per mm of tool travel for a line at (w × layer_height)."""
    area_line = p.line_width_mm * p.layer_height_mm
    area_fil = math.pi * (p.filament_mm / 2.0) ** 2
    return (area_line / area_fil) * p.extrusion_multiplier


# Stick-font glyphs for first-layer K-value labels. Each glyph is a SINGLE
# polyline so we only retract/unretract once per character, which keeps
# oozing down. Coordinates live in a 2×4 unit cell (x ∈ [0,2], y ∈ [0,4]);
# _emit_label scales them into mm. Some glyphs have intentional retraces
# (e.g. "3" traces the middle bar twice) — minor double-extrusion but the
# label stays a single continuous stroke.
_GLYPHS: dict[str, list[tuple[float, float]]] = {
    "0": [(0, 0), (2, 0), (2, 4), (0, 4), (0, 0)],
    "1": [(0, 0), (2, 0), (1, 0), (1, 4), (0, 3)],
    "2": [(0, 4), (2, 4), (2, 2), (0, 2), (0, 0), (2, 0)],
    "3": [(0, 4), (2, 4), (2, 2), (0, 2), (2, 2), (2, 0), (0, 0)],
    "4": [(0, 4), (0, 2), (2, 2), (2, 4), (2, 0)],
    "5": [(2, 4), (0, 4), (0, 2), (2, 2), (2, 0), (0, 0)],
    "6": [(2, 4), (0, 4), (0, 0), (2, 0), (2, 2), (0, 2)],
    "7": [(0, 4), (2, 4), (2, 0)],
    "8": [(0, 4), (0, 0), (2, 0), (2, 4), (0, 4), (0, 2), (2, 2)],
    "9": [(0, 0), (2, 0), (2, 4), (0, 4), (0, 2), (2, 2)],
    ".": [(0.5, 0), (1.5, 0)],
}


def _emit_label(
    out: list[str],
    text: str,
    origin_x: float,
    origin_y: float,
    p: PATestParams,
    e_per_mm: float,
    glyph_w_mm: float = 3.0,
    glyph_h_mm: float = 4.0,
    gap_mm: float = 1.0,
) -> None:
    """Emit G-code to draw ``text`` as stick-font glyphs.

    Expects the filament to be retracted on entry; leaves it retracted on
    exit so the caller can travel immediately. Each character is drawn as
    one continuous stroke with a single retract/unretract pair.
    """
    pen_x = origin_x
    for ch in text:
        poly = _GLYPHS.get(ch)
        if poly is None:
            # Unknown glyph — skip its cell but still advance the pen so
            # the rest of the string stays aligned.
            pen_x += glyph_w_mm + gap_mm
            continue

        def _to_mm(pt: tuple[float, float]) -> tuple[float, float]:
            ux, uy = pt
            return (pen_x + (ux / 2.0) * glyph_w_mm,
                    origin_y + (uy / 4.0) * glyph_h_mm)

        pts = [_to_mm(u) for u in poly]

        # Travel to glyph start (filament still retracted).
        out.append(
            f"G1 X{pts[0][0]:.3f} Y{pts[0][1]:.3f} F{p.travel_feed_mm_s * 60:.0f}"
        )
        out.append(
            f"G1 E{p.retract_mm:.3f} F{p.retract_feed_mm_s * 60:.0f}  ; unretract (label)"
        )

        prev = pts[0]
        for cur in pts[1:]:
            seg_len = math.hypot(cur[0] - prev[0], cur[1] - prev[1])
            if seg_len < 1e-6:
                prev = cur
                continue
            out.append(
                f"G1 X{cur[0]:.3f} Y{cur[1]:.3f} "
                f"E{seg_len * e_per_mm:.4f} F{p.slow_feed_mm_s * 60:.0f}"
            )
            prev = cur

        out.append(
            f"G1 E-{p.retract_mm:.3f} F{p.retract_feed_mm_s * 60:.0f}  ; retract (label)"
        )
        pen_x += glyph_w_mm + gap_mm


def _emit_start(out, p: PATestParams, ks: list[float]) -> None:
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    out.append(f"; Prusa PA test sweep ({len(ks)} K values, K {ks[0]:.4f}..{ks[-1]:.4f} step {p.k_step:g})")
    out.append(f"; Generated {now} by pa_dev/tools/generate_pa_test.py")
    out.append(f"; Filament {p.filament_mm} mm, nozzle {p.nozzle_d_mm} mm, line {p.line_width_mm} mm × {p.layer_height_mm} mm")
    out.append(f"; Slow {p.slow_feed_mm_s} mm/s · Fast {p.fast_feed_mm_s} mm/s · Layers {p.layer_count}")
    out.append(";")
    out.append("; --- Start sequence ---")
    out.append(f"M140 S{p.bed_c}")
    out.append(f"M104 S{p.hotend_c}")
    out.append("G90                  ; absolute XYZ")
    out.append("M83                  ; relative E")
    out.append("G21                  ; mm")
    out.append("M107                 ; part-cooling fan off for first layer")
    out.append("G28                  ; home all axes")
    out.append("G29                  ; bed mesh / MBL (buddy firmware handles profile)")
    out.append(f"M190 S{p.bed_c}     ; wait for bed")
    out.append(f"M109 S{p.hotend_c}  ; wait for hotend")
    out.append(f"M204 P{p.print_accel} R{p.retract_accel} T{p.travel_accel}")

    # --- M573 baseline pressure capture ---
    # Runs with the hotend already at print temp and the bed homed/levelled.
    # Position matches preflight_mk4s.gcode (known-good) so the capture is
    # comparable across runs. The firmware prints BEGIN PA_CAPTURE / PA,ts,val
    # / END PA_CAPTURE — those lines end up in the stream log alongside this
    # G-code's TX trace, and can be extracted after the print.
    if p.include_m573:
        out.append("; --- M573 baseline pressure capture (pre-print) ---")
        out.append(f"G1 Z{p.m573_park_z:.3f} F600        ; raise Z for safety")
        out.append(f"G1 X{p.m573_park_x:.3f} Y{p.m573_park_y:.3f} F3000  ; park near front-left")
        out.append("M573                  ; capture loadcell-derived pressure curve (~5 s)")

    # Purge / prime line along the front of the bed.
    purge_y = p.bed_size_y * 0.05 + 5
    out.append(f"G1 Z{p.layer_height_mm:.3f} F600")
    out.append(f"G1 X20 Y{purge_y:.3f} F{p.travel_feed_mm_s * 60:.0f}")
    e_per_mm = extrusion_mm_per_mm(p)
    purge_len = 100.0
    out.append(f"G1 X{20 + purge_len:.1f} Y{purge_y:.3f} E{purge_len * e_per_mm:.4f} F{p.first_layer_feed_mm_s * 60:.0f}  ; purge line")
    out.append(f"G1 E-{p.retract_mm:.3f} F{p.retract_feed_mm_s * 60:.0f}  ; retract")
    out.append(f"G1 X{20 + purge_len + 10:.1f} F{p.travel_feed_mm_s * 60:.0f}  ; wipe")


def _emit_body(out, p: PATestParams, ks: list[float]) -> None:
    """Emit layer_count × len(ks) lines with M900 K<v> before each line."""
    cx = p.center_x if p.center_x is not None else p.bed_size_x / 2.0
    cy = p.center_y if p.center_y is not None else p.bed_size_y / 2.0

    total_line_x = p.slow_len_mm + p.fast_len_mm + p.slow_len_mm
    x_start = cx - total_line_x / 2.0
    x_mid1 = x_start + p.slow_len_mm
    x_mid2 = x_mid1 + p.fast_len_mm
    x_end = x_mid2 + p.slow_len_mm

    n_lines = len(ks)
    y_extent = (n_lines - 1) * p.line_spacing_mm
    y_start = cy - y_extent / 2.0

    e_per_mm = extrusion_mm_per_mm(p)

    # Label stride: pick a "nice" K interval (0.010, 0.005, 0.002, 0.001) that
    # lands roughly 5-12 labels across the sweep. For the default broad sweep
    # (0.000-0.080 step 0.002) that's 0.010 K → 9 labels; for a fine
    # zoomed-in sweep (0.035-0.070 step 0.001) it's 0.005 K → 8 labels.
    label_every: int | None = None
    for candidate_interval in (0.010, 0.005, 0.002, 0.001):
        every = int(round(candidate_interval / p.k_step))
        if every < 1:
            continue
        n_labels = (n_lines - 1) // every + 1
        if 5 <= n_labels <= 12:
            label_every = every
            break
    if label_every is None:
        label_every = max(1, (n_lines - 1) // 8)

    # Glyph metrics — picked so a 5-char "0.040" label is 19 mm wide × 4 mm
    # tall and comfortably clears adjacent rows at 3 mm spacing.
    glyph_w_mm = 3.0
    glyph_h_mm = 4.0
    glyph_gap_mm = 1.0
    label_text_sample = f"{ks[0]:.3f}"
    label_w_mm = (len(label_text_sample) * glyph_w_mm
                  + (len(label_text_sample) - 1) * glyph_gap_mm)

    for layer_i in range(p.layer_count):
        z = p.layer_height_mm * (layer_i + 1)
        out.append(f"; --- Layer {layer_i + 1} / {p.layer_count}  (Z={z:.3f}) ---")
        out.append(f"G1 Z{z:.3f} F600")
        # All layers print left-to-right so the PA corner (decel end of fast
        # segment) is always on the same side — makes visual comparison
        # across layers unambiguous.
        direction = 1
        iter_seq = list(enumerate(ks))
        for i, k in iter_seq:
            # When direction == -1, i indexes the reversed list; map back to the
            # real K list index so Y rows stay spatially the same across layers.
            row_idx = i if direction == 1 else (n_lines - 1 - i)
            y = y_start + row_idx * p.line_spacing_mm

            # Layer 1 only: stamp a K-value label to the left of the line.
            # Skipping layers 2/3 avoids thick/blobby labels and keeps the
            # overprinted PA line itself (layers 2-3) as the thing readers
            # measure.
            if layer_i == 0 and row_idx % label_every == 0:
                label_text = f"{k:.3f}"
                label_origin_x = x_start - 5.0 - label_w_mm  # 5 mm gap to PA line
                label_origin_y = y - glyph_h_mm / 2.0         # centre on the row
                out.append(f"; label {label_text} (K={k:.4f}) row {row_idx}")
                _emit_label(
                    out, label_text, label_origin_x, label_origin_y, p, e_per_mm,
                    glyph_w_mm=glyph_w_mm, glyph_h_mm=glyph_h_mm, gap_mm=glyph_gap_mm,
                )

            # Travel to the line start.
            out.append(f"M900 K{k:.4f}  ; row {row_idx}  layer {layer_i + 1}")
            if direction == 1:
                xa, xb, xc, xd = x_start, x_mid1, x_mid2, x_end
            else:
                xa, xb, xc, xd = x_end, x_mid2, x_mid1, x_start
            out.append(f"G1 X{xa:.3f} Y{y:.3f} F{p.travel_feed_mm_s * 60:.0f}")
            out.append(f"G1 E{p.retract_mm:.3f} F{p.retract_feed_mm_s * 60:.0f}  ; unretract")
            out.append(f"G1 X{xb:.3f} E{abs(xb - xa) * e_per_mm:.4f} F{p.slow_feed_mm_s * 60:.0f}")
            out.append(f"G1 X{xc:.3f} E{abs(xc - xb) * e_per_mm:.4f} F{p.fast_feed_mm_s * 60:.0f}")
            out.append(f"G1 X{xd:.3f} E{abs(xd - xc) * e_per_mm:.4f} F{p.slow_feed_mm_s * 60:.0f}")
            out.append(f"G1 E-{p.retract_mm:.3f} F{p.retract_feed_mm_s * 60:.0f}  ; retract")


def _emit_end(out, p: PATestParams) -> None:
    out.append("; --- End sequence ---")
    out.append(f"G1 Z{max(30.0, p.layer_height_mm * p.layer_count + 10):.2f} F600  ; lift")
    out.append("G1 X10 Y200 F6000    ; park near front")
    out.append("M104 S0              ; hotend off")
    out.append("M140 S0              ; bed off")
    out.append("M107                 ; fan off")
    out.append("M84                  ; steppers off")
    if p.restore_k is not None:
        out.append(f"M900 K{p.restore_k:.4f}  ; restore linear-advance K-factor")


def generate(p: PATestParams) -> str:
    ks = k_values(p)
    out: list[str] = []
    _emit_start(out, p, ks)
    _emit_body(out, p, ks)
    _emit_end(out, p)
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else None)
    ap.add_argument("--output", help="Output .gcode path (default: pa_dev/gcode/pa_test_<ts>.gcode)")
    ap.add_argument("--k-min", type=float, default=PATestParams.k_min, dest="k_min")
    ap.add_argument("--k-max", type=float, default=PATestParams.k_max, dest="k_max")
    ap.add_argument("--k-step", type=float, default=PATestParams.k_step, dest="k_step")
    ap.add_argument("--hotend", type=int, default=PATestParams.hotend_c, dest="hotend_c")
    ap.add_argument("--bed", type=int, default=PATestParams.bed_c, dest="bed_c")
    ap.add_argument("--slow", type=float, default=PATestParams.slow_feed_mm_s, dest="slow_feed_mm_s")
    ap.add_argument("--fast", type=float, default=PATestParams.fast_feed_mm_s, dest="fast_feed_mm_s")
    ap.add_argument("--slow-len", type=float, default=PATestParams.slow_len_mm, dest="slow_len_mm")
    ap.add_argument("--fast-len", type=float, default=PATestParams.fast_len_mm, dest="fast_len_mm")
    ap.add_argument("--layers", type=int, default=PATestParams.layer_count, dest="layer_count")
    ap.add_argument("--layer-height", type=float, default=PATestParams.layer_height_mm, dest="layer_height_mm")
    ap.add_argument("--spacing", type=float, default=PATestParams.line_spacing_mm, dest="line_spacing_mm")
    ap.add_argument("--line-width", type=float, default=PATestParams.line_width_mm, dest="line_width_mm")
    ap.add_argument("--extrusion-multiplier", type=float, default=PATestParams.extrusion_multiplier, dest="extrusion_multiplier")
    ap.add_argument("--center-x", type=float, default=None, dest="center_x")
    ap.add_argument("--center-y", type=float, default=None, dest="center_y")
    ap.add_argument("--no-m573", action="store_false", dest="include_m573",
                    help="Skip the M573 baseline capture at the start of the print")
    args = ap.parse_args()

    p = PATestParams(**{k: v for k, v in vars(args).items() if v is not None and k != "output"})
    ks = k_values(p)
    if len(ks) < 2:
        print("error: k sweep needs at least 2 values", file=sys.stderr)
        return 2

    if args.output:
        out_path = Path(args.output)
    else:
        DEFAULT_OUT_DIR.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = DEFAULT_OUT_DIR / f"pa_test_{ts}.gcode"

    out_path.parent.mkdir(parents=True, exist_ok=True)
    gcode = generate(p)
    out_path.write_text(gcode, encoding="utf-8", newline="\n")

    # Diagnostics to stderr (script's own "did-what" report).
    total_line_x = p.slow_len_mm + p.fast_len_mm + p.slow_len_mm
    y_extent = (len(ks) - 1) * p.line_spacing_mm
    e_per_mm = extrusion_mm_per_mm(p)
    total_extrude = (
        total_line_x * e_per_mm * len(ks) * p.layer_count
        + 100.0 * e_per_mm  # purge line
    )
    print(f"wrote {out_path}", file=sys.stderr)
    print(f"  K values: {len(ks)}  ({ks[0]:.4f}..{ks[-1]:.4f} step {p.k_step:g})", file=sys.stderr)
    print(f"  layers:   {p.layer_count}", file=sys.stderr)
    print(f"  per-line: slow {p.slow_len_mm:g} / fast {p.fast_len_mm:g} / slow {p.slow_len_mm:g} mm  "
          f"(total X = {total_line_x:g} mm, Y extent = {y_extent:g} mm)", file=sys.stderr)
    print(f"  extrusion total: {total_extrude:.2f} mm filament (≈{total_extrude * math.pi * (p.filament_mm/2)**2 / 1000:.2f} cm³)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
