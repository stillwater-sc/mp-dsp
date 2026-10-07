---
title: Spectrum Analyzer Overview
description: Architectural overview of the spectrum-analyzer module — swept-tuned vs. FFT analyzers, the RBW / VBW / averaging tradeoff, and the per-stage precision contract that governs the mixed-precision design
---

This module implements the digital signal-processing stages found in
both classic architectures of a real spectrum analyzer:

1. The **swept-tuned analyzer** — a local oscillator (LO) is swept
   across the band of interest, the input is mixed down against the
   LO, a narrow resolution-bandwidth (RBW) filter selects energy
   around the IF, and a detector reduces the dwell-time samples to
   one trace point per LO frequency.
2. The **FFT-based analyzer** — overlapping windowed FFTs produce
   one full spectrum per hop; trace averaging smooths across sweeps;
   a circular waterfall buffer retains history for spectrogram
   displays.

Both architectures are useful in practice; both are exercised by the
[end-to-end demo](./spectrum-analyzer-demo/). This page is the
architectural reference: what the module ships, when to pick one
architecture over the other, and the precision contract each stage
honors.

## Scope: what this module is and isn't

**This module IS:**

- The DSP primitives that sit between the ADC samples and the trace
  memory of a spectrum-analyzer instrument: front-end calibration,
  swept LO + RBW + VBW + detector chain, real-time overlapping FFT,
  trace averaging, waterfall buffer, peak / harmonic / delta markers.
- A library you can compose into either architecture (swept or FFT)
  or, in fact, both in parallel — every primitive is independent.
- Mixed-precision-aware: every numerically-active stage takes the
  three scalar template parameters
  (`CoeffScalar`, `StateScalar`, `SampleScalar`) so you can pick the
  precision of each stage independently.

**This module is NOT:**

- An ADC driver. Inputs are assumed to be in memory.
- A measurement / GUI / display layer. The output of every primitive
  is data (a span, a vector, a `std::pair`); rendering is the
  caller's responsibility.
- A swept-time-budget analyzer. The library lets you compose a
  swept-tuned pipeline, but it doesn't enforce or compute the
  RBW-bound sweep-time budget commercial analyzers ship; the swept
  demo shows what happens when dwell-per-bin is shorter than the RBW
  settling time.

The companion oscilloscope-side primitives (trigger, ring buffer,
peak-detect decimator, equalizer, measurements) live alongside in the
same `instrument/` module — see the [scope demo](./scope-demo/) for
that pipeline.

## What lives in this module

| Component | Header | Role |
|---|---|---|
| Front-End Corrector | `spectrum/front_end_corrector.hpp` | Calibration FIR (alias for `EqualizerFilter`) |
| Real-Time Spectrum | `spectrum/realtime_spectrum.hpp` | Overlapping windowed FFT engine |
| Trace Averager | `spectrum/trace_averaging.hpp` | Linear / Exponential / MaxHold / MinHold / MaxHoldN |
| Waterfall Buffer | `spectrum/waterfall_buffer.hpp` | Circular 2D ring for spectrogram displays |
| Swept LO | `spectrum/swept_lo.hpp` | Phase-coherent linear / log chirp generator |
| RBW Filter | `spectrum/rbw_filter.hpp` | Synchronously-tuned cascade of N RBJ band-pass biquads |
| VBW Filter | `spectrum/vbw_filter.hpp` | Single-pole leaky-integrator post-detector LPF |
| Detectors | `spectrum/detectors.hpp` | Peak / Sample / Average / RMS / Negative-peak |
| Markers | `spectrum/markers.hpp` | `find_peaks` / `harmonic_markers` / `make_delta_marker` |
| End-to-End Demo | `applications/spectrum_analyzer_demo` | Both architectures × four precision plans |

## Two architectures, one library

### Swept-tuned analyzer

