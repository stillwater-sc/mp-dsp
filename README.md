# Mixed-Precision Digital Signal Processing

[![CMake](https://github.com/stillwater-sc/mp-dsp/actions/workflows/cmake.yml/badge.svg)](https://github.com/stillwater-sc/mp-dsp/actions/workflows/cmake.yml)

A modern C++20 header-only library for Digital Signal Processing with
arithmetic type parameterization as a first-class design feature.

## The Problem

High-performance and energy-efficient digital signal processing demands
careful attention to numerical precision. In embedded systems, edge AI,
software-defined radio, and real-time audio, every bit of precision
carries a cost in silicon area, memory bandwidth, power consumption, and
latency. Yet traditional DSP libraries hardcode `double` throughout
their implementations, making it impossible to explore how reduced or
alternative precision affects algorithm quality, throughput, and energy.

Consider the classical IIR filter design pipeline faithfully implemented
by Vinnie Falco's [DSPFilters](https://github.com/vinniefalco/DSPFilters)

1. **Analog prototype design** -- place poles and zeros in the s-plane
   using Butterworth, Chebyshev, Elliptic, or Bessel polynomial roots
2. **Frequency transformation** -- shift the prototype to the desired
   band using Constantinides transforms
3. **Bilinear transform** -- map s-plane to z-plane with frequency prewarping
4. **Cascade realization** -- factor into second-order sections (biquads)
5. **Sample processing** -- apply the cascade to streaming data

Each stage has fundamentally different numerical requirements. Pole
placement and coefficient calculation need high precision to preserve
filter characteristics. State accumulators need sufficient dynamic range
to avoid overflow and enough precision to suppress quantization noise.
Input/output sample streams may tolerate substantially lower precision
for throughput or power savings.

Traditional DSP libraries like Vinnie Falco's DSPFilters -- a beautifully crafted,
well-engineered C++ IIR filter library -- use `double` for every one of
these stages. The coefficients are `double`. The state variables are
`double`. The sample buffers are `double`. This uniformity simplifies
implementation but leaves significant performance and efficiency gains
on the table, and entirely forecloses research into mixed-precision
algorithm optimization.

## The Solution

`sw::dsp` treats arithmetic type parameterization as a first-order
concern. Every algorithm is templated on up to three independent scalar
types:

```cpp
template <DspField CoeffScalar  = double,    // filter coefficients
          DspField StateScalar  = CoeffScalar, // accumulator state
          DspScalar SampleScalar = StateScalar> // input/output samples
```

This three-scalar parameterization enables configurations like:

- Design coefficients in `double`, accumulate in `posit<32,2>`,
  stream samples in `posit<16,1>`
- Design in `long double`, accumulate in `double`, stream in `float`
- Use `cfloat<8,4>` samples for neural network feature extraction
  with `double` coefficient design
- Explore `fixpnt<16,12>` state variables for FPGA implementation
  while keeping `double` design precision

The library provides the quantization analysis tools needed to evaluate
these configurations: SQNR measurement, coefficient sensitivity
analysis, stability margin computation, and noise shaping -- everything
required to drive mixed-precision optimization across the DSP domain.

## Quick Start

```cpp
// Include everything: #include <sw/dsp/dsp.hpp>
// Or include individual modules for faster compilation:
#include <sw/dsp/filter/iir/butterworth.hpp>
#include <sw/dsp/filter/filter.hpp>

using namespace sw::dsp;

// Classical usage with double (just like any other DSP library)
SimpleFilter<iir::ButterworthLowPass<4>> filter;
filter.setup(4, 44100.0, 1000.0);

float sample = filter.process(input_sample);
```

```cpp
// Mixed-precision: design in double, accumulate in posit<32,2>,
// stream posit<16,1> samples
#include <sw/universal/number/posit/posit.hpp>

using namespace sw::universal;
using namespace sw::dsp;

SimpleFilter<iir::ButterworthLowPass<4, double, posit<32,2>, posit<16,1>>> filter;
filter.setup(4, 44100.0, 1000.0);

posit<16,1> sample = filter.process(posit<16,1>(input));
```

## Library Scope

The library covers the full DSP domain, organized into focused modules:

| Module | Description |
|--------|-------------|
| **signals** | Signal representation over dense vectors. Generators: sine, chirp, white noise, impulse, step. |
| **conditioning** | Envelope followers, dynamic range compression, AGC, polyphase interpolation/decimation, sample rate conversion. |
| **windows** | Window functions: Hamming, Hanning, Blackman, Kaiser, rectangular, flat-top. |
| **quantization** | ADC/DAC modeling, dithering (TPDF, RPDF), noise shaping, SQNR analysis. The toolbox for evaluating mixed-precision trade-offs. |
| **spectral** | DFT, FFT (Cooley-Tukey), Z-transform and Laplace transform evaluation, power spectral density, STFT/spectrogram. |
| **filtering** | IIR filter design: Butterworth, Chebyshev I/II, Elliptic, Bessel, Legendre, RBJ cookbook. Full pipeline: analog prototype, bilinear/Constantinides transforms, cascade of biquads, Direct Form I/II/Transposed realizations. |
|  | FIR filter design (window method, Parks-McClellan), direct-form convolution, polyphase decomposition, overlap-add/save. |
|  | Second-order section engine: coefficients, state forms (CRTP, no virtual dispatch), cascade, smooth parameter interpolation. |
| **estimation** | Kalman filters (linear, extended, unscented), LMS and RLS adaptive filters. |
| **image** | 2D convolution, separable filters, morphological operations, edge detection (Sobel, Prewitt, Canny). |
| **analysis** | Coefficient sensitivity, condition number estimation, stability margin checking. Numerical quality tools for mixed-precision research. |
| **io** | WAV, CSV, and raw binary file I/O for signals. |
| **viz** | Console text visualization: magnitude/phase plots, pole-zero diagrams. |

## Design Principles

### Arithmetic Type as First-Class Parameter

Every processing algorithm accepts scalar type template parameters.
The library never assumes `double` internally. Mathematical constants
are provided as `constexpr` templates (`pi_v<T>`, `two_pi_v<T>`).
Denormal prevention is traits-aware (no-op for posits and fixed-point,
which have no denormals). Type conversions at precision boundaries
are explicit.

### No Virtual Dispatch on Hot Paths

The original DSPFilters library routes all sample processing through
a deep virtual hierarchy (`Filter` -> `FilterDesignBase` ->
`FilterDesign`). `sw::dsp` uses CRTP and C++20 concepts for fully
static dispatch. All biquad state processing is inlineable. Runtime
polymorphism is available as an explicit opt-in wrapper, not the
default.

### References, Not Pointers

All aggregate data structures are passed by reference. The original
DSPFilters pattern of `LayoutBase` holding a raw `PoleZeroPair*`
with aliased storage is replaced by `PoleZeroLayout<T, MaxPoles>`
using `std::array`. No raw `new`/`delete` anywhere.

### MTL5 Containers

Signal buffers use `mtl::vec::dense_vector<T>`. Image data uses
`mtl::mat::dense2D<T>`. Kalman filter state matrices use
`mtl::mat::dense2D<T>`. Non-owning views use `std::span<T>`.
Fixed-size compile-time storage uses `std::array<T, N>`.

### Header-Only

The entire library is delivered as `.hpp` headers under
`include/sw/dsp/`. No compilation step, no link dependencies
beyond the C++20 standard library, Universal, and MTL5 (all
themselves header-only).

## Dependencies

| Library | Purpose | Repository |
|---------|---------|------------|
| [Universal](https://github.com/stillwater-sc/universal) | Number type arithmetic: posit, cfloat, fixpnt, integer, rational, and more | `stillwater-sc/universal` |
| [MTL5](https://github.com/stillwater-sc/mtl5) | Dense/sparse vectors and matrices, linear algebra operations | `stillwater-sc/mtl5` |

Both are header-only C++20 libraries. Dependencies are fetched
automatically via CMake `FetchContent` if not found on the system.
`sw::dsp` can also be used with native IEEE 754 types (`float`,
`double`, `long double`) alone.

## Building

### Prerequisites

- CMake 4.0+
- C++20 compiler: GCC 12+, Clang 15+, MSVC 2022 (17.4+), or Apple Clang 15+

### Native Build

```bash
git clone https://github.com/stillwater-sc/mp-dsp.git
cd mp-dsp

# CMake will fetch Universal and MTL5 automatically via FetchContent
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If Universal or MTL5 are installed system-wide (`find_package` succeeds),
those installations are used instead of FetchContent.

### RISC-V Cross-Compilation

```bash
cmake -B build-rv64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-gcc.cmake
cmake --build build-rv64
# Tests run via QEMU user-mode (set up automatically by the toolchain file)
ctest --test-dir build-rv64 --output-on-failure
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DSP_BUILD_APPLICATIONS` | `ON` | Build demonstration programs in `applications/` |
| `DSP_BUILD_TESTS` | `ON` | Build and register unit tests |

### Using sw::dsp in Your Project

```cmake
find_package(dsp CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE sw::dsp)
```

Or as a subdirectory:

```cmake
add_subdirectory(path/to/dsp)
target_link_libraries(your_target PRIVATE sw::dsp)
```

## Platform Support

| Platform | Compiler | Architecture | Tests |
|----------|----------|-------------|-------|
| Linux | GCC 12+ | x86-64 | native |
| Linux | Clang 15+ | x86-64 | native |
| Linux | GCC 12+ | RISC-V 64 | QEMU user-mode |
| macOS | Apple Clang 15+ | ARM64 | native |
| Windows | MSVC 2022+ | x86-64 | native |

## Project Structure

```
include/sw/dsp/
  concepts/        C++20 concepts bridging to MTL5 (DspScalar, DspField)
  types/           ComplexPair<T>, PoleZeroPair<T>, BiquadCoefficients<T>,
                   TransferFunction<T>, FilterKind, FilterSpec
  math/            Constants, denormal prevention, quadratic solver,
                   polynomial evaluation, elliptic integrals, root finder
  signals/         Signal representation and generators
  conditioning/    Envelope, compression, AGC, resampling
  windows/         Window functions
  quantization/    ADC/DAC models, dithering, noise shaping, SQNR
  spectral/        DFT, FFT, Z-transform, Laplace, PSD, spectrogram
  filter/
    biquad/        Biquad coefficients, Direct Form state, cascade
    layout/        Pole-zero layouts and analog prototypes
    transform/     Bilinear and Constantinides transforms
    iir/           IIR filter families (Butterworth through RBJ)
    fir/           FIR filter, design, polyphase, overlap methods
  estimation/      Kalman filters, LMS/RLS adaptive filters
  image/           2D convolution, morphology, edge detection
  analysis/        Sensitivity, condition number, stability
applications/      Demonstration programs
tests/             Unit tests
```

## Provenance

The IIR filter design pipeline in this library is informed by Vinnie
Falco's [DSPFilters](https://github.com/vinniefalco/DSPFilters), a
well-regarded C++ implementation of classical IIR filter design. The
algorithms -- Butterworth pole placement, Chebyshev ripple
specification, Jacobi elliptic integrals for Elliptic filters,
Laguerre root finding for Bessel/Legendre, Constantinides frequency
transformations, and the bilinear transform -- are standard DSP
textbook material drawn from the same primary sources:

- Orfanidis, S.J. *"High-Order Digital Parametric Equalizer Design,"*
  JAES vol 53, 2005.
- Constantinides, A.G. *"Spectral Transformations for Digital Filters,"*
  Proc. IEEE vol 117, 1970.
- Bristow-Johnson, R. *"Audio EQ Cookbook,"* musicdsp.org.
- Oppenheim, A.V. & Schafer, R.W. *Discrete-Time Signal Processing.*
- Gustafson, J.L. *"Posit Arithmetic,"* 2017.

`sw::dsp` is a clean-room reimplementation in modern C++20 with
generalized arithmetic type support, not a fork of DSPFilters.

## License

MIT License. Copyright (c) 2024-2026 Stillwater Supercomputing, Inc.
