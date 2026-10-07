# sw::dsp Design Document

## Overview

`sw::dsp` is a modern C++20 header-only library for Digital Signal Processing
with first-class support for mixed-precision arithmetic. It leverages
Stillwater's Universal number arithmetic library for alternative number systems
(posits, fixed-point, custom floats) and MTL5 for linear algebra primitives.

The library covers the full DSP domain: signals, windows, quantization,
filtering (FIR and IIR), signal conditioning, spectral methods, estimation,
and image processing.

## Motivation

Traditional DSP libraries hardcode `double` (or at best `float`/`double`)
throughout their implementations. This prevents exploring how alternative
number representations affect algorithm accuracy, stability, and throughput.

Many DSP algorithms have distinct numerical requirements at different stages:
- **Design-time** computations (pole placement, coefficient calculation) need
  high precision to avoid design error
- **State accumulation** (filter memory, integrators) needs sufficient dynamic
  range to avoid overflow and precision to avoid noise buildup
- **Sample processing** (input/output data) may tolerate lower precision for
  throughput or power savings

By parameterizing every algorithm on three independent scalar types, `sw::dsp`
enables systematic exploration of mixed-precision configurations.

## Dependencies

| Library | Role | Include Path | CMake Target |
|---------|------|-------------|--------------|
| Universal | Number types (posit, cfloat, fixpnt, etc.), traits, complex | `sw/universal/` | `universal::universal` |
| MTL5 | Vectors, matrices, linear algebra operations | `mtl/` | `MTL5::mtl5` |

Both are header-only C++20 libraries. `sw::dsp` can also be used with
only native IEEE 754 types (`float`, `double`) without requiring Universal
or MTL5 for basic functionality.

## Three-Scalar Parameterization

The central design principle. Every processing algorithm template accepts:

```cpp
template <DspField CoeffScalar  = double,
          DspField StateScalar  = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
```

- **CoeffScalar**: filter coefficients, window values, twiddle factors.
  Designed once, stored permanently. Typically `double` or higher.
- **StateScalar**: accumulator variables, delay line contents, filter memory.
  Updated every sample. Must balance precision vs. throughput.
- **SampleScalar**: input/output sample data. Streamed at high rates.
  May be the lowest precision of the three.

Example: a Butterworth lowpass designed in `double`, accumulated in
`posit<32,2>`, streaming `posit<16,1>` samples:

```cpp
using namespace sw::dsp;
SimpleFilter<iir::ButterworthLowPass<4, double, posit<32,2>, posit<16,1>>> filter;
filter.setup(4, 44100.0, 1000.0);
```

## Concept Hierarchy

The library defines C++20 concepts that bridge to MTL5's concept system:

```
DspScalar          — requires +, -, *, unary-, and T{}
  └── DspField     — adds /
      └── DspOrderedField — adds < (total ordering)
```

Additionally:
- `ComplexType` — types with `.real()`, `.imag()`, `std::conj()`
- `SignalContainer` — types with `operator[]` and `.size()`
- `ContiguousSignalContainer` — adds `.data()` for span conversion

## Module Organization

### concepts/
Type concepts that constrain template parameters.

### types/
Fundamental data structures used throughout the library:

- **ComplexPair<T>**: a pair of complex numbers (conjugate pair or two reals).
  Maps to a single second-order section.
- **PoleZeroPair<T>**: poles and zeros for one biquad stage.
- **BiquadCoefficients<CoeffScalar>**: the six coefficients (b0,b1,b2,a1,a2)
  of a normalized second-order section.
- **TransferFunction<T>**: first-class rational polynomial H(z) = B(z)/A(z).
  Supports evaluation, pole/zero extraction, cascade, stability check.
- **FilterKind**: enumeration of filter response types.
- **FilterSpec**: parameter struct for filter design (sample rate, cutoff, etc.).

### math/
Mathematical utilities, all templated on scalar type:

- **constants.hpp**: `pi_v<T>`, `two_pi_v<T>`, `ln2_v<T>`, etc.
- **denormal.hpp**: `DenormalPrevention<T>` — traits-aware; no-op for posits.
- **quadratic.hpp**: complex root solver for quadratics.
- **polynomial.hpp**: Horner evaluation and polynomial multiplication.
- **elliptic_integrals.hpp**: Jacobi elliptic K, sn, cn, dn (for Elliptic filter design).
- **root_finder.hpp**: Laguerre's method (for Bessel/Legendre filter design).

