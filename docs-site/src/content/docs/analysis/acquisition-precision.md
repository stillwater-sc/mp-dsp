---
title: Acquisition Pipeline Precision Analysis
description: SNR, ENOB, NCO SFDR, CIC bit-growth verification, and per-stage noise budget primitives for high-rate acquisition pipelines
---

## What This Module Provides

`sw/dsp/analysis/acquisition_precision.hpp` collects the primitives
needed to characterize how arithmetic precision at each stage of an
acquisition pipeline (NCO, CIC, half-band, polyphase FIR, full
DDC/DecimationChain compositions) affects output quality.

Five primitives + one CSV export schema:

| Symbol | What it measures |
|---|---|
| `enob_from_snr_db(snr_db)` | Effective number of bits from SNR |
| `snr_db<RefT, TestT>(reference, test)` | SNR in dB between a reference and a test signal |
| `measure_nco_sfdr_db<NCO>(nco, fft_size, guard)` | Spurious-free dynamic range of an NCO output |
| `check_cic_bit_growth<CIC, Sample>(cic, input)` | Observed vs theoretical CIC bit growth |
| `AcquisitionPrecisionRow` + `write_acquisition_csv` | Pareto-row record + CSV writer matching the precision_sweep schema |

## ENOB and SNR

The standard ADC characterization formula relates SNR (in dB) to
effective number of bits (ENOB):

$$
\text{ENOB} = \frac{\text{SNR}_{\text{dB}} - 1.76}{6.02}
$$

derived for a sinusoidal full-scale input under quantization-noise-
dominated error. A 16-bit converter delivers ~98.08 dB SNR (16.0
ENOB); an ideal 24-bit converter, ~146.24 dB (24.0 ENOB).

`snr_db` computes

$$
\text{SNR}_{\text{dB}} = 10 \log_{10} \left( \frac{\sum |r_i|^2}{\sum |r_i - t_i|^2} \right)
$$

where $r$ is the reference (high-precision) signal and $t$ is the
test (lower-precision) signal. When the noise power underflows
double precision, the result is clipped to 300 dB to signal a
bit-identical match.

```cpp
#include <sw/dsp/analysis/acquisition_precision.hpp>
using namespace sw::dsp::analysis;

// reference computed in double, test computed in posit<16,1>
double s = snr_db(reference_output, posit_output);
double bits = enob_from_snr_db(s);
```

## NCO SFDR

`measure_nco_sfdr_db` characterizes an NCO's spurious-free dynamic
range:

1. Generate `fft_size` complex samples
2. Forward FFT (zero-padded to the next power of 2)
3. Locate the peak bin, then the largest spur outside a small guard
   band around the peak
4. Return $20 \log_{10}(\text{peak} / \text{spur})$ in dB

```cpp
NCO<sw::universal::posit<32, 2>> nco(posit32(137.0), posit32(4096.0));
double sfdr = measure_nco_sfdr_db(nco, /*fft_size=*/4096, /*guard_bins=*/2);
// posit<32,2> typically delivers > 150 dB SFDR for a bin-aligned tone
```

The test suite measures **319 dB** for a `double` NCO and **175 dB**
for `posit<32, 2>` at a bin-aligned tone (FFT-leakage limited rather
than precision-limited at this configuration).

## CIC Bit-Growth Verification

Hogenauer's classic result is that a CIC decimator with $M$ stages,
decimation ratio $R$, and differential delay $D$ has worst-case
output bit growth of

$$
B_{\text{growth}} = M \, \lceil \log_2(R \cdot D) \rceil
$$

`check_cic_bit_growth` validates this empirically: it runs a worst-
case input through the decimator, records the observed output peak,
and compares against the theoretical bound.

```cpp
CICDecimator<double> cic(/*R=*/4, /*M=*/3, /*D=*/1);
std::vector<double> dc_input(256, 1.0);  // worst case for DC
auto report = check_cic_bit_growth(cic,
    std::span<const double>(dc_input.data(), dc_input.size()));

// report.observed_bits == 6 (matches theoretical 6 = 3 * ceil(log2(4)))
// report.max_abs_output == 64.0 == (R*D)^M
// report.headroom_bits  == small positive value
// report.within_theory  == true
```

The all-ones DC input drives the CIC to its worst-case output peak
$(RD)^M$. Other inputs produce smaller observed values, so a
`within_theory: true` result on a non-worst-case input does **not**
guarantee safety — pair this measurement with the structural Hogenauer
formula at design time.

