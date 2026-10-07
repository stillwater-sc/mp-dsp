---
title: Sampling and Aliasing
description: The Nyquist sampling theorem, aliasing, normalized frequency, and their relationship to mixed-precision DSP.
---

## The sampling theorem

The Nyquist-Shannon sampling theorem is the cornerstone of digital signal
processing. It states that a band-limited continuous signal $x_c(t)$ with
no frequency content above $f_{\max}$ can be perfectly reconstructed from
its samples $x[n] = x_c(n T_s)$ **if and only if** the sample rate
satisfies:

$$
f_s > 2\,f_{\max}
$$

The critical boundary $f_N = f_s / 2$ is called the **Nyquist frequency**.
Any continuous-time frequency component below $f_N$ maps to a unique
discrete-time frequency; components at or above $f_N$ cannot be
distinguished from lower-frequency components -- they **alias**.

## Aliasing

When the sampling theorem is violated, high-frequency content "folds"
into the baseband and becomes indistinguishable from legitimate
low-frequency components. Mathematically, a continuous-time sinusoid at
frequency $f_0$ produces the same samples as sinusoids at frequencies:

$$
f_0 + k\,f_s, \qquad k \in \mathbb{Z}
$$

For example, sampling a 900 Hz tone at $f_s = 1000$ Hz produces exactly
the same sequence as sampling a 100 Hz tone. The 900 Hz component has
aliased to 100 Hz:

$$
f_{\text{alias}} = |f_0 - f_s| = |900 - 1000| = 100 \text{ Hz}
$$

Aliasing is **irreversible** -- once the samples are captured, there is no
way to separate the alias from a genuine signal at that frequency. This
makes the pre-sampling analog signal chain critical.

### Visualizing the folding

Consider a signal with energy spread from 0 to 800 Hz, sampled at
$f_s = 1000$ Hz ($f_N = 500$ Hz). The frequency content above 500 Hz
folds back:

| Original frequency | Aliased frequency |
|---|---|
| 0 -- 500 Hz | 0 -- 500 Hz (unchanged) |
| 500 -- 600 Hz | 500 -- 400 Hz (folded) |
| 600 -- 800 Hz | 400 -- 200 Hz (folded) |

The folded components add to whatever is already present in the
baseband, corrupting the signal.

## Normalized frequency

Working in Hz requires carrying $f_s$ through every equation. It is often
more convenient to use **normalized frequency**:

$$
\hat{f} = \frac{f}{f_s}
$$

The useful range is $\hat{f} \in [0,\; 0.5]$, where $\hat{f} = 0.5$
corresponds to the Nyquist frequency. Equivalently, the **digital
frequency** in radians per sample is:

$$
\omega = 2\pi\hat{f} = \frac{2\pi f}{f_s}
$$

with the unique range $\omega \in [0, \pi]$. Normalized frequency is
dimensionless and independent of the actual sample rate, which makes
filter specifications portable across systems running at different rates.

## Anti-aliasing filters

In practice, real-world signals are never perfectly band-limited. An
**anti-aliasing filter** is an analog low-pass filter placed before the
ADC to attenuate all frequency content above $f_N$:

$$
|H_{\text{aa}}(f)| \approx
\begin{cases}
1, & f < f_p \\
0, & f > f_s / 2
\end{cases}
$$

where $f_p < f_N$ is the passband edge. The transition band between $f_p$
and $f_N$ means a practical anti-aliasing filter cannot have a brick-wall
response. Higher-order analog filters provide steeper rolloff but add
phase distortion and cost. Many systems use **oversampling** -- sampling
at a rate much higher than $2 f_{\max}$ -- to widen the transition band
and relax the analog filter requirements.

### Oversampling

Sampling at $M \cdot f_s$ (where $M$ is the oversampling ratio) pushes
the Nyquist frequency to $M \cdot f_s / 2$, giving the anti-aliasing
filter a much wider transition band. After digital filtering and
decimation back to the target rate, the effective resolution improves:

$$
\text{SNR gain} \approx 10 \log_{10}(M) \text{ dB}
$$

With first-order noise shaping (see [Quantization](/mp-dsp/fundamentals/quantization/)),
the gain increases to approximately $30\log_{10}(M)$ dB per octave of
oversampling -- the principle behind sigma-delta ADCs.

## The bilinear transform and frequency warping

Digital filter design often starts with a well-known analog prototype
$H_a(s)$ and maps it to the $z$-domain. The **bilinear transform** is
the most common method:

$$
s = \frac{2}{T_s} \cdot \frac{1 - z^{-1}}{1 + z^{-1}}
$$

This maps the entire $j\Omega$ axis to one trip around the unit circle,
which prevents aliasing in the frequency response. However, the mapping
is nonlinear: the analog frequency $\Omega$ and digital frequency
$\omega$ are related by the **warping** equation:

$$
\Omega = \frac{2}{T_s} \tan\!\left(\frac{\omega}{2}\right)
$$

At low frequencies ($\omega \ll \pi$), $\Omega \approx \omega / T_s$
and the mapping is nearly linear. Near Nyquist ($\omega \to \pi$), the
tangent compresses a wide analog bandwidth into a narrow digital band.

To design a digital filter with a specified cutoff $\omega_c$, the analog
prototype must be designed at the **pre-warped** frequency:

$$
\Omega_c = \frac{2}{T_s} \tan\!\left(\frac{\omega_c}{2}\right)
$$

The library's IIR design functions (Butterworth, Chebyshev, Elliptic)
handle this pre-warping automatically. Understanding the mechanism is
important when interpreting filter responses at high normalized
frequencies where warping effects become significant.

## Connection to mixed-precision

Normalized frequency values lie in $[0, 0.5]$ -- a compact range that
is well suited to number types with limited dynamic range. Consider the
implications for arithmetic precision:

**Filter coefficients.** The bilinear transform produces coefficients
from rational functions of $\tan(\omega_c / 2)$. Near $\omega_c = 0$,
the tangent is small and the coefficients are well-conditioned. Near
Nyquist, the tangent diverges and the coefficients become sensitive to
rounding. A narrow type like `posit<8,0>` may suffice for a
low-frequency filter but fail for a filter near Nyquist.

**Frequency resolution.** In spectral analysis, the frequency bins are
spaced at $\Delta f = f_s / N$. Representing bin centers in a narrow
type is feasible because they are small rational multiples of $f_s$.

**Phase accumulators.** A numerically controlled oscillator (NCO)
accumulates phase as $\phi[n+1] = \phi[n] + \omega_0$. The accumulator
wraps modulo $2\pi$, so only fractional precision matters -- an ideal
use case for fixed-point or low-precision posit types.

The library's three-scalar model (`CoeffScalar`, `StateScalar`,
`SampleScalar`) lets you assign different types to each role, matching
precision to the sensitivity of each arithmetic path.

## Next steps

Sampling converts a continuous signal to a discrete sequence;
[Quantization](/mp-dsp/fundamentals/quantization/) converts continuous
amplitudes to discrete levels -- the second essential step in any
digital system.