### filter/biquad/
The core processing engine:

- **biquad.hpp**: `BiquadCoefficients<CS>` operations.
- **state.hpp**: `DirectFormI<SS>`, `DirectFormII<SS>`,
  `TransposedDirectFormII<SS>` — CRTP-based state forms. Each `process()`
  method takes `SampleScalar` input, computes in `StateScalar`, returns
  `SampleScalar`.
- **cascade.hpp**: `Cascade<CS, MaxStages>` — array of biquad stages with
  serial processing.
- **smooth.hpp**: `SmoothedCascade` — per-sample parameter interpolation for
  glitch-free real-time modulation.

### filter/layout/
Pole/zero representations:

- **layout.hpp**: `PoleZeroLayout<T, MaxPoles>` — fixed-capacity container
  of PoleZeroPair, replaces the original pointer-based LayoutBase.
- **analog_prototype.hpp**: concept and base for analog filter prototypes.

### filter/transform/
s-plane to z-plane transformations:

- **bilinear.hpp**: `LowPassTransform<T>`, `HighPassTransform<T>` —
  bilinear transform with frequency prewarping.
- **constantinides.hpp**: `BandPassTransform<T>`, `BandStopTransform<T>` —
  Constantinides frequency transformations.

### filter/iir/
IIR filter families, each providing LP/HP/BP/BS/shelf variants:

- **butterworth.hpp**: maximally flat magnitude response
- **chebyshev1.hpp**: equiripple passband (Type I)
- **chebyshev2.hpp**: equiripple stopband (Type II / Inverse)
- **elliptic.hpp**: equiripple passband and stopband (Cauer)
- **bessel.hpp**: maximally flat group delay (Thomson)
- **legendre.hpp**: optimum-L (steepest transition, monotonic passband)
- **rbj.hpp**: Robert Bristow-Johnson Audio EQ Cookbook biquads
- **pole_filter.hpp**: generic pipeline: prototype → transform → cascade

Design pipeline (preserved from DSPFilters):
```
AnalogPrototype.design(order, params)
    → PoleZeroLayout<T> (s-plane)
        → BilinearTransform(fc, digital_layout, analog_layout)
            → PoleZeroLayout<T> (z-plane)
                → Cascade.set_layout(digital_layout)
                    → BiquadCoefficients[] (ready to process)
```

### filter/fir/
FIR filter implementations:

- **fir_filter.hpp**: `FIRFilter<CS, SS, SaS>` with circular buffer delay line.
- **fir_design.hpp**: window method, Parks-McClellan (Remez exchange).
- **polyphase.hpp**: polyphase decomposition for efficient multirate processing.
- **overlap.hpp**: overlap-add and overlap-save convolution.

### signals/
Signal generation and representation:

- **signal.hpp**: `Signal<T>` wrapper over `mtl::vec::dense_vector<T>`.
- **generators.hpp**: `sine()`, `chirp()`, `white_noise()`, `impulse()`, `step()`.
- **sampling.hpp**: upsampling, downsampling, sample rate conversion.

### windows/
Window functions, each a callable returning `mtl::vec::dense_vector<T>`:

Hamming, Hanning, Blackman, Kaiser, Rectangular, Flat-top.

### quantization/
Precision analysis and conversion:

- **adc.hpp**: `ADC<InputT, OutputT>` models analog-to-digital conversion.
- **dac.hpp**: `DAC<InputT, OutputT>` models digital-to-analog conversion.
- **dither.hpp**: TPDF and RPDF dithering.
- **noise_shaping.hpp**: error-feedback noise shaping.
- **sqnr.hpp**: signal-to-quantization-noise ratio analysis.

### spectral/
Frequency domain methods:

- **dft.hpp**: `DFT<T>` naive O(N^2) implementation.
- **fft.hpp**: `FFT<T>` Cooley-Tukey radix-2 (mixed-radix planned).
- **ztransform.hpp**: evaluate transfer functions at arbitrary z-plane points.
- **laplace.hpp**: evaluate continuous-time transfer functions in the s-plane.
- **psd.hpp**: power spectral density estimation.
- **spectrogram.hpp**: short-time Fourier transform.

### estimation/
State estimation and adaptive filtering:

