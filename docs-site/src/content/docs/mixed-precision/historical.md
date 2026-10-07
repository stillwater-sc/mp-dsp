---
title: Historical DSP Arithmetic
description: From Bell Labs telephony to the TMS320 — how fixed-point conventions shaped digital signal processing
---

The mixed-precision approach in `sw::dsp` formalizes what DSP hardware
engineers practiced from the very beginning. Understanding the
historical context illuminates why the three-scalar model is a natural
fit and why fixed-point conventions like Q-format persist in modern
embedded DSP.

## The origins: 1950s Bell Labs

Digital signal processing began with telephony. Bell Labs' experimental
digital telephone systems in the late 1950s used 7-bit and later 12-bit
linear PCM for voice digitization. The key constraint was the channel:
T1 carrier lines transmitted 24 channels at 8 kHz, each with 8 bits per
sample (7 data + 1 signaling).

The filtering was done with dedicated digital hardware using different
word lengths at different stages:

- **12-bit ADC samples** -- limited by converter technology
- **16-bit coefficients** -- hand-computed from analog prototypes
- **32-bit accumulators** -- needed to prevent overflow in the
  multiply-accumulate (MAC) loop

This is exactly the three-scalar model: `SampleScalar` at 12 bits,
`CoeffScalar` at 16 bits, `StateScalar` at 32 bits.

## Lincoln Laboratory and the TX-2

MIT Lincoln Laboratory's TX-2 computer (1958) was among the first
machines used for real-time signal processing research. Its 36-bit word
length gave researchers unusual precision for the era, but the lessons
were the same: coefficient quantization degraded filter performance, and
accumulator overflow corrupted outputs.

The Lincoln Labs group (Gold and Rader, 1969) published the first
systematic analysis of coefficient quantization effects, establishing
that the minimum coefficient word length depends on the distance of
poles from the unit circle -- narrow-band filters need more bits.

## Q-format: the fixed-point convention

As DSP moved from laboratory computers to dedicated processors, the
Q-format convention emerged to manage fixed-point arithmetic without
hardware floating-point units.

### Notation

Q$m$.$n$ denotes a fixed-point number with $m$ integer bits and $n$
fractional bits, for a total of $m + n + 1$ bits (including the sign
bit). Common formats:

| Format | Total bits | Range | Resolution |
|--------|-----------|-------|------------|
| Q0.15 (Q15) | 16 | $[-1, 1 - 2^{-15}]$ | $3.05 \times 10^{-5}$ |
| Q0.31 (Q31) | 32 | $[-1, 1 - 2^{-31}]$ | $4.66 \times 10^{-10}$ |
| Q1.14 | 16 | $[-2, 2 - 2^{-14}]$ | $6.10 \times 10^{-5}$ |

### Why Q15?

Q15 was the natural choice for 16-bit processors: the entire range
$[-1, 1)$ maps to 16-bit signed integers. Multiplying two Q15 values
produces a Q30 result in a 32-bit accumulator, providing 15 bits of
guard against overflow in the MAC loop. This Q15-coefficient,
Q31-accumulator pattern became the de facto standard.

In `sw::dsp` terms: `CoeffScalar = fixpnt<16,15>`,
`StateScalar = fixpnt<32,31>`, `SampleScalar = fixpnt<16,15>`.

## The first DSP processors

### Intel 2920 (1979)

The first commercially available signal processor used 25-bit
fixed-point arithmetic with a 29-bit accumulator. It had no multiplier
-- filtering was done via shift-and-add. Its significance was
conceptual: it proved that a programmable DSP chip was viable.

### AMI S2811 (1979)

The first chip with a hardware multiplier for DSP. 12-bit data path
with a 26-bit accumulator -- another instance of the mixed-precision
pattern.

### TMS32010 (1982)

Texas Instruments' TMS32010 defined the DSP processor category:

- 16-bit data and coefficient words
- 32-bit accumulator with 32-bit product register
- Single-cycle 16x16 multiply-accumulate
- 200 ns instruction cycle (5 MIPS)

The architecture hardcoded the three-scalar pattern:

| Pipeline stage | Word length | Q-format role |
|---------------|-------------|---------------|
| Input samples | 16-bit | `SampleScalar` |
| Coefficients (ROM/RAM) | 16-bit | `CoeffScalar` |
| MAC accumulator | 32-bit | `StateScalar` |

### Motorola DSP56001 (1987)

The DSP56001 extended the pattern with 24-bit data words and a 56-bit
accumulator (two 24-bit guard extensions). This gave 24 bits of overflow
headroom in the accumulator -- enough for a 256-tap FIR filter with
worst-case input without any saturation logic.

## Lessons for modern mixed-precision DSP

The historical pattern is remarkably consistent across four decades
of DSP hardware:

1. **Coefficients and samples use the minimum viable word length** for
   the application's dynamic range and noise floor requirements.

2. **Accumulators use double (or more) the coefficient word length** to
   prevent overflow in the MAC inner loop.

3. **The final output is truncated or rounded** back to the sample word
   length, with optional noise shaping (dithering).

The `sw::dsp` library makes this pattern explicit and type-safe through
its three-scalar template parameterization, while extending the
vocabulary beyond fixed-point to include posit, logarithmic, and custom
floating-point representations.

## Further reading

- B. Gold and C.M. Rader, *Digital Processing of Signals*, McGraw-Hill,
  1969 -- the foundational text on coefficient quantization analysis.
- A.V. Oppenheim and R.W. Schafer, *Discrete-Time Signal Processing*,
  Prentice Hall -- chapters on finite word-length effects.
- See also: [Historical Fixed-Point FFT](/mp-dsp/mixed-precision/historical-fft/)
  for FFT-specific scaling strategies.
