# PA K-Sweep Session — 2026-04-24

## What this session accomplished

Completed a 31-row Pressure Advance K-sweep on a Prusa MK4S using the custom
`M573` loadcell-capture firmware and two tooling scripts (`generate_pa_test.py`,
`stream_gcode.py`). Identified optimal K-factor range for the machine.

---

## Setup

- **Printer:** Prusa MK4S, Buddy firmware 6.5.3+11985.LOCAL
- **Filament:** Prusament PLA 1.75 mm, 215 °C / 60 °C bed
- **Serial:** COM5, 115200 baud
- **Sweep:** K = 0.040 → 0.130, step 0.003 (31 rows), 3 layers × 0.2 mm
- **Line geometry:** slow 25 mm @ 20 mm/s → fast 30 mm @ 100 mm/s → slow 25 mm @ 20 mm/s
- **Stream log:** `pa_dev/runs/stream_20260424_094954.log`

---

## Issues encountered and resolutions

### 1. UnicodeEncodeError on M573 serial output

**Symptom:** `stream_gcode.py` crashed with `UnicodeEncodeError: 'charmap' codec
can't encode characters` when the printer sent the M573 temperature echo
containing `°C` (UTF-8 bytes 0xC2 0xB0).

**Root cause:** `read_line_blocking` decoded bytes as ASCII with
`errors="replace"`, producing U+FFFD replacement characters. Windows stdout
defaults to cp1252, which cannot encode U+FFFD.

**Fix (`stream_gcode.py`):**
- Reconfigure stdout to UTF-8 at startup:
  ```python
  if hasattr(sys.stdout, "reconfigure"):
      sys.stdout.reconfigure(encoding="utf-8", errors="replace")
  ```
- Change `read_line_blocking` decode from ASCII to UTF-8 (both return sites).

---

### 2. M573 left TareMode::Continuous active

**Symptom:** During initial testing, potential false crash detection on F7200
travel moves after M573 ran.

**Root cause:** `M573.cpp` called `loadcell.Tare(TareMode::Continuous)` to
enable the sensitive threshold (-40 g) during capture, but never restored
`TareMode::Static` (-125 g) afterward. Any high-acceleration travel after M573
could exceed the 3× more sensitive threshold and trigger crash detection.

**Fix (`M573.cpp`):** Added `loadcell.Tare()` (default = Static) at the end of
`M573()`, after `cap.DumpToSerial()`, to restore the normal operating threshold
before returning.

---

### 3. Print stalling at TX 128 — split `ok` response (main blocker)

**Symptom:** The print consistently stopped after row 6 of the PA test
(approximately 10 minutes in). The printer LCD showed normal end-of-print
behaviour (returned to home screen, steppers disengaged). The nozzle was
confirmed at X:61.00 Y:76.00 Z:0.20 via M114.

**Root cause:** The Prusa Buddy firmware sends the `ok` acknowledgement for
certain G1 travel moves as two separate newline-terminated serial writes:
`"o\r\n"` followed by `"k\r\n"`, rather than a single `"ok\r\n"`. The
`read_line_blocking` function returns each as a separate string (`"o"` and `"k"`
respectively). `RE_OK = re.compile(r"^\s*ok\b")` matches neither string alone.
`stream_gcode.py` waited indefinitely for an ack. After ~4 minutes of silence,
the printer's serial idle timeout fired, sent `"Done printing file"`, and ran
the end-of-print park sequence.

This was visible in the stream log:
```
TX 128/1044   G1 X61.000 Y76.000 F7200
RX        << o
RX        << k
PROGRESS 128/1044 (12.3%) ETA 0:32:09
PROGRESS 128/1044 (12.3%) ETA 0:32:53
PROGRESS 128/1044 (12.3%) ETA 0:33:37
PROGRESS 128/1044 (12.3%) ETA 0:34:21
RX        << Done printing file
RX        << echo:enqueueing "G90"
...
```

**Fix (`stream_gcode.py`):** Added a `pending_partial_ok` flag in the ack-wait
loop. If `rx.strip() == "o"`, set the flag and continue. If `rx.strip() == "k"`
and the flag is set, rewrite `rx = "ok"` before the regex test. The same guard
was added to the handshake loop.

```python
if rx_stripped == "o":
    pending_partial_ok = True
    continue
if rx_stripped == "k" and pending_partial_ok:
    rx = "ok"
pending_partial_ok = False
if RE_OK.match(rx):
    got_ok = True
    break
```

---

## Results

Full 31-row sweep completed without error in **10 minutes 21 seconds**
(1044 sendable lines, exit code 0).

**Visual inspection:** rows K=0.055 and K=0.060 produced the cleanest corner
definition at the decel end of the fast segment. Lines below ~0.050 show
under-advance gaps; lines above ~0.065 show over-advance bulge.

**Recommended K-factor:** start with **K=0.057** or print a fine zoom sweep
(e.g. `--k-min 0.050 --k-max 0.065 --k-step 0.001`) to confirm.

---

## Fixes applied to tools (for future runs)

### `pa_dev/tools/stream_gcode.py`
- UTF-8 stdout reconfiguration (startup)
- `read_line_blocking` decodes as UTF-8 instead of ASCII
- Split-ok handling in both handshake loop and ack-wait loop

### `pa_dev/tools/generate_pa_test.py`
- **Layer direction:** previously snaked alternate layers left↔right to
  "minimise accel bias." In practice this makes visual comparison across layers
  ambiguous because the PA-sensitive decel corner flips sides each layer.
  Changed to always print left-to-right so the corner is on the same side for
  every layer.

### `lib/Marlin/Marlin/src/gcode/feature/pressure_advance/M573.cpp`
- `loadcell.Tare()` call at end of M573 to restore `TareMode::Static`

---

## Suggested next steps

1. **Fine sweep** around the winner:
   ```bash
   python pa_dev/tools/generate_pa_test.py \
       --k-min 0.050 --k-max 0.065 --k-step 0.001
   python pa_dev/tools/stream_gcode.py COM5 pa_dev/gcode/pa_test_<ts>.gcode --quiet-rx
   ```
2. **Set K in slicer / EEPROM** once confirmed (M900 K0.057 or whichever wins
   the fine sweep).
3. **M573 data analysis:** the stream log contains a full `BEGIN PA_CAPTURE …
   END PA_CAPTURE` block at TX 15. Extract it for loadcell-curve comparison
   against future prints.