- **kalman.hpp**: `KalmanFilter<T, StateDim, MeasDim, CtrlDim>`.
- **ekf.hpp**: Extended Kalman filter (linearized).
- **ukf.hpp**: Unscented Kalman filter (sigma-point).
- **lms.hpp**: Least Mean Squares adaptive filter.
- **rls.hpp**: Recursive Least Squares adaptive filter.

### image/
2D extensions using `mtl::mat::dense2D<T>`:

- **convolve2d.hpp**: 2D convolution.
- **separable.hpp**: separable kernel decomposition (row + column FIR).
- **morphology.hpp**: dilation, erosion, opening, closing.
- **edge.hpp**: Sobel, Prewitt, Canny edge detectors.

### analysis/
Numerical quality tools for mixed-precision research:

- **stability.hpp**: extract poles from biquad sections (solve `z^2 + a1*z + a2 = 0`),
  check stability (`is_stable()`), compute `max_pole_radius()` and `stability_margin()`,
  collect `all_poles()` from a cascade.
- **sensitivity.hpp**: coefficient sensitivity via finite differences
  (`coefficient_sensitivity()`, `worst_case_sensitivity()`), plus
  `pole_displacement()` measuring how poles shift when coefficients are
  quantized from type T to type Q.
- **condition.hpp**: `biquad_condition_number()` and `cascade_condition_number()`
  — frequency response sensitivity to coefficient perturbations.
- **analysis.hpp**: umbrella.

### concepts/
- **scalar.hpp**: `DspScalar`, `DspField`, `DspOrderedField`, `ComplexType`,
  `ConvertibleToDouble`, `complex_for_t<T>`.
- **signal.hpp**: `SignalContainer`, `MutableSignalContainer`, `ContiguousSignalContainer`.
- **filter.hpp**: `FilterDesign`, `DesignableLowPass`, `DesignableBandPass`, `Processable`.

### Umbrella
- **dsp.hpp**: single header that includes the entire library.

## Hot Path Design: No Virtual Dispatch

The original DSPFilters library uses a deep virtual hierarchy
(`Filter → FilterDesignBase → FilterDesign → SmoothedFilterDesign`).
All `process()` calls go through vtable dispatch.

`sw::dsp` uses fully static dispatch on the processing path:
- State forms use CRTP (e.g., `DirectFormII<SS>`)
- `Cascade::process()` is a template function, not virtual
- `SimpleFilter` composes design + state via templates
- All hot-path code is inlineable by the compiler

For users who need runtime polymorphism (GUI filter selection, plugin hosts),
a separate `type_erased_filter` wrapper can be provided using `std::function`.
This is explicit opt-in, not the default.

## Naming Conventions

- Namespace: `sw::dsp` (sub-namespaces: `sw::dsp::iir`, `sw::dsp::fir`, etc.)
- Types: `PascalCase` (e.g., `BiquadCoefficients`, `ComplexPair`)
- Functions: `snake_case` (e.g., `solve_quadratic`, `evaluate_polynomial`)
- Template parameters: `PascalCase` (e.g., `CoeffScalar`, `StateScalar`)
- Constants: `snake_case` (e.g., `pi_v`, `two_pi_v`)
- Files: `snake_case.hpp`

## Build System

CMake 4.0+, header-only INTERFACE library. Supports:
- **Linux**: GCC 12+, Clang 15+
- **macOS**: AppleClang 15+, GCC 12+
- **Windows**: MSVC 2022+ (17.4+)
- **Cross-compilation**: RISC-V 64 via `cmake/toolchains/riscv64-gcc.cmake`

```bash
# Native build
cmake -B build -S .
cmake --build build
ctest --test-dir build

# RISC-V cross-compile
cmake -B build-rv64 -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-gcc.cmake
cmake --build build-rv64
```

## References

- Orfanidis, S.J. "High-Order Digital Parametric Equalizer Design," JAES vol 53, 2005.
- Constantinides, A.G. "Spectral Transformations for Digital Filters," Proc. IEEE vol 117, 1970.
- Bristow-Johnson, R. "Audio EQ Cookbook," musicdsp.org.
- Kuo, F.F. "Network Analysis and Synthesis."
- Oppenheim, A.V. & Schafer, R.W. "Discrete-Time Signal Processing."
- Gustafson, J.L. "Posit Arithmetic," 2017.
