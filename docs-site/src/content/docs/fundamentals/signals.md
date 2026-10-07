---
title: Discrete-Time Signals
description: Foundations of discrete-time signal representation, common signal types, and the library's signal generators.
---

## From continuous to discrete time

Physical signals such as voltages, pressures, and acoustic waves are
**continuous-time**: they are defined for every real value of $t$.
Digital systems cannot store or process a continuum of values, so we
**sample** the continuous signal at uniform intervals of $T_s$ seconds,
producing a **discrete-time** sequence:

$$
x[n] = x_c(n T_s), \qquad n \in \mathbb{Z}
$$

where $x_c(t)$ is the original continuous waveform.

Two fundamental parameters describe the sampling grid:

| Symbol | Name | Definition |
|--------|------|------------|
| $T_s$ | Sampling period | Time between consecutive samples (seconds) |
| $f_s$ | Sample rate | Number of samples per second: $f_s = 1 / T_s$ (Hz) |

A sequence $x[n]$ carries no notion of "seconds" on its own -- the
sample rate provides the link back to physical time via $t = n / f_s$.

## Common signal types

### Unit impulse (Kronecker delta)

The simplest non-trivial signal is the **unit impulse**:

$$
\delta[n] =
\begin{cases}
1, & n = 0 \\
0, & n \neq 0
\end{cases}
$$

Every discrete signal can be written as a weighted sum of shifted impulses:

$$
x[n] = \sum_{k=-\infty}^{\infty} x[k] \, \delta[n - k]
$$

This decomposition is the foundation of linear time-invariant (LTI) system
theory -- the output for any input follows from knowing the system's
**impulse response** $h[n]$.

### Unit step

$$
u[n] =
\begin{cases}
1, & n \geq 0 \\
0, & n < 0
\end{cases}
$$

The step is the running sum of the impulse: $u[n] = \sum_{k=-\infty}^{n} \delta[k]$.
It is used to model sudden onsets, DC biases, and as a building block for
piecewise signals.

### Complex exponential and sinusoid

The **complex exponential** $x[n] = A e^{j\omega_0 n}$ is the eigenfunction
of every LTI system. Its real and imaginary parts give cosine and sine:

$$
x[n] = A \cos(\omega_0 n + \phi)
$$

where $\omega_0 = 2\pi f / f_s$ is the **digital frequency** in
radians per sample. Because $e^{j\omega n}$ is periodic in $\omega$
with period $2\pi$, discrete-time sinusoids with frequencies differing
by integer multiples of $f_s$ are indistinguishable -- a key
observation that leads to the Nyquist sampling theorem.

### Decaying exponential

$$
x[n] = A \, r^n \, u[n], \qquad 0 < r < 1
$$

Decaying exponentials model damped oscillations, capacitor discharge, and
the natural modes of discrete systems. The parameter $r$ controls the
decay rate: the signal loses a factor $1/e$ every $-1/\ln r$ samples.

## Energy and power

For finite-length or decaying signals, **energy** is the appropriate measure:

$$
E_x = \sum_{n=-\infty}^{\infty} |x[n]|^2
$$

Periodic or random signals have infinite energy but finite **power**:

$$
P_x = \lim_{N \to \infty} \frac{1}{2N+1} \sum_{n=-N}^{N} |x[n]|^2
$$

For a sinusoid $x[n] = A\cos(\omega_0 n)$, the average power is $P_x = A^2/2$.
These definitions extend directly to the frequency domain through
**Parseval's theorem**: the total energy (or power) equals the integral
of the energy spectral density (or power spectral density).

## Library signal containers and generators

### The Signal\<T\> container

The library wraps sample data with metadata in `Signal<T>`:

```cpp
#include <sw/dsp/signals/signal.hpp>

// Create a 1-second buffer at 48 kHz
sw::dsp::Signal<double> buf(48000, 48000.0);
buf[0] = 1.0;  // write the first sample

double fs = buf.sample_rate();   // 48000.0
double dur = buf.duration();     // 1.0 s
std::size_t N = buf.size();      // 48000
```

`Signal<T>` is templated on any type satisfying the `DspField` concept,
so you can use `float`, `double`, or any Universal number type such as
`posit<16,2>` with no code changes.

### Generator functions

The header `<sw/dsp/signals/generators.hpp>` provides free functions
that produce standard test signals as `mtl::vec::dense_vector<T>`:

```cpp
#include <sw/dsp/signals/generators.hpp>
using namespace sw::dsp;

// 1 kHz sine, 1024 samples at 44.1 kHz, unit amplitude
auto sig = sine<double>(1024, 1000.0, 44100.0, 1.0);

// Unit impulse at sample 0
auto imp = impulse<double>(256);

// Unit step starting at sample 10
auto stp = step<double>(256, 10);

// Linear chirp sweeping 100 Hz to 4 kHz
auto swp = chirp<double>(8192, 100.0, 4000.0, 44100.0);

// White noise (deterministic with seed)
auto nz = white_noise<double>(1024, 1.0, 42);
```

Every generator is templated on `DspField T`, making it easy to compare
precision effects:

```cpp
#include <sw/universal/number/posit/posit.hpp>
using Posit16 = sw::universal::posit<16, 2>;

// Same sine generator, posit precision
auto sig_posit = sine<Posit16>(1024, Posit16{1000.0},
                               Posit16{44100.0}, Posit16{1.0});
```

### Available generators

| Function | Signal | Key parameters |
|----------|--------|---------------|
| `sine` | $A\sin(2\pi f n / f_s + \phi)$ | frequency, sample rate, amplitude, phase |
| `cosine` | $A\cos(2\pi f n / f_s + \phi)$ | same as sine |
| `triangle` | Symmetric triangle wave | frequency, sample rate, amplitude |
| `square` | Square wave | frequency, sample rate, amplitude, duty cycle |
| `sawtooth` | Linear ramp per period | frequency, sample rate, amplitude |
| `impulse` | $A\,\delta[n - d]$ | length, delay, amplitude |
| `step` | $A\,u[n - d]$ | length, delay, amplitude |
| `ramp` | $\text{slope} \cdot n$ | length, slope |
| `white_noise` | Uniform in $[-A, A]$ | length, amplitude, seed |
| `gaussian_noise` | $\mathcal{N}(0, A)$ | length, amplitude, seed |
| `pink_noise` | $1/f$ spectrum (Voss-McCartney) | length, amplitude, seed |
| `chirp` | Linear frequency sweep | length, start/end freq, sample rate |
| `multitone` | Sum of sinusoids | length, frequency list, sample rate |

All generators return `mtl::vec::dense_vector<T>`, which can be wrapped
in a `Signal<T>` when sample-rate metadata is needed.

## Next steps

With a firm grasp of discrete-time signals, the next fundamental topic is
[Sampling and Aliasing](/mp-dsp/fundamentals/sampling/) -- how the choice of
$f_s$ determines what information is preserved and what is lost.
