# pa_dev/

Scratch/workspace for the Pressure Advance auto-calibration (`M573`) work.
Everything under this folder is development-only — not part of the upstream
Prusa firmware tree.

Layout:

- `preflights/` — hand-written G-code snippets we send before a capture.
  - `preflight_mk4s.gcode` — heat + home + position MK4S before `M573`.
  - `capture_preflight.gcode` — silences `M155` temp reports so the CSV is
    clean during the `M573` run.
- `captures/` — raw printer output from `M573` runs, one `.log` per capture.
  `pa_gui.py` writes new captures here automatically with a timestamped
  filename (`pa_capture_YYYYMMDD_HHMMSS.log`).
- `runs/` — stdout/stderr from the helper scripts (`pa_capture.py`,
  `serial_bridge.py`). Useful for debugging a run that went sideways.
- `docs/` — short design notes and handoff documents.
- `tools/` — stand-alone helper scripts for this workspace (e.g.
  `klipper_to_marlin.py`, which post-processes Voron PA-test G-code into
  Prusa/Marlin-compatible form for ground-truth validation of `M573`).
  Test fixtures live alongside under `tools/fixtures/`.

The supporting tooling lives in the repo's normal `utils/` folder:
`utils/pa_capture.py` (one-shot CLI capture), `utils/pa_gui.py` (interactive
dev UI), `utils/serial_bridge.py` (dumb serial bridge for ad-hoc commands).
