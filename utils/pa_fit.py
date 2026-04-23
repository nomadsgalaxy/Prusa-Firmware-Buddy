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


def _mean_last(seg: list[tuple[int, float]], n: int) -> float:
    """Mean of the last n samples of `seg` (falls back to whole seg)."""
    if not seg:
        return 0.0
    tail = seg[-n:] if n < len(seg) else seg
    return sum(v for _, v in tail) / len(tail)


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


def fit_pulse(cap: Capture, pulse: str) -> PulseFit:
    """Fit τ for one pulse. `pulse` is '1' or '2'."""
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
    t_fast_start = cap.phases[fast_name]
    if slow_out_name in cap.phases:
        t_fast_end = cap.phases[slow_out_name]
    elif pulse_end_name in cap.phases:
        t_fast_end = cap.phases[pulse_end_name]
    else:
        t_fast_end = cap.t_us[-1] + 1

    slow_seg = _slice(samples, t_slow_start, t_fast_start)
    fast_seg = _slice(samples, t_fast_start, t_fast_end)

    # F_0 from the last ~20 samples (or last 25%) of slow_in. slow_in is
    # ~250 ms = ~80 samples at 320 Hz, and we want the tail where the melt
    # has had a chance to approach its slow-flow steady state.
    n_tail = max(10, len(slow_seg) // 4) if slow_seg else 0
    F_0 = _mean_last(slow_seg, n_tail) if n_tail else 0.0

    # Naïve F_inf: mean of last 30 % of fast. This is a good estimate only if
    # the fast window is long enough for the signal to have reached plateau
    # (t_fast_end  ≥  ~5τ). For PLA (τ≈55 ms) in a 296 ms fast window that's
    # fine, but for PETG (τ≈80 ms) and stiffer materials (τ≈120 ms) the tail
    # is still rising and F_inf_naïve is biased low, which would bias τ low.
    # We correct this below with one refinement pass.
    n_plateau = max(10, int(len(fast_seg) * 0.3)) if fast_seg else 0
    F_inf_naive = _mean_last(fast_seg, n_plateau) if n_plateau else F_0

    # Mid-time of the tail window, relative to fast_start. Used by the
    # refinement step below: we model  F̄_tail ≈ F(t_tail_mid)  and back out
    # a corrected F_inf that's self-consistent with the τ estimate.
    if n_plateau and fast_seg:
        tail_start_t = fast_seg[-n_plateau][0]
        tail_end_t = fast_seg[-1][0]
        t_tail_mid_s = ((tail_start_t + tail_end_t) / 2 - t_fast_start) / 1e6
    else:
        t_tail_mid_s = 0.0

    base_mean, base_sigma = _baseline_stats(samples, cap)

    # --- Method 1: 63 % threshold with self-consistency refinement -----------
    # Pass 1: threshold against the naïve F_inf.
    # Pass 2+: given τ₁, back-correct F_inf using the first-order model:
    #     F̄_tail  =  F_inf − (F_inf − F_0) · exp(−t_tail_mid / τ)
    #  →  F_inf   =  (F̄_tail − F_0 · e) / (1 − e)    where e = exp(−t_mid/τ)
    # Then re-run the threshold. Typically converges in 2–3 passes.
    # Each pass: one exp() call, one divide — fine for Cortex-M4F firmware.
    F_inf = F_inf_naive
    tau_thr: Optional[float] = _threshold_cross(fast_seg, t_fast_start,
                                                F_0, F_inf)
    MAX_ITERS = 4
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

        xs: list[float] = []
        ys: list[float] = []
        for t, v in fast_seg:
            dt_us = t - t_fast_start
            if dt_us > horizon_us:
                break
            r = F_inf - v
            # Near the plateau the residual is dominated by noise; the log is
            # ill-defined and overshoot flips sign. Drop those points.
            if abs(r) < max(1e-4, (base_sigma or 0.0) * 2.0):
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
    )


# ---- Reporting -------------------------------------------------------------


