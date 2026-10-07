---
title: End-to-End Demo
description: Capstone walkthrough composing NCO, mixer, polyphase decimation, and the precision-analysis primitives into a configurable IF receiver
---

The library ships a runnable demo at
[`applications/acquisition_demo/`](https://github.com/stillwater-sc/mp-dsp/tree/main/applications/acquisition_demo)
that ties the entire data-acquisition stack together: a simulated ADC,
a DDC composing NCO + complex mixer + polyphase decimation, two
parallel `DecimationChain` instances handling the I and Q baseband
streams, and the
[`acquisition_precision`](../../analysis/acquisition-precision/)
analysis primitives — all swept across a representative range of
number-system configurations.

This page walks through the topology, the design decisions that fall
out of mixed-precision constraints, and how to read the output.

## Pipeline

```text
+------------------------+    +-----------------------------+
| simulate_adc(N_bits)   | -> | DDC<Coeff,State,Sample> {   |
|   real cosine + noise  |    |   NCO at f_IF / f_S         |
|   N-bit uniform quant  |    |   complex mixer (×conj)     |
+------------------------+    |   PolyphaseDecimator ↓2     |
                              | }                           |
                              +-----------------------------+
                                            |
                                  split into I, Q streams
                                            |
                              +-------------+-------------+
                              v                           v
              +-----------------------+   +-----------------------+
              | DecimationChain {     |   | DecimationChain {     |
              |   CICDecimator ↓2     |   |   CICDecimator ↓2     |
              |   HalfBandFilter ↓2   |   |   HalfBandFilter ↓2   |
              |   PolyphaseDecimator  |   |   PolyphaseDecimator  |
              |     ↓2                |   |     ↓2                |
              | }                     |   | }                     |
              +-----------------------+   +-----------------------+
                              |                           |
                              +-------------+-------------+
                                            v
                                +------------------------+
                                | I/Q baseband at fs/16  |
                                +------------------------+
                                            |
                                            v
                                +------------------------+
                                | snr_db / enob          |
                                | vs uniform-double ref  |
                                +------------------------+
                                            |
                                            v
                                  acquisition_demo.csv
```

The DDC handles the first ↓2 (anti-alias + complex baseband
conversion). The post-DDC `DecimationChain` does the remaining ↓8 in
three Hogenauer-classic stages: CIC for the bulk rate change, half-band
for transition-band shaping, and a polyphase FIR for the final
channel-defining decimation. Total decimation is ↓16, exercising every
multistage primitive in the acquisition module.

Each row in the CSV captures one (CoeffScalar, StateScalar,
SampleScalar) configuration's quality against the reference. The
schema matches the
[`AcquisitionPrecisionRow`](../../analysis/acquisition-precision/#csv-export)
format from the analysis module, so the same Python tooling that
reads `precision_sweep.csv` reads this file's identifier columns.

## ADC simulation

```cpp
mtl::vec::dense_vector<double> simulate_adc(int adc_bits, unsigned seed = 0xACDC);
```

A real-valued cosine at the IF frequency, plus low-level AWGN, then
uniform quantization to a signed N-bit ADC's $2^N$ codes (range
$[-2^{N-1}, 2^{N-1}-1]$ — the standard two's-complement layout). The
quantization step is $1 / 2^{N-1}$, the full-scale signed step for
N-bit signed representation, so the SNR floor follows the classic
$6.02 N + 1.76$ dB ceiling for any later stage downstream.

## Pipeline composition

```cpp
template <class CoeffScalar, class StateScalar, class SampleScalar>
std::vector<std::complex<double>>
run_pipeline(const mtl::vec::dense_vector<double>& adc_in_double);
```

The pipeline projects ADC samples into `SampleScalar`, runs them
through a `DDC<CoeffScalar, StateScalar, SampleScalar>` (NCO + mixer
+ ↓2 polyphase), splits the complex output into separate I and Q
real-valued streams, then feeds each stream through its own
`DecimationChain<SampleScalar, CICDecimator, HalfBandFilter,
PolyphaseDecimator>` for an additional ↓8. Recombines I and Q at the
output and casts to `std::complex<double>` for cross-precision
comparison.

Each component does its own work — see the [DDC](./ddc/),
[Polyphase Decimator](./polyphase-decimator/),
[CIC](./cic/), [Half-Band](./halfband/), and
[DecimationChain](./decimation-chain/) references for the math. The
demo's contribution is the configuration sweep infrastructure that
threads `(CoeffScalar, StateScalar, SampleScalar)` through every
stage uniformly.

### Filter design: in `double`, projected to `CoeffScalar`

All three filter designs (DDC anti-alias FIR, half-band, post-DDC
polyphase) are computed in `double` and then projected to
`CoeffScalar` via `static_cast`. This deliberately deviates from the
T-parameterized design pattern the rest of the library follows
(see the
[T-parameterization audit](https://github.com/stillwater-sc/mp-dsp/issues/111))
because we want the SNR measurement to isolate **streaming
arithmetic** precision from **filter-design** precision. If we
designed taps at `CoeffScalar`, the test and the `double` reference
would run through different taps (Remez at `posit` converges to
slightly different values than at `double`), so SNR would conflate
filter-design variance with the arithmetic quality we're trying to
measure. `fixpnt` is a stronger example: `design_halfband<fixpnt>`
trips divide-by-zero in the Remez iteration because `fixpnt` doesn't
have the dynamic range Remez assumes. Design once in `double`,
project for each test config — every configuration runs through
identical taps.

## Three mixed-precision design decisions

These come up the moment you try to instantiate the pipeline at
non-IEEE precision. They're worth understanding because they recur in
any real embedded DSP design.

### 1. Normalized rates

The `DDC` constructor takes a `StateScalar sample_rate` argument, but
the NCO inside only ever uses `frequency / sample_rate`. If you pass
absolute Hz (say `1.0e6`) and the StateScalar is a narrow type (e.g.,
`fixpnt<32, 28>` with range $\pm 8$), construction throws because the
sample rate saturates.

The fix is mathematically trivial:

```cpp
const double f_norm = params.if_frequency_hz / params.sample_rate_hz;
DDC_t ddc(static_cast<StateScalar>(f_norm),
          static_cast<StateScalar>(1.0),
          ddc_decim);
```

Pass `(IF/fs, 1.0)` instead of `(IF, fs)`. The phase increment ends up
identical (`f_norm / 1.0 == IF / fs`), and the StateScalar values stay
in $[0, 1]$ where every sane scalar type can represent them.

This is a general principle for embedded DSP: parameterize on
dimensionless ratios, not absolute units, whenever the underlying math
only needs the ratio.

### 2. Q4.28 (not Q16.16) for fixpnt

A first attempt at fixpnt parameterization picked `fixpnt<32, 16>`
(Q16.16 — 16 integer bits, 16 fractional). That gives ample integer
range ($\pm 32{,}768$) but only $2^{-16} \approx 1.5 \times 10^{-5}$
precision. The post-mixer baseband signal lives in $[-0.5, +0.5]$
where 16 fractional bits is poor. The configuration measured **0
ENOB**.

The fix is to choose the Q-format for the *signal range*, not for
some abstract "32-bit fixed-point". The demo uses `fixpnt<32, 28>`
(Q4.28):

- 4 integer bits → range $\pm 8$, comfortable for the post-mixer
  $[-0.5, 0.5]$ signal even after a few decibels of FIR-stage gain
- 28 fractional bits → precision $2^{-28} \approx 4 \times 10^{-9}$,
  competitive with IEEE float

Result: Q4.28 fixpnt measures ~15 ENOB through this pipeline, the
best of any non-double configuration. The lesson is that fixpnt is
a *format choice*, not a number system; pick the integer/fractional
split based on the signal you'll actually run through it.

### 3. CIC requires fixed-point (or any wrapping) state

The biggest surprise of this demo: with a CIC decimator in the chain,
**`uniform_fixpnt32` beats every floating-point configuration by
~35 dB**, and `uniform_posit16` collapses to negative SNR.

The cause is a structural property of CIC. A CIC integrator is
`y[n] = y[n-1] + x[n]` — an unbounded accumulator. The classical
Hogenauer derivation works only because **the accumulator wraps in
two's-complement modular arithmetic**, and the matching comb stage
subtracts the same wrapped value to recover the bounded result. Any
state representation that doesn't wrap modularly — `float`, `double`,
`posit` — accumulates round-off on every DC-bias sample, and that
error never gets cancelled by the comb. The integrator slowly drifts.

You can see this directly in the table below: float, posit32, and
cfloat32 (all with ~24 bits of mantissa precision) cluster around
58 dB SNR — that's the integrator drift floor, not the arithmetic
ceiling. fixpnt32, with hardware-level two's-complement wrap,
recovers ~93 dB. posit16's 4 mantissa bits aren't enough to keep
the integrator stable at all, hence the -28 dB collapse.

The lesson: **if you have a CIC stage, use fixed-point (or any
wrapping integer-like type) for the integrator state.** The
[CIC reference](./cic/) covers the math; this demo is the empirical
evidence.

## Reading the output

A clean run with a 16-bit ADC at 1 MHz, IF at 100 kHz, decimating
↓16 to a 62.5 kHz baseband:

```text
=== Number-system sweep (16-bit ADC) ===
Configuration                   Bits    SNR(dB)    ENOB
-------------------------------------------------------
uniform_double                   192        inf     ref
uniform_float                     96      57.99    9.34
uniform_posit32                   96      58.39    9.41
uniform_posit16                   48     -28.36   -5.00
uniform_cfloat32                  96      57.99    9.34
uniform_fixpnt32                  96      92.76   15.12
mixed_double_p32_p16             112      57.16    9.20
mixed_double_double_float        160     145.89   23.94
mixed_double_p32_float           128      58.39    9.41
mixed_double_float_float         128      57.99    9.34
mixed_double_fx32_fx32           128      92.76   15.12

=== ADC bit-depth scan (uniform-double pipeline) ===
ADC bits        SNR(dB)    ENOB
-------------------------------
8-bit             54.34    8.73
12-bit            78.28   12.71
14-bit            91.08   14.84
16-bit           102.54   16.74
```

These numbers measure the **complex-residual SNR** between the test
configuration's I/Q output and the uniform-double reference's I/Q
output (i.e., the demo computes
$10 \log_{10} \frac{\sum |r_i|^2}{\sum |r_i - t_i|^2}$ on the
complex baseband stream). That captures both magnitude and phase
errors. An earlier version reduced each stream to its magnitude
envelope before comparison, which silently masked phase-only error
and over-stated `fixpnt32` quality by ~60 dB.

A few patterns to note:

- **`uniform_fixpnt32` (Q4.28) wins by ~35 dB.** This is the CIC
  state-precision lesson described above. The Q4.28 format gives
  enough fractional precision (~28 bits) to match the post-DDC
  signal range, and the hardware-level two's-complement wrap is
  exactly what the CIC integrator needs.
- **All three 32-bit floating-point options cluster at ~58 dB SNR.**
  `uniform_float`, `uniform_posit32`, and `uniform_cfloat32` are
  all bounded by the same physics: float-state CIC integrators drift
  on DC bias, regardless of mantissa precision. This is *not* a flaw
  in float or posit; it's a CIC design constraint that the demo
  surfaces empirically.
- **`uniform_posit16` collapses to negative SNR (-28 dB).** Posit16
  has 4 mantissa bits at unity. That's nowhere near enough to keep
  the integrator stable, so the output is dominated by drift noise.
  This is a useful warning: narrow floating-point types in CIC chains
  do not gracefully degrade — they fall off a cliff.
- **`mixed_double_double_float`** keeps state in `double` but samples
  in `float`. Double-precision integrator state is enough to suppress
  the drift floor by ~93 dB compared to all-`float`, recovering ~24
  ENOB. If you must use floating-point, this is the right precision
  split.
- **The ADC scan** confirms the textbook $\text{SNR} \approx 6.02 N + 1.76$ dB
  ceiling propagates end-to-end: the multistage decimation gains a
  few dB of averaging beyond the raw ADC ceiling, but the slope and
  intercept match.

## Running it

```bash
cmake -B build
cmake --build build --target acquisition_demo
./build/applications/acquisition_demo/acquisition_demo [OPTIONS] [csv_path]
```

Options (all are key=value):

| Flag | Default | Meaning |
|---|---|---|
| `--if-freq=<Hz>` | `100000` | IF frequency of the simulated tone |
| `--sample-rate=<Hz>` | `1000000` | ADC sample rate |
| `--adc-bits=8,12,14,16` | `8,12,14,16` | Comma-separated bit depths for the ADC scan |
| `--num-samples=<N>` | `4096` | Input block length |
| `--csv=<path>` | `<build>/demo-output/acquisition_demo.csv` | Output CSV path |
| `-h`, `--help` | — | Print usage |

The CSV path may also be passed positionally for backwards
compatibility. The console table is printed regardless.

## Extending the sweep

The demo's structure is intentionally flat — a single
`sweep_configurations()` function holds all the rows. To add a
configuration:

```cpp
rows.push_back(measure_config<MyCoeff, MyState, MySample>(
    "config_label",
    "MyCoeff",      // for the CSV
    "MyState",
    "MySample",
    coeff_bits + state_bits + sample_bits,
    adc_in,
    reference_out));
```

The reference is always uniform-double, computed once at the top of
the sweep. Every row is compared against the same reference, which is
what makes the SNR column meaningful as a cross-row metric.

## See also

- [DDC](./ddc/) — front-end mixer + polyphase decimator
- [CIC](./cic/) — first stage of the post-DDC chain
- [Half-Band Filter](./halfband/) — second stage; transition-band shaping
- [Polyphase Decimator](./polyphase-decimator/) — channel-defining final stage
- [Decimation Chain](./decimation-chain/) — variadic-tuple multistage composition used twice (one I, one Q)
- [Acquisition Precision Analysis](../../analysis/acquisition-precision/) — the SNR/ENOB primitives the demo measures with
