# KFR vs. mixed-precision-dsp

A capability comparison between [KFR](https://github.com/kfrlib/kfr) (v7) and [stillwater-sc/mp-dsp](https://github.com/stillwater-sc/mp-dsp).

Two very different philosophies. KFR is a **performance-first SIMD DSP framework for production audio/signal work**. mixed-precision-dsp is a **research-grade numerical analysis toolkit for studying how arithmetic precision affects DSP algorithms**.

## Axis-by-axis comparison

| Dimension | **KFR** | **mixed-precision-dsp** |
|---|---|---|
| Primary goal | Fastest DSP on real hardware | Study precision/accuracy tradeoffs |
| C++ standard | C++20 | C++20 |
| Form | Headers + static libs (`kfr_dft`, `kfr_dsp`, `kfr_io`, `kfr_audio`) | Header-only |
| Numeric types | IEEE 754 (f32/f64), complex, fixed-sample integer PCM | IEEE + posit + cfloat + fixpnt + rational via Universal; 3 independent scalar types per algorithm (coeff / state / sample) |
| SIMD / dispatch | Hand-tuned SSE2–AVX512, NEON, RVV; runtime multiarch dispatch (`KFR_MULTI_*`) | None; relies on autovectorization |
| Platforms | x86, x86_64, ARM, AArch64, RISC-V, iOS, Android, WASM, Windows/macOS/Linux | Linux, macOS, Windows, RISC-V cross |
| Compiler requirements | Clang required for DFT & all non-x86 | GCC 12+, Clang 15+, MSVC 2022+ |
| License | Dual GPLv2+ / commercial | (see LICENSE in repo) |

## Feature matrix

| Feature | KFR | mp-dsp |
|---|---|---|
| FFT/DFT any size (incl. primes, mixed-radix) | ✅ optimized, on par with FFTW | ❌ radix-2 power-of-2 + O(n²) naive |
| Multidimensional DFT | ✅ | ❌ |
| Real DFT, DCT-II/III | ✅ | partial (STFT present) |
| FIR design (window + Parks-McClellan) | window method | ✅ both |
| IIR design (Butter / Cheby I-II / Bessel / Elliptic) | ✅ | ✅ (+ Legendre, RBJ cookbook) |
| Biquad cascade, DF I / II / Transposed | ✅ | ✅ (with smooth parameter interpolation) |
| Zero-phase IIR (`filtfilt`) | ✅ | ❌ |
| Sample-rate conversion | ✅ high-quality polyphase | ⚠️ integer up/down only; polyphase stubbed |
| Window functions | 14+ | 6 |
| Audio file IO | WAV, W64, RF64, AIFF, FLAC, CAF, ALAC, MP3, raw | WAV, CSV, raw |
| EBU R128 loudness | ✅ | ❌ |
| Tensors / `.npy` | ✅ | ❌ |
| C API | ✅ | ❌ |
| Image processing | ❌ | ✅ 2D conv, morphology, Sobel / Prewitt / Canny |
| Adaptive filters (LMS / RLS) | ❌ | ✅ |
| Kalman filter | ❌ | ✅ |
| Quantization tooling (SQNR, dither, noise shaping) | ❌ | ✅ |
| Coefficient sensitivity / stability margin analysis | ❌ | ✅ |
| Precision-sweep harness | ❌ | ✅ (`iir_precision_sweep.cpp`: 6 families × 6 types) |

## Where each one wins

### KFR is the right choice when you need

- **Throughput** — multiarch SIMD dispatch, DFT comparable to FFTW
- Arbitrary-size FFTs (primes, composites, multidimensional)
- Audio product work — codecs, resampling, loudness, `filtfilt`
- A shipping, C-API-exportable library on embedded ARM / RISC-V or WASM

### mixed-precision-dsp is the right choice when you need

- To answer *"what happens if I run this biquad cascade in `posit<16,1>` state with `fixpnt<1,15>` samples?"* — KFR has no answer to this
- Reference implementations to validate low-precision hardware against
- Pole-radius / sensitivity / condition-number analysis of filter designs
- Image + signal in one library
- Kalman / LMS / RLS — KFR does not cover estimation

## Overlap and complementarity

The overlap (FIR, IIR design, biquad, FFT of power-of-2, windows) is where mp-dsp gives you **numerical ground truth across arbitrary types** and KFR gives you **the fastest f32/f64 path**. They are not really competitors.

A plausible workflow:

1. **Prototype and study numerics in mp-dsp** — sweep arithmetic types, measure SQNR / pole displacement / stability margins, pick a configuration.
2. **Deploy in KFR** — port the chosen design to KFR (or a Stillwater hardware target) for the shipping f32/f64 SIMD path.

### Gaps mp-dsp would need to close to be a standalone production option

- Non-power-of-2 FFT (Bluestein / mixed-radix / prime-factor)
- High-quality polyphase sample-rate conversion
- Broader audio codec IO (FLAC / MP3 / AIFF / CAF at minimum)
- A SIMD path (even just autovectorization-friendly kernel layouts, or an explicit `vec<T, N>` abstraction)

### Gaps KFR would need to close to address mp-dsp's niche

- Decouple sample / state / coefficient types (currently a single `T` threads through each algorithm)
- Integration with non-IEEE numeric libraries (Universal, SoftFloat, etc.)
- Built-in quantization / dither / SQNR instrumentation
- Analysis tooling: coefficient sensitivity, stability margins, condition numbers

These gaps are deep enough that the two libraries are better seen as **complementary points in the DSP design space** than as substitutes.