```text
ADC -->  EqualizerFilter  -->  mixer * SweptLO  -->  RBW  -->  detector  -->  VBW  -->  trace[bin]
                                       ▲                                                    │
                                       └─ LO sweeps from f_start to f_stop                  │
                                          (linear or log) over duration T                   │
                                                                                            ▼
                                                                       trace memory indexed
                                                                       by LO frequency
```

The swept-tuned architecture is the classic real-analyzer topology:
the LO walks across the band and the RBW filter "sees" one frequency
window at a time. The `detector` stage reduces each dwell window
(samples collected while the LO sits at one frequency) to a single
trace point.

**Pros:** Tractable hardware (one narrow filter, one LO, one
detector); huge instantaneous frequency span; the canonical EMI /
compliance measurement architecture.

**Cons:** Sweep-time-bounded. The product `(span / RBW)² × (1 / RBW)`
sets the minimum sweep time for a clean measurement; tightening the
RBW for better resolution costs sweep time quadratically. A swept
analyzer with RBW=2 kHz over a 200 kHz span needs ~10 seconds of
dwell to fully settle the RBW per bin — far slower than an FFT-based
measurement.

### FFT-based analyzer

```text
ADC -->  EqualizerFilter  -->  RealtimeSpectrum (FFT, hop-50%)  -->  TraceAverager
                                                                          │
                                                                          ▼
                                                                  WaterfallBuffer
                                                                  (history for spectrogram)
                                                                          │
                                                                          ▼
                                                                  find_peaks / harmonic_markers
```

The FFT-based architecture computes the full spectrum every `hop_size`
input samples. Frequency resolution is `Fs / N`; dynamic range is set
by the FFT precision and the windowing function (Hann's
`-31 dB` sidelobes are typical).

**Pros:** Single-pass. One windowed FFT produces the full spectrum
simultaneously, so the dynamic range you measure is bounded by the
FFT's noise floor (very low for double-precision math), not by
dwell-per-bin. For the same span and same input length, FFT-based
analysis sees deeper spurs than swept-tuned does.

**Cons:** Frequency resolution is `Fs / N` — to halve the bin width
you double the FFT, which doubles the per-FFT compute. The span is
fixed at `Fs / 2`; you can't span 100 GHz at high resolution with a
single FFT.

### When to pick which

| Situation | Architecture |
|---|---|
| EMI / compliance measurement (regulated by CISPR limits) | Swept-tuned (the regulators specify the architecture) |
| Wide span (multi-GHz), low-resolution survey | Swept-tuned (single LO sweeps the full range) |
| High dynamic range, narrowband, no time pressure | FFT (deeper noise floor per pass) |
| Real-time waterfall / spectrogram display | FFT (overlapping FFTs at hop rate) |
| Pulsed RF / transient capture | FFT (no sweep-time tradeoff; one FFT captures the event) |
| Educational / pedagogical demo of the architecture | Swept-tuned (the simplest mental model) |

Modern commercial analyzers ship *both* architectures and switch
between them depending on the measurement, which is what this library
supports through its parallel pipelines.

## The RBW / VBW / averaging tradeoff

These three knobs together control the noise-vs-speed-vs-resolution
tradeoff every spectrum analyzer makes. They're independent in
principle but related in practice.

### RBW (Resolution Bandwidth)

The RBW filter sits between the mixer and the detector in a swept
analyzer; in an FFT analyzer the equivalent is the bin width
(`Fs / N`). The RBW determines:

- **Frequency resolution** — two adjacent spectral lines closer than
  ~RBW apart can't be distinguished as separate.
- **Noise floor (per bin)** — narrowing the RBW reduces the noise
  power per bin proportionally (`-3 dB` per RBW halving).

The library's [`RBWFilter`](https://github.com/stillwater-sc/mp-dsp/blob/main/include/sw/dsp/spectrum/rbw_filter.hpp)
implements a synchronously-tuned cascade of N RBJ band-pass biquads.
The cascade-N choice sets the *shape factor* — the 60 dB / 3 dB
bandwidth ratio:

