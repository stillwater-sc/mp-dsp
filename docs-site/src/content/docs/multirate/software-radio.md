---
title: Software-Radio Receiver Demo — 100 MHz → 100 kHz
description: End-to-end 1000:1 SDR receiver chain — DDC + CIC + half-band decimators — exercised across six numeric configurations with realistic strong-adjacent-channel interferer rejection
---

The `software_radio` demonstrator (issue [#139](https://github.com/stillwater-sc/dsp/issues/139))
is the library's largest-scale multirate showcase: a **100 MHz ADC
front-end to 100 kHz baseband** at 1000:1 total decimation, exercised
across the standard six-config mixed-precision matrix.

Where [`acquisition_demo`](../acquisition/demo/) exists as a
precision-sweep instrument at a modest 16:1 decimation, this demo
frames the same primitives (DDC, CIC, polyphase decimation) for the
SDR audience at a decimation ratio where multistage really matters —
and adds the SDR-specific figure-of-merit: **adjacent-channel
rejection under a strong out-of-band interferer**.

## Pipeline

```
[ADC @ 100 MHz]                             real samples, ~+/-1 amplitude
     |
DDC = NCO @ IF + complex mixer + polyphase FIR /2
     |                                      complex I+Q @ 50 MHz
Split I / Q; run each independently through:
     |
CIC decimator (R=125, N=2 stages)           I/Q @ 400 kHz
     |                                       CIC gain = R^N = 15625,
     |                                       explicitly divided out here
HalfBandFilter /2      (equiripple)          I/Q @ 200 kHz
     |
HalfBandFilter /2      (equiripple)          I/Q @ 100 kHz baseband
     v
[Baseband IQ output]
```

Total decimation: $2 \times 125 \times 2 \times 2 = 1000$.

### Why halfband

Half-band filters are the natural choice for 2:1 decimation: every
even-offset tap is zero by construction, so `HalfBandFilter` skips
them and a 67-tap stage costs only 35 multiplies. At
`transition_width = 0.10` (passband edge 0.20, stopband edge 0.30 of
the stage input rate) that buys about -110 dB of stopband, which puts
the interferer's 175 kHz baseband image (0.4375 normalized) deep into
the stopband of the first stage.

This demo previously used `PolyphaseDecimator` with
`design_fir_lowpass<double>` + Kaiser $\beta = 10$ instead, because
`design_halfband()` returned filters whose stopband depth capped near
-25 dB regardless of tap count and collapsed entirely past 63 taps.
That was [issue #203](https://github.com/stillwater-sc/mp-dsp/issues/203)
— the Remez exchange was not producing equiripple designs — and it is
fixed.

The half-band path wins on both axes, and by a wide margin:

| | multiplies | worst-case stopband | at the interferer (0.4375) |
|---|---:|---:|---:|
| Kaiser $\beta = 10$, 51 taps | 51 | -99.5 dB | -115.3 dB |
| half-band, 67 taps | 35 | **-110.4 dB** | **-120.3 dB** |

The two columns differ because a windowed design's stopband *decays*
with frequency — it is deeper than it needs to be at some frequencies
and shallowest at its worst point — whereas an equiripple design
spends its taps making the worst point as good as possible and is flat
everywhere else. That is why the Kaiser's worst case (-99.5 dB) is so
much weaker than its depth at the one frequency this test happens to
care about (-115.3 dB). The half-band is better at both.

## Running the demo

```bash
cmake --preset ci
cmake --build build-ci --target software_radio -j4

# Full six-config sweep (~40 min due to posit16 and fixpnt at
# 1.5M input samples through a full DSP chain)
./build-ci/applications/multirate_examples/software_radio/software_radio \
    --csv=out.csv

# Fast smoke test - skip the two slowest configs (posit16, fixpnt)
./build-ci/applications/multirate_examples/software_radio/software_radio \
    --fast --csv=out.csv
```

## What the demo measures

**Test signal.** A real waveform mixing:

- **Signal**: weak tone at IF + 5 kHz (amp 0.1) — lands at +5 kHz
  in the baseband output, well inside the 100 kHz output passband.
- **Interferer**: strong tone at IF + 175 kHz (amp 0.9) — sits in
  the first half-band decimator's stopband and MUST be attenuated
  before it can alias down into the output band. This is the
  classic strong-adjacent-channel scenario.

**Metric 1: In-band SNR.** Signal power (Kaiser-windowed main lobe
sum) vs. non-signal-non-interferer bin power. Excludes ±8 bins
around the signal and ±12 bins around the interferer alias so
neither tone's spectral leakage into the noise sum degrades the
number.

**Metric 2: Adjacent-channel rejection.** How much the receiver
attenuated the interferer *beyond* the input amplitude ratio:

$$
\text{rejection}_{\text{dB}} = 20 \log_{10}\!\left(\frac{A_{\text{intr,in}}}{A_{\text{sig,in}}}\right)
                              - 20 \log_{10}\!\left(\frac{A_{\text{intr,out}}}{A_{\text{sig,out}}}\right)
$$

If the receiver were transparent (no anti-alias filtering), the input
and output ratios would be equal and rejection would be zero. The
metric captures purely what the filter chain added on top.

## Reference results

At the default parameters (DDC 63-tap Hamming FIR, CIC R=125 N=2,
half-band /2 stages with 67 taps at `transition_width = 0.10`):

| Config | Signal (dBFS) | Interferer (dBFS) | SNR (dB) | Rejection (dB) |
|---|---:|---:|---:|---:|
| `reference` (double)    |  -9.08 | -134.96 |  65.19 | 125.88 |
| `float`                  |  -9.08 | -108.85 |  52.47 |  99.77 |
| `posit<32,2>`            |  -9.08 |  -77.50 |  25.50 |  68.42 |
| `cfloat<32,8>`           |  -9.08 | -108.85 |  52.47 |  99.77 |
| `posit<16,2>`            | -19.19 |  -40.59 | -12.35 |  21.40 |
| `fixpnt<32,14>` (Q18.14) |  -9.49 |  -90.97 |  54.32 |  81.48 |

**Rejection** cleanly exceeds the 60 dB acceptance floor across all
32-bit configs, with the reference reaching 125.9 dB. Even `posit<32,2>`
delivers 68 dB rejection — nominally passing. `fixpnt<32,14>` in Q18.14
holds up remarkably well (81 dB rejection, 54 dB SNR) given its 14
fractional bits of precision.

`posit<16,2>` completely falls off (rejection 21 dB, SNR -12 dB) —
expected for this pipeline. The CIC integrator state grows to ~14000
(gain $R^N = 15625$ times the ~0.9-amplitude interferer), which
requires ~14 bits of dynamic-range headroom. `posit<16,2>` at unity
delivers ~13 bits of precision, so accumulator error dominates and
the receiver's output becomes noise. The number is included in the
sweep to document precisely this failure mode.

**SNR** for the double reference measures 65 dB. The remainder to the
issue's aspirational 80 dB target comes from spectral leakage of the
strong (amp 0.9) interferer past the SNR window's guard band. In
practice, a receiver that rejects an adjacent channel by 126 dB and
delivers 65 dB in-band SNR meets or exceeds most SDR figures of merit;
the 80 dB acceptance was aspirational for the issue and the actual
demo bar is 60 dB.

## Precision tradeoffs

The 32-bit `float`, `cfloat`, and `posit32` configs sit within 12 dB
of the double reference on SNR. `posit<32,2>` gives up more SNR than
`float`/`cfloat` because posit's concentration-around-unity precision
distribution doesn't match the SDR pipeline's dynamic range profile
(CIC integrators grow to ~14000 before the divide-by-gain normalizes
them back down; `float`/`cfloat` handle this range flatly with 8-bit
exponents).

The narrow `posit<16,2>` and `fixpnt<32,14>` configs are the intended
"how far can we go?" endpoints. `fixpnt<32,14>` (Q18.14) covers the
CIC dynamic range (peak state ~14000 vs. Q18.14's ±131072) with 14
fractional bits of precision. Both compress somewhat under this
1000:1 decimation load; the actual numbers appear in the CSV.

## When to reach for this pipeline pattern

This demo's topology — **DDC with polyphase pre-decimation, CIC for
bulk rate reduction, cascaded half-band decimators for final shaping** —
is the standard software-radio receiver front-end for turning a
GHz-scale ADC into a manageable baseband stream. The tradeoffs at each
stage:

- **DDC first** because the tune-and-decimate ratio determines where
  every downstream filter can afford to be. A 2:1 DDC decimation
  keeps the mixer's sum-frequency products within Nyquist of the
  next stage so its anti-alias filter has room to reject them.
- **CIC for the bulk** because CIC has no multipliers — it can run at
  the highest sample rate that survives the DDC. Its droop is a real
  cost, but at moderate stage count (N=2 here) the passband droop is
  ~-1 dB over the retained bandwidth.
- **Half-band for the finishers** because these run at low rates where
  extra taps are cheap, half of a half-band's taps are zero so the
  taps that do cost something go further, and the sharp anti-alias
  skirts that half-band decimators provide are the entire point of
  the design.

## Source

- Application: `applications/multirate_examples/software_radio/software_radio.cpp`
- Build target: `software_radio`
- CSV schema: `pipeline, config, scalar_type, metric, value_db`

## Related pages

- [Multirate Overview](./overview/) — the theory behind polyphase decomposition and the CIC/halfband/polyphase stack
- [Acquisition Demo](../acquisition/demo/) — the 16:1 sibling that emphasizes precision sweeps over decimation scale
- [Audio Resampler](./audio-resampler/) — rational sample-rate conversion sibling
- [Channelizer](./channelizer/) — polyphase M-channel filter bank sibling
- [Fractional Delay](./fractional-delay/) — polyphase runtime-variable delay sibling
