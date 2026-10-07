---
title: Probe Domain Views
description: Free-function views that turn a captured probe stream into structured, CSV-dumpable data in time / magnitude / phase / I-Q domains
---

`sw::dsp::probe::` provides four **domain views** (sub-issue
[#156](https://github.com/stillwater-sc/mp-dsp/issues/156))
that take a [SignalProbe](./overview/) and produce the right kind of
structured data for a given analysis question. Each view exposes a
`dump_csv(path)` matching the schema families the mp-dsp-python
renderer understands.

## Views at a glance

| View | Input | Output | Use when... |
|---|---|---|---|
| [`time_view`](#time-view) | any probe | `sample_index, time_s, sample_value` | you want the raw samples with a time axis |
| [`magnitude_spectrum`](#magnitude-spectrum) | any probe | `freq_hz, magnitude_dB` | you want the windowed FFT magnitudes |
| [`phase_spectrum`](#phase-spectrum) | any probe | `freq_hz, phase_rad` | you want the (optionally unwrapped) phase per bin |
| [`iq_constellation`](#iq-constellation) | complex probe only | `sample_index, i, q` | you want the I/Q trajectory (e.g., after a DDC) |

All FFT-based views apply a windowing function (default Hamming) to
suppress spectral leakage. Zero-padding to the next power of two is
automatic — the library FFT accepts power-of-two lengths only.

## Time view

```cpp
#include <sw/dsp/probe/views.hpp>
using namespace sw::dsp::probe;

SignalProbe<double> p("adc", 4096, 48000.0);
// ... push samples ...
auto v = time_view(p);
v.dump_csv("adc.time.csv");
```

CSV schema:

```
sample_index,time_s,sample_value
0,0,0.5
1,2.0833e-5,0.4938
...
```

## Magnitude spectrum

**Real probes** → one-sided spectrum $[0, f_s/2]$ (N/2+1 bins).

**Complex probes** → two-sided spectrum in fftshift order
$[-f_s/2, +f_s/2)$ (N bins).

```cpp
auto spec = magnitude_spectrum(probe, WindowType::Hamming);
spec.dump_csv("post_mixer.mag.csv");
```

Available windows: `Rectangular`, `Hamming` (default), `Hann`,
`Blackman`, `Kaiser` (with optional `kaiser_beta` parameter).

CSV schema:

```
freq_hz,magnitude_dB
0,-42.1
93.75,-38.7
187.5,-36.4
...
```

Coherent-gain normalization: a full-scale tone at an exact bin
frequency reads ~0 dB in the magnitude column, so cross-config
comparisons are directly meaningful.

## Phase spectrum

```cpp
auto phase = phase_spectrum(probe, /*unwrap=*/true, WindowType::Hann);
phase.dump_csv("post_biquad.phase.csv");
```

- `unwrap=true` (default): consecutive bin phases are adjusted by
  $\pm 2\pi$ so the trace stays continuous. Useful when the filter's
  group delay is smooth across the passband.
- `unwrap=false`: raw phase values in $(-\pi, \pi]$.

CSV schema:

```
freq_hz,phase_rad
0,0.0000
93.75,-0.0142
...
```

## I/Q constellation

Complex-probe-only. Extracts the I (real) and Q (imag) components as
independent columns for plotting the constellation trajectory.

```cpp
SignalProbe<std::complex<double>> ddc_out("ddc", 4096, 50e6);
// ... push complex outputs ...
auto iq = iq_constellation(ddc_out);
iq.dump_csv("ddc.iq.csv");
```

CSV schema:

```
sample_index,i,q
0,0.7,0.0
1,0.4949,0.4949
2,0.0,0.7
3,-0.4949,0.4949
...
```

For a clean DDC output on a single-tone input, the constellation
traces a circle of radius equal to the tone's baseband amplitude.

## Choosing a window

Each windowing choice trades off main-lobe width against sidelobe
suppression:

| Window | Peak sidelobe (dB) | Main-lobe width (bins) | Best for... |
|---|---:|---:|---|
| Rectangular | -13 | 2 | Bin-aligned pure tones (best resolution) |
| Hann | -32 | 4 | General purpose, quick visual inspection |
| Hamming (default) | -42 | 4 | Balanced main-lobe + sidelobe |
| Blackman | -58 | 6 | Cleaner noise floor at the cost of resolution |
| Kaiser $\beta=12$ | -115 | 8 | Isolating a weak tone next to a strong one |

For the SDR-style measurement of a weak signal alongside a strong
adjacent-channel interferer, Kaiser $\beta = 12$ (or higher) is
usually the right choice — the sidelobe floor stays well below the
arithmetic noise, so measurements aren't limited by analysis-window
leakage.

## Related pages

- [Pipeline Probes Overview](./overview/) — probe capture infrastructure
- [Transfer Function Monitor](../transfer-function/overview/) — Bode
  sweeps + pole/zero extraction (complementary analytical tool)
- [Spectral Analysis](../spectral/overview/) — the underlying FFT
  primitives that magnitude/phase views build on