def print_fit(fit: PulseFit, ground_truth: Optional[float] = None) -> None:
    print(f"--- Pulse {fit.pulse} ---")
    print(f"  slow_in samples:   {fit.n_samples_slow}")
    print(f"  fast samples:      {fit.n_samples_fast}")
    if fit.baseline_g is not None:
        print(f"  baseline (zero_flow_1): "
              f"{fit.baseline_g:+.3f} g  (σ={fit.baseline_sigma_g:.3f} g)")
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
                   seed: int = 42) -> Capture:
    """Synthesize a capture with a known τ so we can regression-test the fit.

    Mirrors the timing of a real MK4S M573 run: 200 ms zero_flow_1, then
    282 ms slow_in_1 ramping to f_slow, then 296 ms fast_1 stepping toward
    f_fast with time constant k_true, then 331 ms slow_out_1 decaying back.
    Sample rate 320 Hz.
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

    # zero_flow_1: F ≈ 0
    cap.phases["pulse1_start"] = t
    cap.phases["zero_flow_1"] = t
    add(200_000, lambda s: 0.0)

    # slow_in_1: first-order rise from 0 → f_slow with same τ=k_true
    t_slow_start = t
    cap.phases["slow_in_1"] = t
    add(282_000, lambda s: f_slow * (1.0 - math.exp(-s / k_true)))
    # Patch F_0 to the true asymptote the melt is relaxing toward (which
    # slow_in may not fully reach) — the fit estimates F_0 from the last few
    # samples, so make sure those are close to f_slow.

    # fast_1: step to f_fast with τ = k_true, starting from whatever F(t_end)
    # the slow_in ended at.
    f_start_fast = f_slow * (1.0 - math.exp(-(t - t_slow_start) / 1e6 / k_true))
    cap.phases["fast_1"] = t
    add(296_000,
        lambda s: f_fast + (f_start_fast - f_fast) * math.exp(-s / k_true))

    # slow_out_1: decay back toward f_slow
    f_start_out = f_fast + (f_start_fast - f_fast) * math.exp(-0.296 / k_true)
    cap.phases["slow_out_1"] = t
    add(331_000,
        lambda s: f_slow + (f_start_out - f_slow) * math.exp(-s / k_true))
    cap.phases["pulse1_end"] = t

    # Pulse 2 identical — skip the purge and synthesize same-shape data so the
    # self-test exercises both fits.
    cap.phases["purge2_start"] = t
    cap.phases["purge2_end"] = t
    cap.phases["pulse2_start"] = t
    cap.phases["slow_in_2"] = t
    t_slow2_start = t
    add(282_000, lambda s: f_slow * (1.0 - math.exp(-s / k_true)))
    f_start_fast2 = f_slow * (1.0 - math.exp(
        -(t - t_slow2_start) / 1e6 / k_true))
    cap.phases["fast_2"] = t
    add(296_000,
        lambda s: f_fast + (f_start_fast2 - f_fast) * math.exp(-s / k_true))
    cap.phases["slow_out_2"] = t
    f_start_out2 = f_fast + (f_start_fast2 - f_fast) * math.exp(
        -0.296 / k_true)
    add(331_000,
        lambda s: f_slow + (f_start_out2 - f_slow) * math.exp(-s / k_true))
    cap.phases["pulse2_end"] = t

    cap.sample_count = len(cap.t_us)
    cap.dropped_count = 0
    return cap


def _run_self_test() -> int:
    """Synthesize captures at several known K values and verify recovery."""
    print("Self-test: synthetic first-order captures, "
          "320 Hz sampling, 0.10 g noise.\n")
    test_ks = [0.030, 0.055, 0.085, 0.120]
    any_fail = False
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
            thr_ok = abs(err_thr) <= 0.010  # ±10 ms tolerance
            log_ok = (err_log is None) or abs(err_log) <= 0.010
            tag = "OK" if (thr_ok and log_ok) else "FAIL"
            if tag == "FAIL":
                any_fail = True
            log_str = (f"  log-lin {fit.tau_loglin_s*1000:6.1f} ms "
                       f"(Δ={err_log*1000:+5.1f} ms)"
                       if fit.tau_loglin_s else "  log-lin n/a")
            print(f"  pulse {pulse}: "
                  f"thr {fit.tau_threshold_s*1000:6.1f} ms "
                  f"(Δ={err_thr*1000:+5.1f} ms), {log_str}  [{tag}]")
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
            fit = fit_pulse(cap, pulse)
            print_fit(fit, args.ground_truth)
            print()
            any_ok = True
        except ValueError as e:
            print(f"--- Pulse {pulse} ---  (skipped: {e})\n")

    return 0 if any_ok else 1


if __name__ == "__main__":
    sys.exit(main())
