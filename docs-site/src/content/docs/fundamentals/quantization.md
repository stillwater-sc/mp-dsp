---
title: Quantization
description: Uniform quantization, SQNR analysis, dithering, noise shaping, and their connection to mixed-precision number types.
---

## What is quantization?

Sampling discretizes time; **quantization** discretizes amplitude. Every
digital representation maps a continuous value to one of a finite set of
levels. The spacing between adjacent levels is the **quantization step
size** $\Delta$, and the quantizer rounds each input to the nearest level:

$$
Q(x) = \Delta \left\lfloor \frac{x}{\Delta} + 0.5 \right\rfloor
$$

For a $B$-bit uniform quantizer covering the range $[-1, 1]$, there are
$2^B$ levels and the step size is:

$$
\Delta = \frac{2}{2^B} = 2^{1-B}
$$

## Quantization error

The difference between the original and quantized value is the
**quantization error**:

$$
e[n] = x[n] - Q(x[n])
$$

For a uniform quantizer, the error is bounded: $|e[n]| \leq \Delta/2$.
When the signal is much larger than $\Delta$ and varies rapidly, the
error behaves approximately like white noise, uniformly distributed on
$[-\Delta/2,\; \Delta/2]$. Its variance is:

$$
\sigma_e^2 = \frac{\Delta^2}{12}
$$

This is the **additive white noise model** of quantization -- the
quantized signal equals the original plus uncorrelated noise:

$$
Q(x[n]) \approx x[n] + e[n], \qquad e[n] \sim \mathcal{U}\!\left(-\frac{\Delta}{2},\; \frac{\Delta}{2}\right)
$$

The model breaks down for small signals (where the error becomes
correlated with the input) and for signals near the clipping boundaries.

## Signal-to-quantization-noise ratio (SQNR)

The ratio of signal power to quantization noise power, expressed in
decibels, is:

$$
\text{SQNR} = 10 \log_{10}\!\left(\frac{P_{\text{signal}}}{P_{\text{noise}}}\right)
$$

For a full-scale sinusoid ($A = 1$, power $= 1/2$) quantized with $B$ bits:

$$
\text{SQNR} = 10 \log_{10}\!\left(\frac{1/2}{\Delta^2/12}\right)
  = 10 \log_{10}\!\left(\frac{3}{2} \cdot 2^{2B}\right)
$$

which simplifies to the well-known rule of thumb:

$$
\boxed{\text{SQNR} \approx 6.02\,B + 1.76 \;\text{dB}}
$$

Each additional bit of precision buys approximately 6 dB of dynamic
range. A 16-bit system achieves roughly 98 dB; a 24-bit system reaches
about 146 dB.

## Dithering

At low signal levels, the quantization error becomes **correlated** with
the input, producing harmonic distortion rather than flat noise. This is
audible in audio systems and problematic in measurement systems.

**Dithering** adds a small amount of noise to the signal **before**
quantization, breaking the deterministic relationship between input and
error. The noise amplitude is typically on the order of $\pm\Delta/2$.

### RPDF vs. TPDF dither

| Dither type | Distribution | Effect |
|---|---|---|
| RPDF (rectangular) | Uniform on $[-\Delta/2, \Delta/2]$ | Eliminates first-order distortion; noise floor modulation remains |
| TPDF (triangular) | Sum of two uniform: $[-\Delta, \Delta]$ | Eliminates both distortion and noise modulation |

TPDF dither is the standard choice for high-quality audio. It raises
the noise floor by about 4.8 dB compared to undithered quantization,
but the noise is uncorrelated and perceptually benign.

### Library API: dither classes

```cpp
#include <sw/dsp/quantization/dither.hpp>
using namespace sw::dsp;

// Create a TPDF dither generator
// Amplitude is typically half the quantization step
TPDFDither<double> dither(0.5 / 32768.0, /*seed=*/42);

// Apply dither to a signal vector in-place
dither.apply(signal);
```

The `RPDFDither<T>` and `TPDFDither<T>` classes are templated on any
`DspField` type and support both sample-by-sample generation via
`operator()` and bulk application via `apply()`.

## Noise shaping

Dithering whitens the quantization noise but does not reduce its total
power. **Noise shaping** uses feedback to reshape the noise spectrum,
pushing energy out of the frequency band where it matters most.

A first-order noise shaper feeds the quantization error back into the
next sample:

$$
y[n] = Q\!\big(x[n] - e[n-1]\big)
$$

where $e[n] = x[n] - y[n]$ is the current quantization error. The
noise transfer function (NTF) is:

$$
\text{NTF}(z) = 1 - z^{-1}
$$

This is a first-order high-pass filter applied to the noise. The
magnitude response is:

