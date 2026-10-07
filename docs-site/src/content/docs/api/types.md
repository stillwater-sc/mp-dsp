---
title: Core Types
description: Key data types in the mixed-precision DSP library -- signals, biquads, cascades, filter specs, and type projection utilities
---

All types live in the `sw::dsp` namespace and are parameterized on scalar
types constrained by the library's C++20 concepts (see
[Concepts](/mp-dsp/api/concepts/)).

## Signal containers

### `Signal<T>`

A thin wrapper around `mtl::vec::dense_vector<T>` that carries sample-rate
metadata alongside the sample data:

```cpp
template <DspField T>
struct Signal {
    mtl::vec::dense_vector<T> samples;
    double sample_rate;

    std::size_t size() const;
    T& operator[](std::size_t i);
    const T& operator[](std::size_t i) const;
};
```

Use `Signal` whenever an algorithm needs to know the sample rate (spectral
analysis, filter design). For raw buffers where the rate is tracked
externally, `dense_vector<T>` is sufficient.

### `Image<T, Channels>`

A multi-channel planar image container for 2-D signal processing (sonar
arrays, image filtering):

```cpp
template <DspField T, std::size_t Channels = 1>
struct Image {
    std::array<mtl::vec::dense_vector<T>, Channels> planes;
    std::size_t width;
    std::size_t height;
};
```

Each channel is stored as a contiguous `dense_vector<T>` in row-major order.

## Biquad and cascade types

### `BiquadCoefficients<T>`

Holds the five coefficients of a single second-order section. The denominator
is normalized so that $a_0 = 1$:

```cpp
template <DspField T>
struct BiquadCoefficients {
    T b0, b1, b2;   // numerator (zeros)
    T a1, a2;        // denominator (poles), a0 = 1 implied
};
```

### `Cascade<T, MaxStages>`

A fixed-capacity array of biquad sections that together form a higher-order
IIR filter:

```cpp
template <DspField T, std::size_t MaxStages = 25>
class Cascade {
public:
    std::size_t stages() const;
    BiquadCoefficients<T>& operator[](std::size_t i);

    // Evaluate the composite frequency response at normalized freq w
    complex_for_t<T> response(double w) const;

    // Process a single sample through all stages in sequence
    T process(T sample);
    void reset();
};
```

`MaxStages` is a compile-time upper bound that avoids heap allocation. A
25-stage default supports filters up to order 50.

## Pole-zero representations

### `PoleZeroPair<T>`

A single conjugate pole-zero pair used during analog prototype design:

```cpp
template <DspField T>
struct PoleZeroPair {
    complex_for_t<T> pole;
    complex_for_t<T> zero;
};
```

### `PoleZeroLayout<T, MaxPoles>`

Collects all pole-zero pairs for a complete filter design. Analog prototypes
populate this layout; the bilinear transform then maps it to the digital
domain:

```cpp
template <DspField T, std::size_t MaxPoles = 50>
class PoleZeroLayout {
public:
    void add(const PoleZeroPair<T>& pz);
    std::size_t size() const;
    const PoleZeroPair<T>& operator[](std::size_t i) const;
};
```

### `TransferFunction<T>`

Numerator and denominator polynomials in powers of $z^{-1}$, useful for
analysis and export:

```cpp
template <DspField T>
struct TransferFunction {
    std::vector<T> numerator;    // b coefficients
    std::vector<T> denominator;  // a coefficients
};
```

## Filter design types

### `FilterSpec`

A plain specification struct passed to design routines:

```cpp
struct FilterSpec {
    unsigned order;
    double sample_rate;
    double freq1;          // cutoff or center frequency
    double freq2;          // bandwidth (band-pass/band-stop)
    double passband_ripple_db;   // Chebyshev / elliptic
    double stopband_atten_db;    // elliptic
};
```

### `SimpleFilter<Design>`

The main user-facing filter class. It combines a design object with
processing state and exposes the full filter lifecycle:

```cpp
template <typename Design>
class SimpleFilter {
public:
    void setup(unsigned order, double sample_rate, double cutoff);
    double process(double sample);
    void reset();
    const auto& cascade() const;
};
```

`Design` must satisfy `FilterDesign`. Typical usage:

```cpp
#include <sw/dsp/filter/iir/butterworth.hpp>

sw::dsp::SimpleFilter<sw::dsp::Butterworth> lp;
lp.setup(4, 48000.0, 1000.0);   // 4th-order, 48 kHz, 1 kHz cutoff

for (auto& x : samples) {
    x = lp.process(x);
}
```

## Type projection utilities

When moving values between arithmetic types of different precision, use the
explicit projection functions instead of raw casts:

```cpp
template <DspField Narrow, DspField Wide>
Narrow project_onto(Wide value);

template <DspField Wide, DspField Narrow>
Wide embed_into(Narrow value);
```

- **`project_onto<Narrow>(wide_value)`** -- narrows a value, applying
  whatever rounding or saturation the target type defines.
- **`embed_into<Wide>(narrow_value)`** -- widens a value into a
  higher-precision type without loss.

```cpp
using Posit16 = sw::universal::posit<16,2>;
double coeff = 0.123456789;

// Design in double, deploy in posit<16,2>
auto narrow = project_onto<Posit16>(coeff);

// Widen back for analysis
auto wide = embed_into<double>(narrow);
```

These functions make precision transitions explicit and auditable, which is
central to the library's mixed-precision philosophy.
