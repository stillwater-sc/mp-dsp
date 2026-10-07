---
title: End-to-End Spectrum Analyzer Demo
description: Capstone walkthrough wiring the front-end corrector, swept LO, RBW filter, VBW filter, detectors, real-time spectrum, trace averaging, waterfall buffer, and markers into both spectrum-analyzer architectures with a per-stage mixed-precision sweep
---

The library ships a runnable spectrum-analyzer demo at
[`applications/spectrum_analyzer_demo/`](https://github.com/stillwater-sc/mp-dsp/tree/main/applications/spectrum_analyzer_demo)
that exercises the entire `spectrum/` module end-to-end and **runs
both analyzer architectures in parallel** at four precision plans.
A synthetic three-tone signal (50 / 100 / 150 kHz at 0 / -30 / -60 dBc)
plus a -80 dBc spurious at 175 kHz plus AWGN flows through:

1. The **FFT path** — `EqualizerFilter` → `RealtimeSpectrum` →
   `TraceAverager` → `WaterfallBuffer` → `find_peaks`
2. The **swept-tuned path** — `EqualizerFilter` → mixer × `SweptLO`
   → `RBWFilter` → square-law detector → `VBWFilter` →
   trace memory indexed by LO frequency

Both paths run for each of four precision plans (`reference`,
`arith_float`, `arith_posit32`, `arith_posit16`), producing 4 × 2 = 8
independent template instantiations. The output is a CSV trace plus a
console summary table that highlights how each architecture's dynamic
range degrades (or doesn't) as precision is narrowed.

This page walks through the topology, the per-stage precision
contract, and the headline mixed-precision finding.

## Pipeline

```text
                                +---------------------+
   simulate_input(double[])  -->| EqualizerFilter     | -- the
   (3-tone + spurious + AWGN)   | (FrontEndCorrector) |    precision-
                                +---------------------+    sensitive
                                          |                EQ stage
                                          v
                       +-----------+----------------+
                       |                            |
                  FFT path                  Swept-tuned path
                       |                            |
                       v                            v
                  RealtimeSpectrum            mixer × SweptLO
                  (Hann, 50% overlap)         (linear chirp)
                       |                            |
                       v                            v
                  TraceAverager                 RBWFilter
                  (Exponential)                 (5-stage sync-tuned)
                       |                            |
                       v                            v
                  WaterfallBuffer              square-law detector
                  (16 frames history)               |
                       |                            v
                       |                         VBWFilter
                       |                       (post-detector LPF)
                       |                            |
                       v                            v
                  find_peaks                  trace memory
                  (top-N + tolerance)         indexed by LO freq
                       |                            |
                       +-------------+--------------+
                                     |
                                     v
                              spectrum_analyzer_demo.csv
                              + console summary
                              + per-stage timing
                              + 10 GSPS comparison
```

## Per-stage precision contract

Each stage's precision requirement follows from its arithmetic, not
from a uniform "let's run everything in `T`" choice:

| Stage | What it does | Precision driver |
|---|---|---|
| `EqualizerFilter` | FIR multiply-accumulate per sample | **Streaming arithmetic** — narrowing here costs SNR in both paths |
| `RealtimeSpectrum` | Windowed FFT every `hop_size` samples | **FFT twiddle multiplies** — dominates the FFT path's dynamic range |
| `TraceAverager` | IIR per-bin smoothing across sweeps | StateScalar (single-pole IIR per bin); narrowing costs SNR slowly |
| `WaterfallBuffer` | Pure storage | Comparison-blind copy — narrowing is free |
| `SweptLO` | cos/sin per sample at varying phase increment | Phase precision sets SFDR (same as NCO) |
| `RBWFilter` | 5-stage cascade of RBJ bandpass biquads | **Cascaded recursion** — errors compound across stages |
| Square-law detector | `x * x` per sample | Wide enough type to avoid overflow |
| `VBWFilter` | Single-pole IIR across trace bins | Full-StateScalar feedback; bumpless retune |
| `find_peaks` / `harmonic_markers` | Comparison + sub-bin interpolation | Comparison in `T`, parabolic fit in double |

To bound template-instantiation explosion (4 plans × 2 paths = 8
instantiations), the demo parameterizes each pipeline on a single
`ArithScalar` that drives every numerically-active stage in that
path. The four plans then become:

- `reference` — `double` everywhere (the SNR baseline)
- `arith_float` — IEEE `float` for all stages
- `arith_posit32` — `posit<32, 2>`
- `arith_posit16` — `posit<16, 2>`

Trace storage and marker scoring stay in `double` regardless — they're
the reporting layer.

## Test signal

```cpp
// 3 tones at 50 / 100 / 150 kHz at 0 / -30 / -60 dBc
// + spurious at 175 kHz at -80 dBc
// + AWGN with sigma = 1e-5
// fs = 2 MHz, 65536 samples
const double a1 = pow(10, 0.0 / 20);   // tone1: full scale
const double a2 = pow(10, -30 / 20);
const double a3 = pow(10, -60 / 20);
const double as = pow(10, -80 / 20);   // spurious — the dynamic-range probe
```

The spurious is placed at 175 kHz — between tone3 (150 kHz) and the
swept-stop frequency, deliberately not coincident with any planted
tone or harmonic of one. It's the headline dynamic-range feature: an
analyzer that can recover -80 dBc above its noise floor distinguishes
a precision-preserving plan from a precision-destroying one.

## Calibration profile

```cpp
freqs    = {0.0,   50e3,  100e3, 250e3, 500e3, 1e6};
gains_dB = {0.0,  -0.2,   -0.5,  -1.0,  -2.0, -3.0};
phases   = {0.0,  -0.05,  -0.10, -0.20, -0.30, -0.40};
```

Synthetic mild rolloff with a small phase signature. The
`FrontEndCorrector` (which is a typedef alias for
`EqualizerFilter`) inverts this profile, applying a small high-band
boost that the analyzer pipeline then operates on. Same convention
as the [scope demo](./scope-demo/): keep the corrections under +2 dB
so the precision-impact comparison is the headline, not the
equalizer's design tradeoffs.

## Precision plans

Four plans, each parameterizing both paths' arithmetic-active stages
on a single `ArithScalar`:

```cpp
// reference: all double
run_fft_path  <double>(input, "reference", "double",      sizeof(double));
run_swept_path<double>(input, "reference", "double",      sizeof(double));

// IEEE float across the stages
run_fft_path  <float>(input,  "arith_float", "float",     sizeof(float));
run_swept_path<float>(input,  "arith_float", "float",     sizeof(float));

// posit<32,2>
run_fft_path  <posit<32,2>>(input, "arith_posit32", "posit<32,2>", 4);
run_swept_path<posit<32,2>>(input, "arith_posit32", "posit<32,2>", 4);

// posit<16,2> — the headline narrow-precision case
run_fft_path  <posit<16,2>>(input, "arith_posit16", "posit<16,2>", 2);
run_swept_path<posit<16,2>>(input, "arith_posit16", "posit<16,2>", 2);
```

## Sweep result

Plan labels are abbreviated below for column width; the actual demo
output truncates them to 27 characters. The full plan names are shown
here for readability:

```text
plan (path / arith)              B/samp tone1(kHz)  tone1(dB)  tone3(dB)     spur?  floor(dB)   SNR(dB)
-------------------------------------------------------------------------------------------------------
reference (fft/double)                8     50.006      59.53       0.64      PASS     -61.02       inf
arith_float (fft/float)               4     50.006      59.53       0.64      PASS     -61.02     60.06
arith_posit32 (fft/posit<32,2>)       4     50.006      59.53       0.64      PASS     -61.02     76.13
arith_posit16 (fft/posit<16,2>)       2     50.006      59.54      -0.31      fail     -27.57      5.71
reference (swept/double)              8     50.820     -14.66        NaN      fail     -60.02       inf
arith_float (swept/float)             4     50.815     -14.66        NaN      fail     -60.16     44.75
arith_posit32 (swept/posit<32,2>)     4     50.821     -14.66        NaN      fail     -60.06     57.39
arith_posit16 (swept/posit<16,2>)     2        NaN        NaN        NaN      fail    -137.76     -2.73
```

Read this table column-by-column:

- **`tone1(kHz)`** — frequency of the strongest tone, recovered from
  the trace by `find_peaks` + a tolerance-window `nearest_marker`. All
  4 FFT plans pin it within a single bin (3.6 kHz); 3 of 4 swept
  plans land within ~1 kHz; only `arith_posit16/swept` fails to find
  it at all.
- **`tone3(dB)`** — amplitude of the -60 dBc tone. FFT path recovers
  it in 3 of 4 plans; swept path can't recover it in *any* plan
  (it's below the swept architecture's RBW-skirt-bound noise floor).
- **`spur?`** — did the demo find the planted -80 dBc spurious 10 dB
  above the noise floor? Only the FFT path with reference / float /
  posit32 manages it.
- **`floor(dB)`** — measured noise floor (mean trace amplitude in
  bins free of planted features). FFT noise floor is consistent
  across all plans except posit16; swept noise floor is consistent
  across all plans except posit16 (which catastrophically loses the
  signal).
- **`SNR(dB)`** — output trace SNR vs. the same-path reference plan.
  The headline mixed-precision metric.

### What the SNR column actually means

`SNR(dB) = inf` for the reference plans (compared against
themselves). For every other plan, SNR is computed against the
*same-path* reference's trace — an FFT plan vs. the FFT reference,
a swept plan vs. the swept reference:

```text
SNR_dB(plan) = 10 * log10(sum(reference^2) / sum((reference - plan)^2))
```

Higher = "this plan's pipeline output matches the all-double
reference more closely". The path comparison isn't apples-to-apples —
the FFT and swept paths produce different trace shapes — so the
metric is meaningful *within* a path, not across paths.

## The mixed-precision findings

Two architectural / precision interactions, both visible in the table:

### FFT path: precision scales gracefully down to 32 bits

`arith_float`, `arith_posit32`, and `reference` all see the same
features at essentially the same amplitudes; their per-trace SNR
ranges from 60 dB (float) to 76 dB (posit32). The FFT's structure
helps here — the butterflies cancel errors via the symmetry of
the transform; small per-twiddle rounding errors don't compound the
way they would in a long IIR cascade.

`arith_posit16` is the cliff: the spurious disappears, the noise
floor jumps from -61 to -28 dB (33 dB worse), and the SNR drops to
5.7 dB. 16-bit arithmetic in a 4096-point FFT loses too much per
butterfly to recover the deep features.

### Swept-tuned path: precision matters until it doesn't, then it really matters

The swept path has a structural ceiling: the RBW filter's skirts
leak the strongest tone (50 kHz, 0 dBc) across the entire band at
roughly -60 dB, setting the architecture's noise floor regardless of
arithmetic precision. So `reference`, `arith_float`, and
`arith_posit32` all see the same ~-60 dB floor and detect only the
strongest tone — *not* because their precision differs, but because
the swept architecture itself is limited.

`arith_posit16` is *categorically* different: the long IIR cascade
(mixer + 5 RBW biquads + VBW) accumulates posit16 rounding errors
across each stage, the feedback loops drift, and the trace ends up
~138 dB below the reference (effectively zero). The signal is
*lost*, not just degraded.

### The headline takeaway

> **Pick the architecture for the dynamic range you need; pick the
> precision for the architecture you chose.**

Two complementary lessons:

1. **Architecture beats precision for dynamic range.** Even at
   double precision the swept path can't see the -60 dBc tone3 in
   this demo; the FFT path sees it at posit32 and float and
   reference. If you need 80 dB of dynamic range, no amount of
   precision will give it to you in a swept-tuned pipeline that
   can't dwell long enough at each LO frequency.

2. **For long IIR cascades, precision has a hard cliff.** Posit16
   is fine for the FFT path's butterflies (they self-cancel), but
   catastrophically wrong for the swept path's mixer + RBW + VBW
   cascade (errors compound stage by stage). The lesson generalizes
   to any pipeline with multiple recursive stages.

## Per-stage timing and the 10 GSPS comparison

```text
=== Per-stage timing (fft path, reference plan) ===
  equalizer        ~1500 ns/sample
  spectrum         ~3800 ns/sample    <-- dominates
  trace_avg         ~140 ns/sample
  waterfall          ~80 ns/sample
  markers              5 ns/sample
  TOTAL            ~5500 ns/sample

=== Per-stage timing (swept path, reference plan) ===
  equalizer        ~1400 ns/sample
  spectrum          ~240 ns/sample    (mixer + RBW + bin accumulation)
  vbw                  1 ns/sample
  markers              1 ns/sample
  TOTAL            ~1700 ns/sample
```

The FFT path is dominated by the FFT itself (~3800 ns/sample); the
swept path is dominated by the equalizer (~1400 ns/sample) and the
mixer + RBW cascade (~240 ns/sample). VBW and markers are essentially
free — they operate at the bin rate, not the sample rate.

Real 10 GSPS analyzers implement the equalizer, FFT, and RBW filter
in ASIC pipelines with hundreds of parallel taps; this single-CPU
implementation needs roughly 16,000× to 55,000× speedup to hit
10 GSPS. That gap is informational, not a target.

## Out of scope (deferred)

The capstone deliberately stops short of:

- **Real-analyzer sweep-time calculations.** The demo uses a fixed
  30 ms sweep duration; a real analyzer would auto-pick the sweep
  time from the RBW and span to ensure full settling. The library
  has the primitives; wiring up the auto-sweep-time logic is a
  follow-up.
- **CISPR-style quasi-peak detector.** Its decay-time semantics
  need a stateful class, not a stateless reducer; explicitly
  deferred from `detectors.hpp`.
- **Multi-segment / zoom FFT.** The FFT path uses a single
  `RealtimeSpectrum` instance; a real analyzer would compose
  multiple FFTs at different decimation rates for different
  zoom levels.
- **Display rendering.** CSV is the deliverable; a follow-up could
  produce an interactive HTML waterfall viewer from the
  `WaterfallBuffer` output.

## See also

- [Spectrum Analyzer Overview](./spectrum-analyzer-overview/) —
  the architectural reference for the swept-tuned vs. FFT
  tradeoffs and the full per-stage precision contract.
- [End-to-End Scope Demo](./scope-demo/) — the companion
  oscilloscope-side capstone, demonstrating the same
  precision-of-storage-vs-precision-of-arithmetic tradeoff in a
  different topology.
