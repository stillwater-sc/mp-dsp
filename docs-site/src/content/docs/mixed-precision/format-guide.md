---
title: Number Type Selection Guide
description: Choosing among integer, fixed-point, posit, logarithmic, and custom float formats for each stage of a DSP pipeline
---

The `sw::dsp` library supports five number systems from the
[Universal Numbers Library](https://github.com/stillwater-sc/universal).
Each format family offers different trade-offs in precision, dynamic
range, and hardware cost. This guide helps you choose the right type
for each position in the three-scalar model.

## The three-scalar positions

Every DSP algorithm in the library is parameterized by three types:

| Position | Role | Precision priority |
|----------|------|-------------------|
| `CoeffScalar` | Filter coefficients | Highest — design-time precision |
| `StateScalar` | Accumulator state | High — processing precision |
| `SampleScalar` | Input/output samples | Variable — streaming precision |

The historical DSP practice of using different word lengths at each
pipeline stage is exactly this model. Bell Labs telephony used Q15
coefficients with 32-bit accumulators and 12-bit ADC samples. The
TMS320 used 16-bit data with 32-bit MAC results. The library
formalizes what hardware engineers always did implicitly.

## Number type families

### 1. Fixed-point (`fixpnt<nbits, rbits>`)

Deterministic latency, uniform quantization grid, direct
correspondence to hardware multiplier widths. The natural format
for DSP.

**Sample I/O formats (fractional, normalized to [-1, +1)):**

| Universal type | Q-notation | Historical role |
|---------------|-----------|-----------------|
| `fixpnt<8,7>` | Q7 | 8-bit PCM |
| `fixpnt<12,11>` | Q11 | 12-bit ADC (radar, sonar) |
| `fixpnt<16,15>` | Q15 | 16-bit audio, TMS320 standard |
| `fixpnt<24,23>` | Q23 | Professional audio (24-bit ADC) |

**Coefficient formats (extra integer bits for headroom):**

| Universal type | Q-notation | Use case |
|---------------|-----------|----------|
| `fixpnt<16,14>` | Q2.14 | Coefficients with [-2, +2) range |
| `fixpnt<24,20>` | Q4.20 | Wide-range coefficients |
| `fixpnt<32,24>` | Q8.24 | High-precision design coefficients |

**Accumulator formats (double-width + guard bits):**

| Universal type | Q-notation | Use case |
|---------------|-----------|----------|
| `fixpnt<32,31>` | Q31 | Double-width for 16-bit paths |
| `fixpnt<40,31>` | Q9.31 | 16x16 MAC + 8 guard bits |
| `fixpnt<48,32>` | Q16.32 | Double-width for 24-bit paths |

**When to use fixed-point:**
- FPGA or ASIC targets where bit-width maps to silicon area
- Reproducing classical DSP processor behavior (TMS320, DSP56000)
- Applications where uniform quantization noise is acceptable
- When you need exact control over the quantization grid

### 2. Posit (`posit<nbits, es>`)

Tapered precision — highest accuracy near 1.0 where DSP signals
and filter coefficients concentrate. No NaN proliferation, no
denormal stalls, exact zero.

**Standard posits (es=2) — the posit equivalent of IEEE 754's hierarchy:**

| Type | Bits | Role |
|------|------|------|
| `posit<8,2>` | 8 | Ultra-low-precision edge/sensor |
| `posit<12,2>` | 12 | Compact real-time samples |
| `posit<16,2>` | 16 | Standard sample streaming |
| `posit<24,2>` | 24 | Balanced state accumulator |
| `posit<32,2>` | 32 | Coefficient design, high-precision state |

Consistent es=2 across all sizes means narrowing from p32 to p16 to p8
is a well-defined projection — the exponent field scales identically.

**High-precision posits (es=0) — maximum fraction bits:**

| Type | Precision near 1.0 |
|------|--------------------|
| `posit<8,0>` | 5 fraction bits |
| `posit<12,0>` | 9 fraction bits |
| `posit<16,0>` | 13 fraction bits |

With es=0, the regime directly encodes powers of 2. These are
attractive as coefficient representations where values cluster near
$\pm 1$ and extra fraction bits reduce coefficient quantization noise.

**Moderate-range posits (es=1) — balanced precision and dynamic range:**

| Type | Dynamic range | Precision near 1.0 |
|------|--------------|-------------------|
| `posit<8,1>` | ~13 decades | 4 fraction bits |
| `posit<12,1>` | ~20 decades | 8 fraction bits |
| `posit<16,1>` | ~26 decades | 12 fraction bits |

These compete directly with fixed-point for sample streaming: more
dynamic range than Q-format at the cost of non-uniform quantization.

**When to use posits:**
- Exploring whether tapered precision improves DSP quality
- Signals with wide dynamic range but activity near unity
- Pipelines where NaN/infinity handling is problematic
- Research into alternative arithmetic for FPGA/ASIC DSP

### 3. Custom float (`cfloat<nbits, es>`)

Miniature IEEE-like floating-point for ML/DSP crossover and
format parameter studies.

| Universal type | Analogue | Use case |
|---------------|----------|----------|
| `cfloat<8,4>` | FP8 E4M3 | ML training/inference |
| `cfloat<8,5>` | FP8 E5M2 | ML gradient format |
| `cfloat<16,5>` | IEEE binary16 | Standard half precision |
| `cfloat<16,8>` | bfloat16 | ML accelerator format |
| `cfloat<32,8>` | IEEE binary32 | Standard float |

**When to use custom floats:**
- ML inference pipelines that also need DSP preprocessing
- Studying the impact of exponent vs mantissa bit allocation
- Comparing IEEE-like formats against posits at the same bit-width

### 4. Logarithmic Number System (`lns<nbits, rbits>`)

Values stored as fixed-point logarithms: multiplication becomes
addition, division becomes subtraction. Addition requires table
lookup or CORDIC.

| Universal type | Fractional log bits | Accuracy |
|---------------|-------------------|----------|
| `lns<8,5>` | 5 | ~3% |
| `lns<16,10>` | 10 | ~0.1% |
| `lns<32,22>` | 22 | ~0.00002% |

**When to use LNS:**
- Multiply-heavy pipelines where additions are infrequent
- Power spectral density (multiplication in log domain is free)
- Studying whether logarithmic arithmetic reduces energy for
  coefficient application

### 5. Integer (`integer<nbits>`)

For quantization studies, ADC/DAC modeling, and direct
correspondence to converter word lengths.

| Universal type | Use case |
|---------------|----------|
| `integer<8>` | 8-bit ADC/PCM |
| `integer<12>` | 12-bit instrumentation ADC |
| `integer<16>` | 16-bit audio ADC |
| `integer<24>` | 24-bit professional audio ADC |

**When to use integers:**
- Modeling ADC/DAC quantization effects directly
- Studying the minimum bit-width for a given SNR target
- Input/output stages where fractional scaling is handled elsewhere

## Recommended pipeline configurations

These configurations are grounded in historical DSP practice and
represent meaningful evaluation points for the
[precision sweep](/mp-dsp/mixed-precision/motivation/) tool:

| Configuration | CoeffScalar | StateScalar | SampleScalar |
|--------------|-------------|-------------|--------------|
| TMS320 classic | `fixpnt<16,15>` | `fixpnt<32,31>` | `fixpnt<16,15>` |
| DSP56000 style | `fixpnt<24,23>` | `fixpnt<48,32>` | `fixpnt<24,23>` |
| Radar 12-bit | `fixpnt<16,15>` | `fixpnt<32,31>` | `fixpnt<12,11>` |
| Posit pipeline | `posit<32,2>` | `posit<32,2>` | `posit<16,2>` |
| Posit narrow | `posit<16,2>` | `posit<24,2>` | `posit<8,2>` |
| Cross-system | `double` | `posit<32,2>` | `fixpnt<16,15>` |
| LNS experiment | `double` | `lns<16,10>` | `lns<8,5>` |

## The Q-format convention

The fractional fixed-point convention became universal in DSP because
multiplying two numbers in $[-1, +1)$ always produces a result in
$[-1, +1)$ — overflow is impossible from multiplication alone.

| Notation | Total bits | Fractional | Range |
|----------|-----------|-----------|-------|
| Q7 | 8 | 7 | [-1.0, +0.992] |
| Q11 | 12 | 11 | [-1.0, +0.9995] |
| Q15 | 16 | 15 | [-1.0, +0.99997] |
| Q23 | 24 | 23 | [-1.0, +0.9999999] |
| Q31 | 32 | 31 | [-1.0, +0.9999999995] |

The `fixpnt<N, N-1>` format in Universal corresponds to the standard
Q(N-1) convention.

## Selection rules of thumb

1. **CoeffScalar** should be at least as wide as SampleScalar,
   preferably 2-4 bits wider. Coefficient quantization produces
   deterministic errors (pole displacement, passband distortion)
   that are more objectionable than distributed roundoff noise.

2. **StateScalar** should be double the width of SampleScalar for
   multiply-accumulate operations. This is the universal practice
   from the TMS320's 32-bit accumulator to the DSP56000's 56-bit
   accumulator.

3. **SampleScalar** is the streaming precision — choose the narrowest
   type that meets your SNR requirement. The minimum practical word
   length for IIR filtering is approximately $\log_2 N_{\text{poles}} + 10$ bits.

4. **Posit es=2** for general-purpose pipelines; **es=0** when you
   need maximum precision near unity; **es=1** as a balanced
   alternative to fixed-point.

5. **LNS** only when multiplications dominate and additions are rare
   or can be batched.

## References

- Jackson, L.B. "Roundoff-noise analysis for fixed-point digital filters," *IEEE Trans. Audio Electroacoustics*, 1970
- Kaiser, J.F. "Digital Filters," in *System Analysis by Digital Computer*, Wiley, 1966
- Gold, B. and Rader, C.M. *Digital Processing of Signals*, McGraw-Hill, 1969
- Oppenheim, A.V. and Weinstein, C.J. "Effects of finite register length," *Proc. IEEE*, 1972
- Gustafson, J.L. "Posit Arithmetic," Technical Report, 2017