$$
|\text{NTF}(e^{j\omega})| = 2\left|\sin\!\left(\frac{\omega}{2}\right)\right|
$$

At DC ($\omega = 0$), the noise is completely suppressed. Near Nyquist
($\omega = \pi$), it is amplified by a factor of 2 (6 dB). The total
noise power is unchanged, but it has been moved to high frequencies
where it can be filtered out or where human hearing is less sensitive.

Higher-order noise shapers provide steeper noise suppression in-band at
the cost of more noise amplification out-of-band and potential
instability.

### Library API: noise shaping

```cpp
#include <sw/dsp/quantization/noise_shaping.hpp>
using namespace sw::dsp;

// First-order noise shaper: double input, float output
FirstOrderNoiseShaper<double, float> shaper;

// Process a single sample
float output = shaper.process(input_sample);

// Process a full signal vector
auto shaped = shaper.process(reference_signal);
```

## Measuring SQNR with the library

The library provides direct SQNR measurement by comparing a reference
signal against its quantized version:

```cpp
#include <sw/dsp/quantization/sqnr.hpp>
#include <sw/dsp/signals/generators.hpp>
using namespace sw::dsp;

// Generate a reference sine at double precision
auto ref = sine<double>(4096, 1000.0, 48000.0, 0.9);

// Measure SQNR when quantizing to float
double db = measure_sqnr_db<double, float>(ref);
// Expected: ~150 dB (float has 24-bit mantissa)
```

The `sqnr_db` function accepts two vectors of potentially different
types, computing:

$$
\text{SQNR} = 10 \log_{10}\!\left(
  \frac{\displaystyle\sum_n |x_{\text{ref}}[n]|^2}
       {\displaystyle\sum_n |x_{\text{ref}}[n] - x_{\text{quant}}[n]|^2}
\right)
$$

Additional error metrics include `max_absolute_error` and
`max_relative_error` for worst-case analysis.

## ADC and DAC models

The library models the analog-to-digital and digital-to-analog
conversion process as type conversions between precision levels:

```cpp
#include <sw/dsp/quantization/adc.hpp>
#include <sw/dsp/quantization/dac.hpp>
using namespace sw::dsp;

// ADC: high-precision input to lower-precision output
ADC<double, float> adc;
auto quantized = adc.convert(reference_signal);

// DAC: reconstruct back to high precision for analysis
DAC<float, double> dac;
auto reconstructed = dac.convert(quantized);
```

The `ADC<InputT, OutputT>` performs `static_cast<OutputT>`, which
triggers the target type's native rounding behavior. For IEEE types
this is round-to-nearest-even; for Universal types such as posits, the
rounding follows the type's specification. The `DAC<InputT, OutputT>`
performs the reverse cast for reconstruction.

## Connection to mixed-precision

Traditional DSP treats quantization as a fixed property of the hardware:
a 16-bit ADC produces 16-bit samples, and all arithmetic uses the same
word size. The mixed-precision approach treats quantization as a
**design parameter** at each stage of the processing pipeline.

### Different types, different quantization profiles

The 6 dB-per-bit rule applies to **uniform** quantizers (fixed-point and
the mantissa of IEEE floats). Other number representations trade
uniformity for other properties:

| Type | Quantization behavior |
|---|---|
| Fixed-point ($B$ bits) | Uniform: $\text{SQNR} \approx 6.02B + 1.76$ dB |
| IEEE float ($p$-bit mantissa) | Relative error bounded by $2^{-p}$; SQNR depends on signal level |
| Posit ($n$ bits, $es$) | Tapered accuracy: highest near $\pm 1$, lower at extremes |

Posits concentrate their precision around 1.0, making them efficient
for normalized signals. A `posit<16,1>` may deliver SQNR comparable to
a 16-bit integer for signals in $[-1, 1]$ while using the same storage.

### The three-scalar model and quantization

The library's `CoeffScalar`, `StateScalar`, and `SampleScalar`
parameterization lets you make independent quantization choices:

- **CoeffScalar**: Filter coefficients are computed once and stored.
  They need enough precision to represent the pole/zero locations
  accurately but do not stream through the system.
- **StateScalar**: Accumulators perform many multiply-add operations.
  They need a wide dynamic range to avoid overflow and enough precision
  to avoid error accumulation.
- **SampleScalar**: Input/output samples stream at the full sample rate.
  Narrower types reduce memory bandwidth and storage, which is critical
  in real-time or embedded systems.

By measuring SQNR for each path independently, you can find the
narrowest types that meet your quality target, minimizing power and
silicon area without sacrificing signal integrity.

## Next steps

With an understanding of signals, sampling, and quantization, you are
ready to explore [filter design](/mp-dsp/filter/overview/) -- where the
three-scalar model meets classical IIR and FIR techniques.
