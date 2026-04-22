"""Translate a Klipper-flavoured G-code file into Prusa/Marlin-compatible G-code.

The Voron Pressure Advance test generator
(https://realdeuce.github.io/Voron/PA/pressure_advance.html) targets Klipper.
We want to print the same test on a Prusa MK4S (Marlin-based Buddy firmware) so
we can compare a visually-read optimal K against our custom M573 calibration.

This script performs a line-by-line translation:

  SET_PRESSURE_ADVANCE ADVANCE=<v> [SMOOTH_TIME=<t>] [EXTRUDER=<e>]
      -> M900 K<v>           (SMOOTH_TIME has no Marlin equivalent; dropped)

  SET_VELOCITY_LIMIT VELOCITY=<v>
      -> M203 X<v> Y<v>      (per-axis max feedrate; mm/s -> mm/min conversion
                              is NOT applied; the Voron generator emits Klipper
                              units which match Marlin M203 when X/Y are given
                              as mm/s — adjust --feedrate-units if your firmware
                              expects mm/min here)

  SET_VELOCITY_LIMIT ACCEL=<a>
      -> M204 P<a> R<a> T<a> (print, retract, travel accel)

  SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=...   -> dropped (no Marlin analogue)
  SET_VELOCITY_LIMIT ACCEL_TO_DECEL=...           -> dropped
  PRINT_START ...        -> commented out (replace upstream if you want a
                            Prusa-style start sequence; easiest is to paste
                            native Marlin start G-code into the Voron tool's
                            "Start G-code" box before generating)
  PRINT_END              -> commented out (same rationale)
  BED_MESH_CALIBRATE     -> G29
  BED_MESH_PROFILE ...   -> commented out (profiles don't map cleanly)
  G32                    -> G28        (Klipper's homing+Z-tilt helper)

Anything else that doesn't start with G/M/T (case-insensitive) is treated as a
Klipper extended command and is commented out with a 'WARN_UNTRANSLATED'
prefix so it shows up prominently when you skim the diff.

Numerical note: Marlin's M900 K-factor and Klipper's pressure_advance share
units (seconds of advance-per-mm/s of filament velocity) in their common
implementations, so a 1:1 transfer of the numerical value is a reasonable
first pass. The exact response curve differs between firmwares — the output
of this tool is a *starting* ground truth; you should still read the cleanest
line off the print visually.

Usage:
    python pa_dev/tools/klipper_to_marlin.py input.gcode output.gcode
    python pa_dev/tools/klipper_to_marlin.py input.gcode output.gcode --restore-k 0
    python pa_dev/tools/klipper_to_marlin.py input.gcode - --stats

The second form writes the translated G-code to stdout. --stats prints a
summary of what was translated/dropped/passed through.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --- Regex library ----------------------------------------------------------
# Order matters in translate_line(); first match wins.

RE_PA = re.compile(
    r"^\s*SET_PRESSURE_ADVANCE\b(?P<args>.*)$",
    re.IGNORECASE,
)
RE_PA_ARG_ADV = re.compile(r"\bADVANCE\s*=\s*(?P<v>-?\d+(?:\.\d+)?)", re.IGNORECASE)

RE_VELOC = re.compile(r"^\s*SET_VELOCITY_LIMIT\b(?P<args>.*)$", re.IGNORECASE)
RE_VELOC_VEL = re.compile(r"\bVELOCITY\s*=\s*(?P<v>-?\d+(?:\.\d+)?)", re.IGNORECASE)
RE_VELOC_ACC = re.compile(r"\bACCEL\s*=\s*(?P<v>-?\d+(?:\.\d+)?)", re.IGNORECASE)

RE_BED_MESH_CAL = re.compile(r"^\s*BED_MESH_CALIBRATE\b", re.IGNORECASE)
RE_BED_MESH_PROF = re.compile(r"^\s*BED_MESH_PROFILE\b", re.IGNORECASE)
RE_PRINT_START = re.compile(r"^\s*PRINT_START\b", re.IGNORECASE)
RE_PRINT_END = re.compile(r"^\s*PRINT_END\b", re.IGNORECASE)
RE_G32 = re.compile(r"^\s*G32\b", re.IGNORECASE)

# Detect "standard" G/M/T commands (we pass these through as-is).
RE_STANDARD_GM = re.compile(r"^\s*(G|M|T)\d+\b", re.IGNORECASE)
# A line is blank / pure comment if this matches.
RE_BLANK_OR_COMMENT = re.compile(r"^\s*(?:;.*)?$")


@dataclass
class Stats:
    lines_in: int = 0
    pa_translated: int = 0
    velocity_translated: int = 0
    bed_mesh_calibrate: int = 0
    commented_out: int = 0
    warned: int = 0
    standard_passthrough: int = 0
    blank_or_comment: int = 0
    warnings: list[str] = field(default_factory=list)


def translate_line(raw: str, stats: Stats) -> list[str]:
    """Return one or more output lines for a single input line.

    Preserve the original line as a trailing `; was: ...` comment whenever we
    mutate it, so the diff is self-documenting.
    """
    line = raw.rstrip("\r\n")

    if RE_BLANK_OR_COMMENT.match(line):
        stats.blank_or_comment += 1
        return [line]

    # --- SET_PRESSURE_ADVANCE ADVANCE=... [SMOOTH_TIME=...] --------------
    m = RE_PA.match(line)
    if m:
        adv = RE_PA_ARG_ADV.search(m.group("args"))
        if adv is None:
            stats.warned += 1
            stats.warnings.append(f"SET_PRESSURE_ADVANCE without ADVANCE=: {line!r}")
            return [f"; WARN_UNTRANSLATED {line}"]
        k = float(adv.group("v"))
        stats.pa_translated += 1
        return [f"M900 K{k:.4f}  ; was: {line.strip()}"]

    # --- SET_VELOCITY_LIMIT VELOCITY=... / ACCEL=... ---------------------
    m = RE_VELOC.match(line)
    if m:
        args = m.group("args")
        out_lines: list[str] = []
        v = RE_VELOC_VEL.search(args)
        a = RE_VELOC_ACC.search(args)
        if v is not None:
            vv = float(v.group("v"))
            out_lines.append(f"M203 X{vv:g} Y{vv:g}  ; was: {line.strip()}")
        if a is not None:
            aa = float(a.group("v"))
            out_lines.append(f"M204 P{aa:g} R{aa:g} T{aa:g}  ; was: {line.strip()}")
        if not out_lines:
            # Only dropped-args (SQUARE_CORNER_VELOCITY, ACCEL_TO_DECEL, ...)
            stats.commented_out += 1
            return [f"; dropped (no Marlin equivalent): {line.strip()}"]
        stats.velocity_translated += 1
        return out_lines

    # --- Bed mesh --------------------------------------------------------
    if RE_BED_MESH_CAL.match(line):
        stats.bed_mesh_calibrate += 1
        return [f"G29  ; was: {line.strip()}"]
    if RE_BED_MESH_PROF.match(line):
        stats.commented_out += 1
        return [f"; BED_MESH_PROFILE not translated: {line.strip()}"]

    # --- PRINT_START / PRINT_END macros ---------------------------------
    if RE_PRINT_START.match(line):
        stats.commented_out += 1
        return [
            f"; PRINT_START macro: replace with your Prusa start sequence",
            f"; was: {line.strip()}",
        ]
    if RE_PRINT_END.match(line):
        stats.commented_out += 1
        return [
            f"; PRINT_END macro: replace with your Prusa end sequence",
            f"; was: {line.strip()}",
        ]

    # --- G32 (Klipper homing+tilt) --------------------------------------
    if RE_G32.match(line):
        stats.commented_out += 1
        return [f"G28  ; was: {line.strip()}"]

    # --- Standard G/M/T commands: pass through ---------------------------
    if RE_STANDARD_GM.match(line):
        stats.standard_passthrough += 1
        return [line]

    # --- Unknown command: warn and comment out ---------------------------
    stats.warned += 1
    stats.warnings.append(f"Untranslated: {line.strip()}")
    return [f"; WARN_UNTRANSLATED {line}"]


def translate_stream(in_lines, out_fp, *, restore_k: float | None) -> Stats:
    stats = Stats()
    out_fp.write("; Translated from Klipper to Marlin by klipper_to_marlin.py\n")
    for raw in in_lines:
        stats.lines_in += 1
        for out in translate_line(raw, stats):
            out_fp.write(out + "\n")
    if restore_k is not None:
        out_fp.write(f"M900 K{restore_k:.4f}  ; restore linear-advance K-factor\n")
    return stats


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else None)
    ap.add_argument("input", help="Klipper G-code file to translate")
    ap.add_argument("output", help="Marlin G-code output path, or '-' for stdout")
    ap.add_argument(
        "--restore-k",
        type=float,
        default=None,
        help="If set, append 'M900 K<value>' at the end of output "
        "(e.g. --restore-k 0 to disable linear advance after the test)",
    )
    ap.add_argument("--stats", action="store_true", help="Print translation summary to stderr")
    args = ap.parse_args()

    in_path = Path(args.input)
    if not in_path.is_file():
        print(f"klipper_to_marlin: input not found: {in_path}", file=sys.stderr)
        return 2

    with open(in_path, "r", encoding="utf-8", errors="replace") as f:
        in_lines = f.readlines()

    if args.output == "-":
        stats = translate_stream(in_lines, sys.stdout, restore_k=args.restore_k)
    else:
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "w", encoding="utf-8", newline="\n") as out:
            stats = translate_stream(in_lines, out, restore_k=args.restore_k)
        print(f"wrote {out_path} ({stats.lines_in} input lines)", file=sys.stderr)

    if args.stats or stats.warned:
        print("--- klipper_to_marlin stats ---", file=sys.stderr)
        print(f"  lines in:                {stats.lines_in}", file=sys.stderr)
        print(f"  PA (M900 K) translated:  {stats.pa_translated}", file=sys.stderr)
        print(f"  velocity translated:     {stats.velocity_translated}", file=sys.stderr)
        print(f"  bed mesh calibrate:      {stats.bed_mesh_calibrate}", file=sys.stderr)
        print(f"  commented out / dropped: {stats.commented_out}", file=sys.stderr)
        print(f"  standard passthrough:    {stats.standard_passthrough}", file=sys.stderr)
        print(f"  blank / comment:         {stats.blank_or_comment}", file=sys.stderr)
        print(f"  warnings:                {stats.warned}", file=sys.stderr)
        for w in stats.warnings[:10]:
            print(f"    - {w}", file=sys.stderr)
        if len(stats.warnings) > 10:
            print(f"    ... and {len(stats.warnings) - 10} more", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
