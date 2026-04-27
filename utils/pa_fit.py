#!/usr/bin/env python3
"""Off-line K estimator for M573 PA captures.

Reads `.log` files produced by utils/pa_gui.py (or raw M573 console output
sandwiched between `BEGIN PA_CAPTURE` / `END PA_CAPTURE` markers) and fits a
first-order time constant τ to the fast_1 / fast_2 step responses.

We estimate τ two ways:

  1. **63 % threshold** — find t where F(t) first crosses
     F_0 + 0.632 · (F_∞ − F_0). This is the method that will eventually run
     inside Marlin on the MK4S (STM32F427, Cortex-M4F with FPU). Zero dynamic
     memory, no log/exp math, one forward pass over the samples. This is the
     authoritative number.

  2. **Log-linear regression** — fit  ln|F_∞ − F(t)| = a − t/τ  over the first
     ~3τ of the rise. Used as an off-line cross-check only; not intended to
     be ported to firmware.

The ground truth for our calibration filament comes from the Marlin PA tower
test (visual): K ≈ 0.055 for PLA. Agreement to within ±0.010 s between the two
methods, and vs. the visual tower, is acceptable for this prototype stage.

Usage:
    python utils/pa_fit.py path/to/pa_capture_YYYYMMDD_HHMMSS.log
    python utils/pa_fit.py --ground-truth 0.055 path/to/capture.log
    python utils/pa_fit.py --self-test
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# ---- Parsing ---------------------------------------------------------------

# Captures come from two sources:
#   (a) utils/pa_gui.py, which writes clean `PA,ts,load` / `PA_PHASE,name,ts`
#       lines with no prefix;
#   (b) raw serial-console dumps (e.g. Pronsole / any terminal logger), where
#       every received line is prefixed with something like "RX        << ".
# Accept both by making any leading non-`P` characters optional.
_PFX = r"^.*?(?=PA[,_])"  # lazily skip any prefix up to the PA token
SAMPLE_RE = re.compile(_PFX + r"PA,(?P<ts>\d+),(?P<load>-?\d+(?:\.\d+)?)\s*$")
PHASE_RE = re.compile(_PFX + r"PA_PHASE,(?P<name>[^,]+),(?P<ts>\d+)\s*$")
_SAMPLES_LINE_RE = re.compile(r".*?\bPA_SAMPLES=(\d+)\s+PA_DROPPED=(\d+)")
_BEGIN_RE = re.compile(r".*?\bBEGIN PA_CAPTURE\b")
_END_RE = re.compile(r".*?\bEND PA_CAPTURE\b")


@dataclass
class Capture:
    t_us: list[int] = field(default_factory=list)
    load_g: list[float] = field(default_factory=list)
    # First occurrence of each phase name → timestamp_us. Phase names are
    # unique in the current M573 output (pulse1_start, zero_flow_1, slow_in_1,
    # fast_1, slow_out_1, pulse1_end, purge2_start, purge2_end, pulse2_start,
    # slow_in_2, fast_2, slow_out_2, pulse2_end) so a dict is fine; if the
    # firmware ever reuses a name we'll need a list instead.
    phases: dict[str, int] = field(default_factory=dict)
    sample_count: Optional[int] = None
    dropped_count: Optional[int] = None


def parse_capture(path: Path) -> Capture:
    cap = Capture()
    in_block = False
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if _BEGIN_RE.match(line):
                in_block = True
                continue
            if _END_RE.match(line):
                in_block = False
                continue
            if not in_block:
                continue
            m_n = _SAMPLES_LINE_RE.match(line)
            if m_n:
                cap.sample_count = int(m_n.group(1))
                cap.dropped_count = int(m_n.group(2))
                continue
            m = PHASE_RE.match(line)
            if m:
                name = m.group("name")
                if name not in cap.phases:
                    cap.phases[name] = int(m.group("ts"))
                continue
            m = SAMPLE_RE.match(line)
            if m:
                cap.t_us.append(int(m.group("ts")))
                cap.load_g.append(float(m.group("load")))
    return cap


# ---- Fit -------------------------------------------------------------------


@dataclass
class OnsetDetection:
    """Outcome of the dF/dt onset detector for one fast phase."""
    t_us: int            # effective onset timestamp (phase_mark or detected)
    method: str          # "disabled" | "phase_mark_aligned" | "signal_dip"
                         #   | "no_step" | "phase_mark_fallback"
    offset_ms: float     # (t_us - phase_mark_us) / 1000
    dip_depth_g: float   # max |F - F_0| in the "wrong" direction
                         # (i.e. how far the signal dropped below F_0
                         # before the rise — 0 if no dip)
    F_start: Optional[float] = None
                         # effective starting value for the rise — equals
                         # (F_0 ∓ dip_depth_g) when a real dip was detected,
                         # None when the detector fell through to phase_mark
                         # (fit should keep using slow-phase F_0 in that case)


@dataclass
class PulseFit:
    pulse: str                       # "1" or "2"
    F_0: float                       # pre-step load (g) — last N of slow_in
    F_inf: float                     # plateau load (g) — last 30% of fast
    dF: float                        # F_inf - F_0
    baseline_g: Optional[float]      # mean of zero_flow_1 (g)
    baseline_sigma_g: Optional[float]
    tau_threshold_s: Optional[float] # authoritative (firmware-portable)
    tau_loglin_s: Optional[float]    # cross-check
    n_samples_slow: int
    n_samples_fast: int
    n_samples_loglin: int            # how many points went into the regression
    onset: OnsetDetection = field(
        default_factory=lambda: OnsetDetection(0, "disabled", 0.0, 0.0))


def _mean_last(seg: list[tuple[int, float]], n: int) -> float:
    """Mean of the last n samples of `seg` (falls back to whole seg)."""
    if not seg:
        return 0.0
    tail = seg[-n:] if n < len(seg) else seg
    return sum(v for _, v in tail) / len(tail)


def _find_plateau_F_inf(fast_seg: list[tuple[int, float]]
                        ) -> tuple[float, int, float]:
    """Find F_inf as the mean of the flattest post-rise window in fast_seg.

    Background: naïve F_inf = mean(last 30 % of fast) is biased whenever the
    trailing ~50 ms of the fast window includes the extruder-decel dip
    (nozzle pressure releases as feedrate ramps from cruise to 0 at the
    end of the move). On the MK4S bed-contact pulse that dip pulls F_inf
    LOW by ~200 g; on the free-air pulse a different edge effect pulls it
    HIGH by ~50 g. The bias propagates asymmetrically: it barely moves τ_thr
    but catastrophically biases τ_loglin (factor of 2–3×) because log(r)
    diverges as r → 0. Plateau detection sidesteps both.

    Algorithm: slide a W-sample window across fast_seg, pick the window
    with minimum stddev whose mean is in the upper half of the seg's
    value range (this gates against picking a pre-rise flat region if the
    signal barely rose). W = clamp(n // 5, 16, 40) — ~50 to ~125 ms at
    320 Hz, matching the ≥180 ms plateau window that B1 geometry delivers.

    Returns (plateau_mean, start_index, plateau_std). If fast_seg is too
    short or no window qualifies, falls back to last-30 % naïve and
    returns (naïve_mean, -1, inf) so the caller can detect the fallback.
    """
    n = len(fast_seg)
    fallback_n = max(10, int(n * 0.3))
    fallback = _mean_last(fast_seg, fallback_n) if fallback_n else 0.0
    MIN_W, MAX_W = 16, 40
    if n < 2 * MIN_W:
        return (fallback, -1, float("inf"))

    W = max(MIN_W, min(MAX_W, n // 5))
    vals = [v for _, v in fast_seg]
    v_min = min(vals)
    v_max = max(vals)
    rise_gate = v_min + 0.5 * (v_max - v_min)

    # O(1) window stats via cumulative sums.
    cs = [0.0] * (n + 1)
    cs2 = [0.0] * (n + 1)
    for i, (_, v) in enumerate(fast_seg):
        cs[i + 1] = cs[i] + v
        cs2[i + 1] = cs2[i] + v * v

    best_std = float("inf")
    best_start = -1
    best_mean = fallback
    for i in range(n - W + 1):
        m = (cs[i + W] - cs[i]) / W
        if m < rise_gate:
            continue
        var = max(0.0, (cs2[i + W] - cs2[i]) / W - m * m)
        std = math.sqrt(var)
        if std < best_std:
            best_std = std
            best_start = i
            best_mean = m

    if best_start < 0:
        return (fallback, -1, float("inf"))
    return (best_mean, best_start, best_std)


def _slice(samples: list[tuple[int, float]],
           t0_us: int, t1_us: int) -> list[tuple[int, float]]:
    # Samples are time-ordered; a linear scan is cheaper than bisect for the
    # sample counts we see here (~1500 rows), and keeps the code firmware-
    # portable: the Marlin port will consume samples in order from the ring
    # buffer, so there's no need for random-access indexing.
    return [(t, v) for t, v in samples if t0_us <= t < t1_us]


def _baseline_stats(samples: list[tuple[int, float]],
                    cap: Capture) -> tuple[Optional[float], Optional[float]]:
    """Mean and sample-stddev of the zero_flow_1 window, if present."""
    if "zero_flow_1" not in cap.phases:
        return (None, None)
    # zero_flow_1 ends at slow_in_1 (the next phase in order).
    t0 = cap.phases["zero_flow_1"]
    t1 = cap.phases.get("slow_in_1", t0 + 200_000)  # 200 ms fallback
    seg = _slice(samples, t0, t1)
    if len(seg) < 8:
        return (None, None)
    vals = [v for _, v in seg]
    m = sum(vals) / len(vals)
    var = sum((v - m) ** 2 for v in vals) / (len(vals) - 1)
    return (m, math.sqrt(var))


def _moving_avg(window: list[tuple[int, float]],
                m: int) -> list[tuple[int, float]]:
    """Centered M-point moving average (timestamps preserved).

    Used to knock the baseline noise down before the onset detector looks
    for the slow→fast dip. On MK4S the raw σ_baseline is ~400 g and the
    physical dip is only ~140 g, so the detector has to operate on a
    smoothed trace or the dip disappears in the noise.

    Firmware-portable: a ring-buffer sliding sum is a two-line addition to
    the existing pa_calibration accumulator — M·sample size = 9·4 B = 36 B.
    """
    n = len(window)
    if n == 0 or m <= 1:
        return list(window)
    out: list[tuple[int, float]] = []
    half = m // 2
    for i in range(n):
        lo = max(0, i - half)
        hi = min(n, i + half + 1)
        s = 0.0
        for j in range(lo, hi):
            s += window[j][1]
        out.append((window[i][0], s / (hi - lo)))
    return out


def _detect_fast_onset(fast_seg: list[tuple[int, float]],
                       phase_mark_us: int,
                       F_0: float,
                       F_inf_est: float,
                       baseline_sigma: Optional[float],
                       search_lookahead_us: int = 100_000,
                       max_plausible_offset_us: int = 80_000,
                       smooth_window: int = 5,
                       dip_min_frac_of_dF: float = 0.03
                       ) -> OnsetDetection:
    """Find the real motion-onset timestamp of a fast phase.

    **Why this exists:** M573's MarkPhase(name, ticks_us()) runs on the gcode
    thread right before the move is queued — not when the stepper actually
    starts executing it. On MK4S the planner queue is several moves deep,
    so the fast_N PA_PHASE timestamp leads real motion by ~40 ms (see
    .auto-memory/m573_phase_mark_queue_lag.md). Any τ fit that uses the
    phase-mark timestamp as t=0 is biased — the first 40 ms of the "fast"
    window are actually the tail of the previous (slow) move, and there's
    a pressure-release dip right at the real transition. We fix it on the
    signal side (cheap, no firmware change) by finding the dip minimum.

    **Algorithm:**
     1. Lightly smooth the trace (5-sample moving average) — enough to
        knock down single-sample noise spikes, not enough to drag the
        ramp minimum earlier (wider windows bleed rise values into the
        ramp-bottom average and attenuate the dip).
     2. Walk forward from phase_mark tracking the running minimum (for
        ascending step) or maximum (descending). Stop when the smoothed
        signal crosses F_0 ± 20 % of dF (clearly past the onset).
     3. If the extremum is ≥ 3 % of |dF| in the "wrong" direction from
        F_0, call it a real dip and return its timestamp. Otherwise the
        dip is indistinguishable from noise → return phase_mark.
     4. Sanity cap: if the detected offset exceeds max_plausible_offset_us
        (80 ms by default, comfortably above the ~40 ms M573 lag),
        something is wrong (noise is pulling the min into a late trough)
        → fall back to phase_mark.

    The 3 %-of-dF threshold is chosen over a σ-based one because real
    MK4S captures have σ_baseline comparable to dip depth — σ-gating
    rejects real dips on those traces.

    **Firmware portability:** one forward pass, two floats of state
    (ext_v, ext_t) plus the 5-sample sliding sum. No random access.
    Translates to ≈15 lines of C in pa_calibration.

    Returns an OnsetDetection with the chosen timestamp and a method tag
    so the caller can log how much correction was applied.
    """
    dF = F_inf_est - F_0
    if abs(dF) < 1e-6 or len(fast_seg) < smooth_window + 4:
        return OnsetDetection(phase_mark_us, "no_step", 0.0, 0.0)

    ascending = dF > 0
    smoothed = _moving_avg(fast_seg, smooth_window)

    rise_delta = 0.20 * abs(dF)
    rise_target = F_0 + (rise_delta if ascending else -rise_delta)
    search_end = phase_mark_us + search_lookahead_us

    ext_v = F_0
    ext_t = phase_mark_us
    found_ext = False
    for t, v in smoothed:
        if t > search_end:
            break
        if ascending:
            if v < ext_v:
                ext_v = v
                ext_t = t
                found_ext = True
            if v >= rise_target:
                break
        else:
            if v > ext_v:
                ext_v = v
                ext_t = t
                found_ext = True
            if v <= rise_target:
                break

    dip_depth = (F_0 - ext_v) if ascending else (ext_v - F_0)
    dip_depth = max(0.0, dip_depth)

    # Significance gate: require dip ≥ 3 % of |dF|. Rationale in docstring.
    min_dip = dip_min_frac_of_dF * abs(dF)
    if not found_ext or dip_depth < min_dip:
        return OnsetDetection(phase_mark_us, "phase_mark_aligned",
                              0.0, dip_depth, F_start=None)

    # Sanity cap: the M573 queue-time lag is ~40 ms. If we detected an
    # onset more than 80 ms after the phase mark, noise is probably
    # dragging us into a late trough; better to trust the phase mark.
    offset_us = ext_t - phase_mark_us
    if offset_us > max_plausible_offset_us or offset_us < 0:
        return OnsetDetection(phase_mark_us, "phase_mark_fallback",
                              offset_us / 1000.0, dip_depth, F_start=None)

    # F_start = the smoothed dip value. Downstream fit uses this in place
    # of the slow-phase F_0 so the 63 % threshold is measured from the
    # actual starting point of the rise, not from 0.5 g above it.
    return OnsetDetection(ext_t, "signal_dip",
                          offset_us / 1000.0, dip_depth, F_start=ext_v)


def _threshold_cross(fast_seg: list[tuple[int, float]],
                     t_fast_start: int,
                     F_0: float, F_inf: float) -> Optional[float]:
    """Return time (s) at which F(t) first crosses F_0 + 0.632·(F_inf−F_0)."""
    dF = F_inf - F_0
    if abs(dF) < 1e-6 or not fast_seg:
        return None
    target = F_0 + 0.6321205588 * dF  # 1 − 1/e
    ascending = dF > 0
    for t, v in fast_seg:
        crossed = (v >= target) if ascending else (v <= target)
        if crossed:
            return (t - t_fast_start) / 1e6
    return None


def fit_pulse(cap: Capture, pulse: str,
              detect_onset: bool = True) -> PulseFit:
    """Fit τ for one pulse. `pulse` is '1' or '2'.

    `detect_onset` (default True): use the dF/dt onset detector to correct
    for M573's queue-time phase-mark lag. Pass False to compare against
    the pre-correction behaviour (for A/B diagnostics).
    """
    slow_name = f"slow_in_{pulse}"
    fast_name = f"fast_{pulse}"
    # slow_out_N gives us a clean end-of-fast boundary. If it's missing, fall
    # back to pulseN_end, then to the last sample.
    slow_out_name = f"slow_out_{pulse}"
    pulse_end_name = f"pulse{pulse}_end"

    if slow_name not in cap.phases or fast_name not in cap.phases:
        raise ValueError(f"Missing phase marks for pulse {pulse} "
                         f"(need {slow_name} and {fast_name})")

    samples = list(zip(cap.t_us, cap.load_g))

    t_slow_start = cap.phases[slow_name]
    t_fast_start_mark = cap.phases[fast_name]  # raw PA_PHASE timestamp
    if slow_out_name in cap.phases:
        t_fast_end = cap.phases[slow_out_name]
    elif pulse_end_name in cap.phases:
        t_fast_end = cap.phases[pulse_end_name]
    else:
        t_fast_end = cap.t_us[-1] + 1

    slow_seg = _slice(samples, t_slow_start, t_fast_start_mark)

    # F_0 from the last ~20 samples (or last 25%) of slow_in. slow_in is
    # ~250 ms = ~80 samples at 320 Hz, and we want the tail where the melt
    # has had a chance to approach its slow-flow steady state.
    n_tail = max(10, len(slow_seg) // 4) if slow_seg else 0
    F_0 = _mean_last(slow_seg, n_tail) if n_tail else 0.0

    # Baseline stats — needed by the onset detector for σ-based rise
    # threshold; also reported downstream.
    base_mean, base_sigma = _baseline_stats(samples, cap)

    # Raw fast segment (from PA_PHASE mark) — used only to produce an
    # initial F_inf estimate for the onset detector. The post-onset segment
    # replaces this below.
    fast_seg_raw = _slice(samples, t_fast_start_mark, t_fast_end)
    n_plateau_raw = max(10, int(len(fast_seg_raw) * 0.3)) if fast_seg_raw else 0
    F_inf_hint = (_mean_last(fast_seg_raw, n_plateau_raw)
                  if n_plateau_raw else F_0)

    # Onset detection: replace the PA_PHASE timestamp with the signal-derived
    # true-onset time. See _detect_fast_onset docstring + the memory file
    # .auto-memory/m573_phase_mark_queue_lag.md for the "why."
    if detect_onset and fast_seg_raw:
        onset = _detect_fast_onset(
            fast_seg_raw, t_fast_start_mark,
            F_0, F_inf_hint, base_sigma,
        )
        t_fast_start = onset.t_us
    else:
        onset = OnsetDetection(t_fast_start_mark, "disabled", 0.0, 0.0,
                               F_start=None)
        t_fast_start = t_fast_start_mark

    # When the onset detector finds a real pressure-release dip, the rise
    # actually starts from the dip bottom, not from the slow-phase F_0. Use
    # the detector's F_start so the threshold / log-lin fits measure from
    # the correct origin. (If no dip was detected, keep slow-phase F_0.)
    if onset.F_start is not None:
        F_0 = onset.F_start

    fast_seg = _slice(samples, t_fast_start, t_fast_end)

    # F_inf: use plateau detection instead of a fixed last-30 % window.
    # The B1 pulse geometry delivers a ≥180 ms true plateau mid-way
    # through the fast window, BUT the trailing ~50 ms is contaminated by
    # extruder-decel dip (when the commanded cruise ends and feedrate
    # ramps to zero, nozzle pressure releases). "Last 30 %" catches that
    # dip; plateau detection finds the flat middle zone and uses its
    # mean. See _find_plateau_F_inf for the bias rationale.
    #
    # If plateau detection falls through (short fast_seg, or no flat
    # window qualifies), we fall back to the naïve last-30 % mean — same
    # behaviour as before the patch.
    F_inf_naive, plat_start, plat_std = _find_plateau_F_inf(fast_seg)
    plateau_found = plat_start >= 0

    # Mid-time of the window whose mean we're treating as F_inf. If
    # plateau detection succeeded, use the plateau window's midpoint;
    # otherwise use the naïve last-30 % midpoint (matches pre-patch
    # behaviour for the refinement step below).
    if plateau_found and fast_seg:
        plat_W = max(16, min(40, len(fast_seg) // 5))
        mid_idx = plat_start + plat_W // 2
        mid_idx = min(mid_idx, len(fast_seg) - 1)
        t_tail_mid_s = (fast_seg[mid_idx][0] - t_fast_start) / 1e6
    elif fast_seg:
        n_fallback = max(10, int(len(fast_seg) * 0.3))
        tail_start_t = fast_seg[-n_fallback][0]
        tail_end_t = fast_seg[-1][0]
        t_tail_mid_s = ((tail_start_t + tail_end_t) / 2 - t_fast_start) / 1e6
    else:
        t_tail_mid_s = 0.0

    # --- Method 1: 63 % threshold with self-consistency refinement -----------
    # Pass 1: threshold against F_inf_naive.
    # Pass 2+: given τ₁, back-correct F_inf using the first-order model:
    #     F̄_tail  =  F_inf − (F_inf − F_0) · exp(−t_tail_mid / τ)
    #  →  F_inf   =  (F̄_tail − F_0 · e) / (1 − e)    where e = exp(−t_mid/τ)
    # Then re-run the threshold. Typically converges in 2–3 passes.
    # Each pass: one exp() call, one divide — fine for Cortex-M4F firmware.
    #
    # IMPORTANT: only run the refinement when the F_inf estimate came
    # from the last-30 % FALLBACK. If plateau detection succeeded, the
    # window we averaged is (by construction) flat, so F_inf_naive is
    # already the plateau asymptote and the refinement would drift it
    # UP by assuming the window is still rising. On the C1.1 capture
    # this drift moved F_inf 4254 → 4383 g and inflated τ_thr by 11 ms.
    F_inf = F_inf_naive
    tau_thr: Optional[float] = _threshold_cross(fast_seg, t_fast_start,
                                                F_0, F_inf)
    # Refinement corrects F_inf when the tail isn't yet at plateau.
    #
    # Disambiguating "true physical plateau" vs "first-order window that
    # looks flat but is still rising" is the crux here. The cheap tell is
    # the within-window linear slope:
    #   - Real capture @ K≈80 ms, F_inf≈4255 g: plateau is physically flat
    #     (signal reached steady state; slope ≈ 0 ± noise).
    #   - Synth first-order K=85 ms over the 296 ms window: plateau window
    #     ends at 97 % of asymptote; predicted first-order slope within
    #     window is ≈ 2 g/s out of dF≈9 g → detectable positive rise.
    # So: if we detect a significant positive slope, the signal is still
    # rising and refinement should correct F_inf upward. If the window is
    # truly flat, skip refinement — it would drift F_inf away from truth.
    skip_refine = False
    if plateau_found:
        plat_W = max(16, min(40, len(fast_seg) // 5))
        plat_end = min(plat_start + plat_W, len(fast_seg))
        # Linear regression of load vs. time within the plateau window.
        xs_p: list[float] = []
        ys_p: list[float] = []
        for i in range(plat_start, plat_end):
            t_us, v = fast_seg[i]
            xs_p.append((t_us - fast_seg[plat_start][0]) / 1e6)
            ys_p.append(v)
        n_p = len(xs_p)
        if n_p >= 4:
            sx = sum(xs_p)
            sy = sum(ys_p)
            sxx = sum(x * x for x in xs_p)
            sxy = sum(x * y for x, y in zip(xs_p, ys_p))
            denom = n_p * sxx - sx * sx
            if abs(denom) > 1e-12:
                slope = (n_p * sxy - sx * sy) / denom  # g/s
                # σ of slope for a flat (noise-only) window with evenly
                # spaced samples at rate dt: ≈ sqrt(12)*σ / (N^1.5 * dt).
                # dt from the window itself.
                dt = (xs_p[-1] - xs_p[0]) / max(1, n_p - 1)
                sigma_slope = (math.sqrt(12.0) * (base_sigma or 0.0)
                               / (n_p ** 1.5 * max(dt, 1e-6)))
                # "Truly flat" iff slope isn't significantly positive
                # (t-stat < 2). Using signed slope — only a still-rising
                # window needs refinement.
                if sigma_slope > 0 and slope < 2.0 * sigma_slope:
                    skip_refine = True
    MAX_ITERS = 0 if skip_refine else 4
    for _ in range(MAX_ITERS):
        if tau_thr is None or tau_thr <= 0 or t_tail_mid_s <= 0:
            break
        e = math.exp(-t_tail_mid_s / tau_thr)
        # If e < ~0.03 the tail is already at plateau (>97 % there) and the
        # correction is negligible; early-out.
        if e < 0.03:
            break
        # If e >= 0.95 we're so far from plateau that the correction is
        # ill-conditioned (dividing by a near-zero 1−e). Bail — the log-lin
        # cross-check will flag this.
        if e >= 0.95:
            break
        F_inf_new = (F_inf_naive - F_0 * e) / (1.0 - e)
        if abs(F_inf_new - F_inf) < 1e-4:  # converged to <0.1 mg
            F_inf = F_inf_new
            break
        F_inf = F_inf_new
        tau_thr = _threshold_cross(fast_seg, t_fast_start, F_0, F_inf)

    dF = F_inf - F_0

    # --- Method 2: log-linear regression (off-line cross-check) --------------
    # Residual r(t) = F_inf − F(t). For a clean first-order rise,
    # ln|r(t)| = ln|ΔF| − t / τ  → slope = −1/τ.
    # We only fit the first ~3τ of the rise (where the residual is still well
    # above noise). If τ_threshold failed, fall back to fitting the first half
    # of the fast window.
    tau_log: Optional[float] = None
    n_fit = 0
    if abs(dF) > 1e-6 and len(fast_seg) >= 8:
        if tau_thr is not None:
            horizon_s = 3.0 * tau_thr
        else:
            horizon_s = 0.5 * ((fast_seg[-1][0] - fast_seg[0][0]) / 1e6)
        horizon_us = int(horizon_s * 1e6)

        # Residual gate — reject samples where |r| is so small that a tiny
        # F_inf bias poisons the log. Two components:
        #   - 2·fast_sigma: fast-phase noise floor. Use the plateau-window
        #     std when available; fall back to baseline_sigma otherwise.
        #     Baseline sigma on a real capture reflects XY-motion during
        #     slow_in (~614 g on C1.1) — ~100× larger than the quiet noise
        #     during fast_N (~6 g). Using base_sigma here wrongly truncates
        #     the loglin fit to the first ~40 % of the rise on real data.
        #   - 5 % of |dF|: F_inf-bias floor. Even a 0.3 % bias in F_inf is
        #     enough to wreck log(r) near plateau — where true r << |dF|·5 %.
        #     This cap stops the regression from "seeing" samples inside
        #     the zone where the residual is smaller than the plausible
        #     F_inf estimation error. Found by the K=85 ms synth case
        #     where F_inf was 0.03 g low and log-lin drifted 21 ms short.
        if plateau_found and plat_std < float("inf"):
            fast_sigma = plat_std
        else:
            fast_sigma = base_sigma or 0.0
        r_gate = max(1e-4,
                     fast_sigma * 2.0,
                     abs(dF) * 0.05)

        xs: list[float] = []
        ys: list[float] = []
        for t, v in fast_seg:
            dt_us = t - t_fast_start
            if dt_us > horizon_us:
                break
            r = F_inf - v
            # Near the plateau the residual is dominated by noise; the log is
            # ill-defined and overshoot flips sign. Drop those points.
            if abs(r) < r_gate:
                continue
            if (r > 0) != (dF > 0):
                continue  # residual has flipped past plateau — discard
            xs.append(dt_us / 1e6)
            ys.append(math.log(abs(r)))

        if len(xs) >= 5:
            n = len(xs)
            sx = sum(xs)
            sy = sum(ys)
            sxx = sum(x * x for x in xs)
            sxy = sum(x * y for x, y in zip(xs, ys))
            denom = n * sxx - sx * sx
            if abs(denom) > 1e-9:
                slope = (n * sxy - sx * sy) / denom
                if slope < 0:  # valid first-order rise has negative slope
                    tau_log = -1.0 / slope
                    n_fit = n

    return PulseFit(
        pulse=pulse,
        F_0=F_0,
        F_inf=F_inf,
        dF=dF,
        baseline_g=base_mean,
        baseline_sigma_g=base_sigma,
        tau_threshold_s=tau_thr,
        tau_loglin_s=tau_log,
        n_samples_slow=len(slow_seg),
        n_samples_fast=len(fast_seg),
        n_samples_loglin=n_fit,
        onset=onset,
    )


# ---- Reporting -------------------------------------------------------------


def print_fit(fit: PulseFit, ground_truth: Optional[float] = None) -> None:
    print(f"--- Pulse {fit.pulse} ---")
    print(f"  slow_in samples:   {fit.n_samples_slow}")
    print(f"  fast samples:      {fit.n_samples_fast}")
    if fit.baseline_g is not None:
        print(f"  baseline (zero_flow_1): "
              f"{fit.baseline_g:+.3f} g  (σ={fit.baseline_sigma_g:.3f} g)")
    o = fit.onset
    if o.method == "signal_dip":
        print(f"  onset correction:  +{o.offset_ms:5.1f} ms (signal dip, "
              f"depth {o.dip_depth_g:+.1f} g)")
    elif o.method == "phase_mark_aligned":
        print(f"  onset correction:  {o.offset_ms:+.1f} ms "
              f"(phase-mark kept; dip {o.dip_depth_g:.1f} g < 3 % of ΔF)")
    elif o.method == "disabled":
        print(f"  onset correction:  disabled (raw PA_PHASE mark)")
    elif o.method == "no_step":
        print(f"  onset correction:  skipped (no step detected)")
    print(f"  F_0 (pre-step):    {fit.F_0:+.3f} g")
    print(f"  F_inf (plateau):   {fit.F_inf:+.3f} g")
    print(f"  ΔF step:           {fit.dF:+.3f} g")
    if fit.tau_threshold_s is not None:
        k = fit.tau_threshold_s
        print(f"  τ (63% threshold): {k*1000:6.1f} ms   "
              f"→ K = {k:.4f}   [firmware-portable]")
    else:
        print(f"  τ (63% threshold): not reached within fast window")
    if fit.tau_loglin_s is not None:
        k = fit.tau_loglin_s
        print(f"  τ (log-linear):    {k*1000:6.1f} ms   "
              f"→ K = {k:.4f}   [n={fit.n_samples_loglin}]")
    else:
        print(f"  τ (log-linear):    fit failed")
    if (ground_truth is not None
            and fit.tau_threshold_s is not None):
        delta = fit.tau_threshold_s - ground_truth
        print(f"  Δ vs visual PA (K={ground_truth:.4f}): "
              f"{delta*1000:+.1f} ms "
              f"({delta/ground_truth*100:+.1f} %)")


# ---- Self-test -------------------------------------------------------------


def _synth_capture(k_true: float,
                   f_slow: float = 1.5,
                   f_fast: float = 11.0,
                   noise_sigma: float = 0.10,
                   seed: int = 42,
                   phase_mark_lead_us: int = 0,
                   dip_depth: float = 0.0) -> Capture:
    """Synthesize a capture with a known τ so we can regression-test the fit.

    Mirrors the timing of a real MK4S M573 run: 200 ms zero_flow_1, then
    282 ms slow_in_1 ramping to f_slow, then 296 ms fast_1 stepping toward
    f_fast with time constant k_true, then 331 ms slow_out_1 decaying back.
    Sample rate 320 Hz.

    `phase_mark_lead_us`: simulate M573's queue-time phase-mark bug. The
    fast_N PA_PHASE marker is placed `lead` µs before the real motion
    onset; during the lead window the signal stays near f_slow (as if the
    stepper is still finishing slow_in) and may dip by `dip_depth` g
    before the real step begins. With lead=0, dip=0 we model a perfect
    capture (onset = phase_mark).
    """
    import random
    rng = random.Random(seed)
    dt_us = int(1_000_000 / 320)  # ~3125 µs
    cap = Capture()
    t = 0  # µs

    def add(duration_us: int, model) -> None:
        nonlocal t
        n = duration_us // dt_us
        for i in range(n):
            v = model(i * dt_us / 1e6) + rng.gauss(0.0, noise_sigma)
            cap.t_us.append(t)
            cap.load_g.append(v)
            t += dt_us

    def add_pulse(idx: int) -> None:
        """Synthesize one pulse (zero_flow for pulse 1 only, slow_in, fast,
        slow_out) with the configured lead/dip injected at fast_N onset."""
        nonlocal t
        suffix = str(idx)
        cap.phases[f"pulse{idx}_start"] = t
        if idx == 1:
            cap.phases["zero_flow_1"] = t
            add(200_000, lambda s: 0.0)

        cap.phases[f"slow_in_{suffix}"] = t
        t_slow_start = t
        add(282_000, lambda s: f_slow * (1.0 - math.exp(-s / k_true)))

        # Signal value at the end of slow_in (may still be rising if
        # slow_in duration < 5·k_true). This is what the melt holds while
        # the lead window elapses.
        f_slow_end = f_slow * (1.0 - math.exp(
            -(t - t_slow_start) / 1e6 / k_true))

        cap.phases[f"fast_{suffix}"] = t
        # Lead window: PA_PHASE marker is here, but the real motion
        # hasn't started. Signal stays near f_slow_end and optionally
        # dips linearly to f_slow_end − dip_depth at the real onset.
        if phase_mark_lead_us > 0:
            lead_s = phase_mark_lead_us / 1e6
            add(phase_mark_lead_us,
                lambda s: f_slow_end - dip_depth * (s / lead_s))

        # Real fast step. Starts from whatever value the signal reached
        # at the end of the lead window (f_slow_end − dip_depth), rises
        # toward f_fast with time constant k_true.
        f_step_start = f_slow_end - dip_depth
        add(296_000,
            lambda s: f_fast
                      + (f_step_start - f_fast) * math.exp(-s / k_true))

        cap.phases[f"slow_out_{suffix}"] = t
        f_end_fast = (f_fast
                      + (f_step_start - f_fast) * math.exp(-0.296 / k_true))
        add(331_000,
            lambda s: f_slow
                      + (f_end_fast - f_slow) * math.exp(-s / k_true))
        cap.phases[f"pulse{idx}_end"] = t

    add_pulse(1)
    # Zero-duration purge between pulses — we only care about fast_N shapes.
    cap.phases["purge2_start"] = t
    cap.phases["purge2_end"] = t
    add_pulse(2)

    cap.sample_count = len(cap.t_us)
    cap.dropped_count = 0
    return cap


def _run_self_test() -> int:
    """Synthesize captures at several known K values and verify recovery.

    Two test families:
      (a) Clean captures (lead=0, dip=0) — regression-test the threshold &
          log-linear fits on pure first-order data. Onset detector should
          NOT fire here (dip-significance gate rejects noise-only minima).
      (b) Lead + dip captures (lead=40 ms, dip=0.5 g on a ~9.5 g step) —
          regression-test the onset detector itself. Without correction
          the threshold fit is biased by ~lead ms; with correction it
          should land back within the ±10 ms tolerance.
    """
    print("Self-test: synthetic first-order captures, "
          "320 Hz sampling, 0.10 g noise.\n")
    test_ks = [0.030, 0.055, 0.085, 0.120]
    any_fail = False

    print("--- Family (a): clean captures, no queue-time lag ---")
    for k_true in test_ks:
        cap = _synth_capture(k_true=k_true)
        print(f"=== K_true = {k_true:.4f} ({k_true*1000:.0f} ms) ===")
        for pulse in ("1", "2"):
            fit = fit_pulse(cap, pulse)
            if fit.tau_threshold_s is None:
                print(f"  pulse {pulse}: THRESHOLD FAILED")
                any_fail = True
                continue
            err_thr = fit.tau_threshold_s - k_true
            err_log = (fit.tau_loglin_s - k_true) if fit.tau_loglin_s else None
            thr_ok = abs(err_thr) <= 0.010
            log_ok = (err_log is None) or abs(err_log) <= 0.010
            # Also require that the detector correctly said "phase_mark"
            # (no real dip in clean data).
            onset_ok = fit.onset.method in ("phase_mark_aligned", "disabled")
            tag = "OK" if (thr_ok and log_ok and onset_ok) else "FAIL"
            if tag == "FAIL":
                any_fail = True
            log_str = (f"  log-lin {fit.tau_loglin_s*1000:6.1f} ms "
                       f"(Δ={err_log*1000:+5.1f} ms)"
                       if fit.tau_loglin_s else "  log-lin n/a")
            print(f"  pulse {pulse}: "
                  f"thr {fit.tau_threshold_s*1000:6.1f} ms "
                  f"(Δ={err_thr*1000:+5.1f} ms), {log_str}, "
                  f"onset={fit.onset.method}  [{tag}]")
        print()

    print("--- Family (b): 40 ms queue-time lag + 0.5 g dip ---")
    # The dip depth (0.5 g) is 5 % of the 9.5 g step — just above the 3 %
    # significance gate. σ_baseline is 0.1 g, so σ of the 5-point moving
    # average is ≈ 0.045 g and the 0.5-g dip stands out easily.
    #
    # Tolerance: ±15 ms here (vs ±10 ms in Family (a)). The centered
    # moving-average min-finder has a ≈ one-sample-plus-noise bias
    # relative to the true V-vertex: post-onset samples rise fast and
    # pull the smoothed minimum backward in time by (5 samples × 3.125 ms)
    # / 2 = ~ 8 ms. We accept that residual as a known limitation of a
    # single-pass firmware-portable detector — the correction still cuts
    # the raw phase-mark bias by ~5× (~60 ms → ~10 ms) which is what A1
    # needs to give A2 a fighting chance.
    for k_true in test_ks:
        cap = _synth_capture(k_true=k_true,
                             phase_mark_lead_us=40_000,
                             dip_depth=0.5)
        print(f"=== K_true = {k_true:.4f} ({k_true*1000:.0f} ms), "
              f"lead=40 ms, dip=0.5 g ===")
        for pulse in ("1", "2"):
            fit_raw = fit_pulse(cap, pulse, detect_onset=False)
            fit = fit_pulse(cap, pulse, detect_onset=True)

            # The corrected fit is the one we're validating. The raw fit
            # is printed as the before-picture to show the correction
            # earned its keep.
            tau_raw = fit_raw.tau_threshold_s
            tau = fit.tau_threshold_s
            if tau is None:
                print(f"  pulse {pulse}: THRESHOLD FAILED (corrected)")
                any_fail = True
                continue
            err = tau - k_true
            thr_ok = abs(err) <= 0.015
            onset_ok = fit.onset.method == "signal_dip"
            tag = "OK" if (thr_ok and onset_ok) else "FAIL"
            if tag == "FAIL":
                any_fail = True
            raw_str = (f"{tau_raw*1000:5.1f}"
                       if tau_raw is not None else "  n/a")
            print(f"  pulse {pulse}: "
                  f"raw {raw_str} ms → corrected {tau*1000:6.1f} ms "
                  f"(Δ={err*1000:+5.1f} ms), "
                  f"onset=+{fit.onset.offset_ms:.0f}ms "
                  f"(dip {fit.onset.dip_depth_g:.2f} g)  [{tag}]")
        print()

    print("SELF-TEST:", "PASS" if not any_fail else "FAIL")
    return 0 if not any_fail else 1


# ---- Main ------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Fit K (τ) from an M573 PA capture.")
    ap.add_argument("capture", type=Path, nargs="?",
                    help="path to pa_capture_*.log "
                         "(omit with --self-test)")
    ap.add_argument("--ground-truth", type=float, default=None,
                    help="Visual PA test K for comparison (s)")
    ap.add_argument("--self-test", action="store_true",
                    help="Run synthetic-data regression test and exit")
    ap.add_argument("--no-onset-detect", action="store_true",
                    help="Disable the signal-driven onset detector "
                         "(use raw PA_PHASE timestamps). For A/B comparison "
                         "against the queue-time-lag correction.")
    args = ap.parse_args()

    if args.self_test:
        return _run_self_test()
    if args.capture is None:
        ap.error("capture path is required unless --self-test")
    if not args.capture.exists():
        print(f"capture file not found: {args.capture}", file=sys.stderr)
        return 1

    cap = parse_capture(args.capture)
    print(f"Parsed {args.capture.name}: {len(cap.t_us)} samples, "
          f"dropped {cap.dropped_count}, "
          f"{len(cap.phases)} phase marks")
    if cap.dropped_count and cap.dropped_count > 0:
        print(f"  ⚠ {cap.dropped_count} dropped samples — "
              f"capture window exceeded ring buffer. Fit still valid if the "
              f"fast_* windows were covered.")
    print()

    any_ok = False
    for pulse in ("1", "2"):
        try:
            fit = fit_pulse(cap, pulse,
                            detect_onset=not args.no_onset_detect)
            print_fit(fit, args.ground_truth)
            print()
            any_ok = True
        except ValueError as e:
            print(f"--- Pulse {pulse} ---  (skipped: {e})\n")

    return 0 if any_ok else 1


if __name__ == "__main__":
    sys.exit(main())
