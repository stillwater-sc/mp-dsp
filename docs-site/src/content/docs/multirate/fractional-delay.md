---
title: Polyphase Fractional-Delay Demo
description: Runtime-variable fractional-delay line implemented as a polyphase filter bank — 1/L sample resolution, flat group delay, mixed-precision across six numeric types
---

The `fractional_delay` demonstrator (issue [#138](https://github.com/stillwater-sc/dsp/issues/138))
exercises the **polyphase fractional-delay line** — an L-phase filter
bank that shifts an input stream by any multiple of 1/L samples at
runtime, without redesigning any coefficients between calls.

This class complements the existing static
[`sw::dsp::instrument::FractionalDelay`](https://github.com/stillwater-sc/mp-dsp/blob/main/include/sw/dsp/instrument/fractional_delay.hpp),
which redesigns its FIR whenever `set_delay()` is called. The polyphase
variant trades memory (L filter phases held in RAM) for the ability to
switch phases per-sample.

## The polyphase fractional-delay idea

An ideal fractional-delay filter with delay $d \in [0, 1)$ has
impulse response $h_d[n] = \operatorname{sinc}(n - c - d)$ for some
center $c$. Windowing to $K$ taps gives a length-$K$ FIR that
implements the desired delay.

The naive approach (which `instrument::FractionalDelay` uses) redesigns
the entire FIR whenever the delay changes. That's fine when the delay
is set once and left alone — a channel aligner correcting a fixed ADC
skew, for instance.

The **polyphase** approach pre-computes $L$ variants of this FIR, one
for each delay $d = p / L$ where $p = 0, 1, \ldots, L-1$. All $L$ phase
filters share the same taps-per-phase length $K$ (total coefficient
memory: $L \cdot K$). At runtime, selecting the delay is just a
`round(fractional_part * L)` and an index lookup — no design math per
sample.

Delay resolution is $1/L$ samples. Higher $L$ = finer resolution + more
memory. The demo defaults to $L = 64$ (1/64-sample resolution) and
$K = 15$ taps per phase (960 coefficients total).

## Running the demo

```bash
cmake --preset ci
cmake --build build-ci --target fractional_delay -j4
./build-ci/applications/multirate_examples/fractional_delay/fractional_delay \
    --csv=out.csv
```

## What the demo measures

### Test A: delay-accuracy sweep

At a fixed test tone (1 kHz), the demo requests delays of $\{7.0, 7.25,
7.5, 7.75, 7.9, 8.0\}$ samples and measures the actual implemented
delay via FFT phase analysis on the delayed tone.

**Measurement method.** A pure sinusoid $x[n] = \sin(2 \pi f n / f_s)$
delayed by $\tau$ samples becomes $y[n] = \sin(2 \pi f (n - \tau) /
f_s)$. In the frequency domain, this rotates the FFT bin at $f$ by
$-2 \pi f \tau / f_s$ radians. Reading the phase difference at that bin
and dividing back out gives $\tau$ directly — a linear measurement with
noise floor set by FFT SNR, not by grid discretization.

For frequencies where $2 f \tau / f_s > 1$ (i.e., the delay exceeds
half a tone period), the raw phase reading is wrapped modulo $2\pi$.
The demo passes the *requested* delay as a hint to disambiguate; the
recovered delay is the multiple of one tone period closest to the hint.

**Acceptance.** Reference (double) config within 1% of requested delay
for all sweep values.

### Test B: group-delay flatness

At a fixed requested delay (8.5 samples), the demo sweeps the input
tone frequency across $\{100, 500, 1000, 2000, 5000, 10000\}$ Hz and
measures the implemented delay at each frequency. A well-designed
polyphase filter has essentially flat group delay across its passband;
Test B verifies that empirically.

**Acceptance.** Reference group-delay spread across the swept
frequencies is less than 0.5 samples.

### Test C: precision sweep

Both tests run under the standard six-config mixed-precision matrix:

| Config | `StateScalar / SampleScalar` |
|---|---|
| `reference` | `double` |
| `float`     | `float` |
| `posit32`   | `posit<32,2>` |
| `posit16`   | `posit<16,2>` |
| `cfloat32`  | `cfloat<32,8>` (IEEE-like) |
| `fixpnt32`  | `fixpnt<32,24>` (Q8.24) |

Filter design fixed at `double` in all configs — the streaming
multiply-accumulate loop is the isolated axis under test.

## Reference results

At $L = 64$, $K = 15$, Kaiser $\beta = 8$:

| Requested delay | Reference measured | Error |
|---:|---:|---:|
| 7.000 | 7.0004 | +0.0004 |
| 7.250 | 7.2500 | -0.0000 |
| 7.500 | 7.4998 | -0.0002 |
| 7.750 | 7.7500 | -0.0000 |
| 7.900 | 7.9064 | +0.0064 |
| 8.000 | 8.0003 | +0.0003 |

Worst error is 0.08% of the requested delay — well under the 1%
acceptance threshold. The 7.9-sample request lands at phase index
$\operatorname{round}(0.9 \times 64) = 58$, corresponding to delay
$58/64 = 0.90625$ (matching the measured 7.9064 exactly), so the
"error" is really discretization to the nearest 1/64-sample grid step.

At requested delay 8.5, sweeping frequency:

| Tone (Hz) | Reference measured | Error |
|---:|---:|---:|
|   100 | 8.6163 | +0.1163 |
|   500 | 8.4979 | -0.0021 |
|  1000 | 8.4998 | -0.0002 |
|  2000 | 8.4999 | -0.0001 |
|  5000 | 8.5001 | +0.0001 |
| 10000 | 8.5001 | +0.0001 |

Group-delay spread across the passband is 0.12 samples — well under
the 0.5 acceptance threshold. The 100 Hz measurement drifts up because
at that frequency the tone period is 480 samples, so the phase-change
per sample is small and phase noise contributes more to the delay
readout.

Across all six precision configs the results are indistinguishable
from `double` to sub-thousandth-sample precision — `posit16` shows the
only visible deviation (~0.001 sample worst case), unsurprising given
the delay is essentially a phase multiplier bounded by measurement
noise, not by arithmetic precision.

## When to reach for this pattern

Use polyphase `multirate::FractionalDelay` when:

- The delay is **continuously variable** (clock-recovery loops, phase
  trackers, adaptive time alignment)
- The delay is **swept as part of analysis** (delay-vs-frequency
  characterization)
- One of a **small fixed set of values** is chosen per sample (channel
  aligners, multi-tap synthesis)

Use the static `instrument::FractionalDelay` when the delay is fixed
at construction and the memory overhead of $L$ phase filters isn't
justified by any runtime flexibility.

## Trade-off: L vs K

The design has two independent knobs:

- **$L$ (phases)**: sets the delay resolution ($1/L$ samples). Doubling
  $L$ doubles the coefficient memory but does *not* change per-sample
  compute (still $K$ multiplies per output).
- **$K$ (taps per phase)**: sets the filter quality. Longer $K$ =
  flatter passband, sharper transition, deeper stopband — at the cost
  of $K$ multiplies per output sample and $(K-1)/2$ samples of
  intrinsic group delay. **$K$ must be odd** (see below); the
  constructor throws otherwise.

For most applications, $K = 11$ to $K = 25$ is enough. Increase $K$
for stricter audio use (mastering, high-end reverbs), decrease for
low-latency real-time control loops. $L$ can go as high as memory
allows without changing runtime cost.

### Why $K$ must be odd

With odd $K$ the intrinsic group delay $(K-1)/2$ is an **integer**, so
phase 0 is $\mathrm{sinc}(k - \text{center})$ sampled on the integers —
exactly one non-zero tap, an unfiltered passthrough. A request at the
group-delay floor therefore costs nothing at all: measured, the phase-0
impulse response reproduces its input to $3\times10^{-17}$.

With even $K$ the floor lands on a half-integer, so **no** request maps
to an unfiltered tap and every output — including a nominally integer
delay — pays interpolation error and passband droop. That is why the
constraint exists, and why [#208](https://github.com/stillwater-sc/mp-dsp/issues/208)
was fixed by moving the default onto an odd value (11) rather than by
relaxing the check.

## Source

- Header: `include/sw/dsp/multirate/fractional_delay.hpp`
- Application: `applications/multirate_examples/fractional_delay/fractional_delay.cpp`
- Build target: `fractional_delay` (default preset `ci` builds it)
- CSV schema: `pipeline, test, config, scalar_type, tone_hz, requested_delay, measured_delay, error_samples, gain_db`

## Related pages

- [Multirate Overview](./overview/) — polyphase decomposition theory
- [Pattern Catalog](./patterns/) — full multirate problem→API mapping
- [`instrument::FractionalDelay`](https://github.com/stillwater-sc/mp-dsp/blob/main/include/sw/dsp/instrument/fractional_delay.hpp) — the static, design-at-construction variant. It has no docs-site page yet, so this points at the header.
- [Audio Resampler Demo](./audio-resampler/) — sibling multirate demonstrator using polyphase for rational rate conversion
