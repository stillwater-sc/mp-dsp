---
title: Historical Fixed-Point FFT
description: Scaling strategies, twiddle factor quantization, and word-length trade-offs from the origins of the FFT to the TMS320 era
---

The Cooley-Tukey FFT (1965) was born in 36-bit floating-point on an
IBM 7094. Within three years, real-time radar and sonar forced
fixed-point implementations on 12–18 bit minicomputers. The scaling
strategies developed then remain directly relevant to mixed-precision
DSP research today.

## The bit-growth problem

Each radix-2 butterfly can double the signal magnitude. Over
$\log_2 N$ stages, the output can grow by a factor of $N$. A
1024-point FFT needs 10 extra bits — catastrophic for a 16-bit
machine.

Three scaling strategies emerged:

## Divide-by-2 at each stage

Right-shift all values after every butterfly stage. Simple — the
shift costs zero logic in hardware. Became the TMS320 standard.

**Trade-off:** Loses $\log_2 N$ bits of precision unconditionally,
but distributes quantization noise optimally across stages.

**Welch's 1969 result:** Roundoff noise grows as $\log_2 N$, not $N$:

$$\sigma^2 \approx \frac{2}{3} \cdot 2^{-2B} \cdot \log_2 N$$

This single result made large fixed-point FFTs practical. A 1024-point
16-bit FFT achieves ~80 dB SNR.

## Block floating-point

All values share a common exponent (Oppenheim & Weinstein, 1972).
Scale only when overflow actually threatens — adaptive dynamic range
at fixed-point cost.

1. Scan for maximum magnitude before each stage
2. Right-shift and increment exponent only if overflow threatens
3. Left-shift and decrement exponent when headroom allows

Achieves near-floating-point SNR. Preferred for radar and sonar where
dynamic range mattered. The "loud neighbor" problem — one large value
forces the entire block to scale — is the main limitation.

## Twiddle factor quantization

Sin/cos lookup tables produce **deterministic** spectral leakage (not
noise). The error bound from Oppenheim & Weinstein:

$$|X̂(k) - X(k)| \leq \frac{\pi}{2} \cdot \log_2 N \cdot 2^{-B_w} \cdot \sum |x(n)|$$

**Rule of thumb:** Twiddle factors need at least as many bits as the
data path, preferably 2–4 more. Using fewer bits for twiddle factors
than for data is a false economy.

| Application | Data bits | Twiddle bits |
|------------|----------|-------------|
| Radar (1970s) | 12 | 12–16 |
| TMS320C10 | 16 | 16 (Q15 ROM) |
| High-purity spectral | 16 | 20–24 |
| DSP56000 | 24 | 24 |

## Double-length accumulators

Nearly universal: multiply in double width (16×16→32), round once for
storage. Reduces butterfly rounding error without solving the overflow
problem. The TMS320 had a 32-bit accumulator; the DSP56000 had 56 bits
(24×24→48 + 8 guard bits).

## Minimum practical word lengths

| FFT size | Min bits | SNR at 12-bit | SNR at 16-bit | SNR at 24-bit |
|----------|---------|--------------|--------------|--------------|
| 64 | ~12 | 58 dB | 82 dB | 130 dB |
| 256 | ~14 | 57 dB | 81 dB | 129 dB |
| 1024 | ~16 | 56 dB | 80 dB | 128 dB |
| 4096 | ~18 | 55 dB | 79 dB | 127 dB |

Minimum practical: $\log_2 N + 6$ bits. The gentle $\log_2 N$
degradation is why 16-bit fixed-point FFTs dominated for decades.

## Mapping to the three-scalar model

Every historical FFT technique maps to `sw::dsp`'s parameterization:

| Historical practice | Three-scalar equivalent |
|--------------------|------------------------|
| Twiddle table precision | `CoeffScalar` — at least as wide as `SampleScalar` |
| Double-width accumulator | `StateScalar` — wider than `SampleScalar` |
| Data word length | `SampleScalar` — the streaming precision |
| Block floating-point | Adaptive — analogous to posit regime bits |

## Configurations for the precision sweep

The [FFT trade-off analysis](/mp-dsp/mixed-precision/motivation/)
tool evaluates these historically-grounded configurations:

| Configuration | Twiddle | Accumulator | Data |
|--------------|---------|-------------|------|
| TMS320 classic | `fixpnt<16,15>` | `fixpnt<32,31>` | `fixpnt<16,15>` |
| DSP56000 style | `fixpnt<24,23>` | `fixpnt<48,32>` | `fixpnt<24,23>` |
| Radar 12-bit | `fixpnt<16,15>` | `fixpnt<32,31>` | `fixpnt<12,11>` |
| Posit pipeline | `posit<32,2>` | `posit<32,2>` | `posit<16,2>` |
| Posit narrow | `posit<16,2>` | `posit<24,2>` | `posit<8,2>` |
| Cross-system | `double` | `posit<32,2>` | `fixpnt<16,15>` |

## References

- Cooley & Tukey, *Math. Comp.*, 1965 — the algorithm
- Welch, *IEEE Trans. Audio Electroacoust.*, 1969 — fixed-point FFT error analysis
- Weinstein, *IEEE Trans. Audio Electroacoust.*, 1969 — floating-point FFT error analysis
- Oppenheim & Weinstein, *Proc. IEEE*, 1972 — comprehensive finite-wordlength effects
- Gold & Rader, *Digital Processing of Signals*, McGraw-Hill, 1969