| N | Shape factor | Use |
|---|---|---|
| 1 | ~2010 | Effectively unusable (RBW skirts overlap heavily) |
| 3 | ~16 | Survey-grade |
| 5 | ~10 | Comparable to a Gaussian filter (the demo's default) |
| 8 | ~6 | Near-Gaussian shape factor |

### VBW (Video Bandwidth)

The VBW is a post-detector low-pass filter that smooths the trace.
Lower VBW = more averaging = lower-noise trace at the cost of slower
response to changes. The library's
[`VBWFilter`](https://github.com/stillwater-sc/mp-dsp/blob/main/include/sw/dsp/spectrum/vbw_filter.hpp)
is a matched-z single-pole IIR (`alpha = 1 - exp(-2π fc / fs)`) — the
simplest filter that does the job, bumpless on retune.

### Trace averaging

The third smoother, and the only one that operates *across* sweeps
rather than within one. Five modes ship:

- **Linear** — cumulative arithmetic mean (the classic noise-floor
  smoother).
- **Exponential** — single-pole IIR across sweeps (the "live"
  smoother that keeps tracking changes).
- **MaxHold** — element-wise max across all sweeps since reset
  (transient capture).
- **MinHold** — element-wise min.
- **MaxHoldN** — max-hold over a rolling window of the last N sweeps.

### When to use which smoother

| Goal | Tool |
|---|---|
| Lower the noise floor of a single trace | Tighten RBW |
| Smooth ripples that change between sweeps | Lower VBW |
| Build up the noise floor over many sweeps (e.g., for a long EMI scan) | Linear trace averaging |
| Catch a transient spike that any one sweep might miss | MaxHold trace averaging |
| Get a "live" smoothed display that tracks drift | Exponential trace averaging |

Real analyzers expose all three knobs because they answer different
questions; the library mirrors that.

## The mixed-precision contract

Every numerically-active stage takes
`(CoeffScalar, StateScalar, SampleScalar)`, with comparison-only or
copy-only stages parameterized on a single `SampleScalar`. The
contract for each stage:

| Stage | Streaming arithmetic? | Precision driver |
|---|---|---|
| EqualizerFilter / FrontEndCorrector | Yes (FIR multiply-accumulate) | `(EqCoeff, EqState, EqSample)` |
| RealtimeSpectrum (FFT) | Yes (twiddle multiplies) | `CoeffScalar` (twiddles + butterflies) |
| RBW Filter (cascade of biquads) | Yes (per-biquad recursion) | `(CoeffScalar, StateScalar)` |
| VBW Filter (single-pole IIR) | Yes (recursion) | `StateScalar` (full-state precision through feedback) |
| Swept LO (phase accumulator) | Yes (cos/sin generation) | `(CoeffScalar, StateScalar)` |
| Detectors (peak/RMS/etc.) | RMS / Average yes; Peak / Sample no | `T` for RMS; comparison-only otherwise |
| Trace Averager | Linear / Exp yes; MaxHold/MinHold/MaxHoldN no | `SampleScalar` |
| Waterfall Buffer | No (storage only) | `SampleScalar` |
| Markers (find_peaks etc.) | Sub-bin interpolation in double | `T` for ordering, double for return |

The headline pattern, the same one the
[scope demo](./scope-demo/) demonstrates: storage is cheap to narrow
(comparison-only stages don't accumulate error); arithmetic is
expensive to narrow (errors compound across each cascade stage). The
[spectrum-analyzer demo](./spectrum-analyzer-demo/) makes that
tradeoff visible across both architectures and four precision plans.

## See also

- [End-to-End Spectrum Analyzer Demo](./spectrum-analyzer-demo/) —
  the runnable capstone that exercises both architectures across
  four precision plans.
- [End-to-End Scope Demo](./scope-demo/) — the companion
  oscilloscope-side capstone, demonstrating the same
  precision-of-storage-vs-precision-of-arithmetic tradeoff in a
  different topology.
