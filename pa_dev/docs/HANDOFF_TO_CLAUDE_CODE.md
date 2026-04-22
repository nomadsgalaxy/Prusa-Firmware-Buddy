# Handoff: finish MK4/MK4S firmware build (PA auto-calibration prototype)

## TL;DR
Code changes are complete for a new `M573` G-code that captures loadcell samples during a scripted slow→fast→slow extrusion pattern and dumps them as CSV. The initial build attempt failed because only Python 3.14 is on this machine and the repo pins `numpy==1.26.4` (no 3.14 wheel). You have an admin terminal — install Python 3.12, delete the stale venv, re-run the build, and confirm the `.bbf` is produced. No source code changes needed.

## Repo
- Path: `C:\Users\Antdr\OneDrive\Computers\Work Computers\Prusa\Documents\Claude\Prusa\Prusa-Firmware-Buddy`
- Base: Prusa-Firmware-Buddy v6.5.3 (clean, already synced with git)

## Target
- `python utils/build.py --preset mk4 --build-type debug`
- Expected output: `build/products/<something>.bbf`
- The user will flash via their USB-connected MK4/MK4S and run `M573` from a serial terminal to capture CSV output between `BEGIN PA_CAPTURE` / `END PA_CAPTURE` markers.

## Known environment state
- Python 3.14.3 at `C:\Python314\python.exe` — **too new**, this is the blocker.
- `numpy==1.26.4` (pinned in `requirements.txt`) has no CP314 wheel → pip falls through to source build → fails.
- A partial venv already exists at `<repo>\.venv` (created with 3.14). Delete it before re-running.
- Build logs from the failed attempt: `<repo>\build_mk4.log`, `<repo>\build_mk4.err.log`.
- Chocolatey is installed at `C:\ProgramData\chocolatey\bin\choco.exe`.
- Do NOT unpin `numpy` or other requirement lines — other pins (`littlefs-python==0.8`, `pillow~=10.4`, `pip~=23.0`) were also released before 3.14 and may have the same issue. Fix the Python version, not the pins.

## Plan
1. **Install Python 3.12.** In the admin terminal:
   ```powershell
   choco install python312 -y
   ```
   Confirm it landed (typical path): `C:\Python312\python.exe`. Verify with `& 'C:\Python312\python.exe' --version`.
2. **Delete the stale venv:**
   ```powershell
   Remove-Item -Recurse -Force 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Prusa\Documents\Claude\Prusa\Prusa-Firmware-Buddy\.venv'
   ```
3. **Re-run the build with Python 3.12:**
   ```powershell
   Set-Location -LiteralPath 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Prusa\Documents\Claude\Prusa\Prusa-Firmware-Buddy'
   & 'C:\Python312\python.exe' utils\build.py --preset mk4 --build-type debug
   ```
   First run bootstraps the venv, fetches the ARM toolchain + ninja + cmake into `.dependencies\`, then compiles. Expect 10–20 minutes. If it hangs on a toolchain download, retry — the fetches are idempotent.
4. **Verify the firmware binary:**
   - `Get-ChildItem '<repo>\build\products\' -Recurse -Filter '*.bbf'` should show a `.bbf` file.
   - Summarize: path, size, and mtime, so the user knows where to point PrusaSlicer / the flasher.

## What to do if the build fails
- If there's a C++ compile error, it's almost certainly in one of the files I added below. Report the error verbatim — do NOT start "fixing" things without confirming the diagnosis.
- If pip still fails on a package even with Python 3.12, read `requirements.txt` and the pip error carefully before suggesting changes. Prefer installing missing build tools (ninja, MSVC build tools) over modifying pins.
- `BUDDY_NO_VIRTUALENV=1` exists as an escape hatch but **don't use it** — it pollutes the system Python.

## Code changes already in place (do NOT re-write these)
All added/modified for the signal-capture prototype of loadcell-based Pressure Advance auto-calibration. Guarded by `HAS_LOADCELL()` — no impact on MINI/MK3.5 builds.

**New files**
- `src/common/pa_calibration.hpp` — Capture singleton, ISR-safe sample ring (1536 slots ≈ 4.8 s @ 320 Hz), 8 phase marks.
- `src/common/pa_calibration.cpp` — Lock-free `StoreSample` via `head_.fetch_add`; `DumpToSerial` yields every 64 samples via `idle(true)`.
- `lib/Marlin/Marlin/src/gcode/feature/pressure_advance/M573.cpp` — Orchestrates tare → arm → 3-phase scripted extrude → stop → dump. Per-printer purge-zone geometry (MK4, Core One / Core One+, XL) resolved at compile time via `geom()`.

**Modified files**
- `src/common/loadcell.cpp` — added `#include "pa_calibration.hpp"` and an ISR hook after `tared_z_load` computation: `if (auto &pa_cap = pa_calibration::Capture::instance(); pa_cap.IsActive()) { pa_cap.StoreSample(tared_z_load, time_us); }`. Cost when inactive: one relaxed atomic load.
- `src/common/CMakeLists.txt` — added `pa_calibration.cpp` to the `HAS_LOADCELL` target_sources block.
- `lib/AddMarlin.cmake` — added `HAS_LOADCELL` block inside `BOARD_IS_MASTER_BOARD` scope with `M573.cpp`.
- `lib/Marlin/Marlin/src/gcode/gcode.h` — declared `static void M573();` under `#if HAS_LOADCELL()`.
- `lib/Marlin/Marlin/src/gcode/gcode.cpp` — dispatch case 573 under `#if HAS_LOADCELL()`.

## Output format M573 produces on serial
```
BEGIN PA_CAPTURE
PA_SAMPLES=<n> PA_DROPPED=<m>
PA_PHASE,start,<time_us>
PA_PHASE,slow_baseline,<time_us>
PA_PHASE,fast_step,<time_us>
PA_PHASE,slow_decay,<time_us>
PA_PHASE,end,<time_us>
PA,<time_us>,<load_g>
... (~1500 rows) ...
END PA_CAPTURE
```

## After the build succeeds
Report back:
- `.bbf` path + size.
- Any warnings in the final compile stage that mention `pa_calibration`, `M573`, or `loadcell.cpp` — these are the new code's blast radius.
- Commit is NOT needed. The user will review and commit via GitHub Desktop.

Do not push, do not commit, do not touch branches.