## Per-Stage Noise Budgeting

The library doesn't ship a single "give me the noise budget"
function — that would require deep introspection into every chain's
internal connectivity. Instead, the per-stage noise budget is a
**workflow** built on top of `snr_db`:

1. Construct two `DecimationChain` instances with identical structure
2. In one, swap the scalar type of the stage you want to characterize
   (e.g., the polyphase FIR runs at `posit<16, 1>` instead of `double`)
3. Run identical input through both
4. Apply `snr_db` to the outputs — the SNR quantifies how much noise
   that one stage's lower precision contributed

Sweeping this across stages produces the per-stage contribution
breakdown.

```cpp
// Stage 0 quantization study (CIC at posit instead of double):
auto out_ref      = double_chain.process_block(input);
auto out_with_cic_at_posit = mixed_chain.process_block(input);
double cic_contribution_db = snr_db(out_ref, out_with_cic_at_posit);
```

## CSV Export

`AcquisitionPrecisionRow` and `write_acquisition_csv` produce CSV
output that intentionally reuses identifier columns from the
[`precision_sweep.csv`](https://github.com/stillwater-sc/mp-dsp/tree/main/applications/precision_sweep)
schema (used by the library's Python visualization tooling), then
diverges into acquisition-specific metrics. The schemas are siblings,
not super/subsets — IIR precision sweeps and acquisition pipelines
care about different quality measures.

The full headers:

```text
precision_sweep.csv:
  pipeline, config, coeff_type, state_type, sample_type,
  sqnr_db, max_coeff_error, pole_displacement, stability_margin,
  passband_ripple_db, stopband_attenuation_db, condition_number

acquisition_precision.csv:
  pipeline, config_name, coeff_type, state_type, sample_type,
  total_bits, output_snr_db, output_enob, nco_sfdr_db,
  cic_overflow_margin_bits
```

Four identifier columns (`pipeline`, `coeff_type`, `state_type`,
`sample_type`) carry bit-identical names and meanings across both
schemas, so a visualization that groups by configuration tuple can
ingest either file. Column 2 is `config` in precision_sweep and
`config_name` here (same role, different name). The metric columns
are different by design and a unifying tool needs to switch on which
schema it's reading.

The acquisition columns:

| Column | Type | Notes |
|---|---|---|
| `total_bits` | int | sum of bit-widths across coeff / state / sample scalars |
| `output_snr_db` | double | end-to-end SNR vs a high-precision reference |
| `output_enob` | double | `enob_from_snr_db(output_snr_db)` |
| `nco_sfdr_db` | double | `-1` when N/A (no NCO in this configuration) |
| `cic_overflow_margin_bits` | double | `-1` when N/A (no CIC in this configuration) |

```cpp
std::vector<AcquisitionPrecisionRow> rows;
// ... populate one row per (pipeline, config) sweep point
write_acquisition_csv("acquisition_precision.csv", rows);
```

## Reference: Standard Pipeline ENOB Floors

Use these as targets when validating a posit/cfloat acquisition chain:

| Pipeline scalar (uniform) | Theoretical SNR floor | Theoretical ENOB |
|---|---|---|
| `posit<16, 1>` | ~73 dB | ~12 |
| `posit<24, 1>` | ~121 dB | ~20 |
| `posit<32, 2>` | ~169 dB | ~28 |
| `cfloat<32, 8>` (IEEE float) | ~146 dB | ~24 |
| `double` (IEEE-754) | ~314 dB | ~52 |

These are *type ceilings* — actual chain SNR is reduced by the
structural noise of the decimation filters and the accumulated
rounding through the chain. The library's regression tests measure
**98.3 dB / ~16 ENOB** for a 3-stage CIC → half-band → polyphase chain
in `posit<32, 2>` — well below the 169 dB type ceiling but more than
adequate for any realistic ADC source.

## See Also

- [Multi-Stage Decimation Chain](../acquisition/decimation-chain/) — the chain
  abstraction these primitives characterize
- [Polyphase Decimator](../acquisition/polyphase-decimator/) — channel-shaping stage
- [NCO](../acquisition/nco/) — phase-accumulator oscillator whose SFDR these
  primitives measure
- [CIC](../acquisition/cic/) — bit-growth analysis target
