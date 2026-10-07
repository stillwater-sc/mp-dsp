---
title: Pipeline Probes Overview
description: Attach probes to any point in a pipeline to inspect samples flowing through — captured to CSV/JSON for external analysis and visualization
---

The `probe/` module (Epic [#160](https://github.com/stillwater-sc/mp-dsp/issues/160),
sub-issue [#155](https://github.com/stillwater-sc/mp-dsp/issues/155))
adds pipeline sample-tap primitives so users can inspect intermediate
signal points in the pipelines they assemble from the library.

Where the [Digital Oscilloscope Demo](../instrument/scope-demo/)
characterizes **external** waveforms (ADC stream + trigger + capture),
a probe looks **into** an existing pipeline — validating stage-by-stage
behavior, diagnosing precision issues, and teaching the precision /
bandwidth / rate tradeoffs the library exists to expose.

## What a probe is

Three cooperating primitives, all under `sw::dsp::probe`:

| Primitive | Role |
|---|---|
| `SignalProbe<T>` | Fixed-capacity ring buffer. `push(x)` writes a sample; `samples()` returns the captured stream oldest-first. |
| `NoOpProbe<T>` | API-compatible drop-in whose `push()` is a no-op — used for compile-time probe disable in production builds. |
| `ProbedStage<S>` | Wraps a pipeline stage exposing `process()` + `sample_scalar` and pushes each output into a `SignalProbe`. |

```cpp
#include <sw/dsp/probe/signal_probe.hpp>
using namespace sw::dsp;

// Wrap an existing stage in a probe (any stage with sample_scalar
// + process() works: FIR filters, biquad cascades, DDC, decimators, ...).
auto probed_mixer = probe::make_probe(my_ddc, "after_mixer",
                                       /*capacity=*/4096, /*fs=*/50e6);

// Feed samples through the pipeline as normal - the probe captures
// each output.
for (double x : adc_input) probed_mixer.process(x);

// Dump the captured stream to CSV + sidecar JSON for external tools.
probed_mixer.probe().dump_csv("after_mixer.csv");
```

## Compile-time disable

`SignalProbe<T>` and `NoOpProbe<T>` share the same interface. Templating
the pipeline on `Probe = SignalProbe<T>` or `Probe = NoOpProbe<T>`
selects at build time between "collect samples" and "do nothing" —
no preprocessor flags, no runtime cost when probes are compiled out.

```cpp
// Debug build:
using DebugProbe = probe::SignalProbe<double>;
auto p_dbg = probe::ProbedStage<Stage, DebugProbe>(...);

// Production build - same code path, zero cost:
using ReleaseProbe = probe::NoOpProbe<double>;
auto p_rel = probe::ProbedStage<Stage, ReleaseProbe>(...);
```

## Interchange format

The C++ side of the toolchain produces structured data; all rendering
lives in the separate [mp-dsp-python](https://github.com/stillwater-sc/mp-dsp-python)
repository. `dump_csv(path)` writes:

**Main CSV** (`path`):

```
sample_index,sample_value
0,0.5
1,0.4938
2,0.4753
...
```

**Sidecar JSON** (`path.json`):

```json
{
  "label": "after_mixer",
  "sample_rate_hz": 50000000,
  "capacity": 4096,
  "captured": 4096
}
```

The mp-dsp-python renderer keys off the sidecar to label plots and
convert sample indices to time.

## Ring-buffer semantics

`SignalProbe` behaves like a standard fixed-size ring:

- **Under-fill** (before capacity is reached): `samples()` returns
  the pushed samples in the order they arrived.
- **At-fill** (`is_full()` becomes true): the newest sample replaces
  the oldest; `samples()` still returns them oldest-first, so a
  fresh-vs-old ordering is always chronological.

Callers who need to reset a probe (e.g., between independent test
segments) can call `clear()`.

## Worked example: probes inside a DDC chain

```cpp
#include <sw/dsp/acquisition/ddc.hpp>
#include <sw/dsp/probe/signal_probe.hpp>

// A DDC pipeline with three tap points: input, after DDC, and after
// polyphase decimation. Each stage is wrapped independently.
using namespace sw::dsp;

DDC<double, double, double> ddc(if_hz, fs, decimator);
probe::SignalProbe<double> probe_input("adc_input", 8192, fs);
probe::SignalProbe<std::complex<double>> probe_ddc("post_ddc", 8192, fs/2);
probe::SignalProbe<std::complex<double>> probe_out("post_poly", 8192, fs/16);

for (double x : adc_samples) {
    probe_input.push(x);
    auto [ready, z] = ddc.process(x);
    if (ready) probe_ddc.push(z);
    // ... push further stages into probe_out ...
}

probe_input.dump_csv("adc_input.csv");
probe_ddc.dump_csv("post_ddc.csv");
probe_out.dump_csv("post_poly.csv");
```

Three CSV+JSON pairs land ready for mp-dsp-python's `plot_probe.py`
renderer.

## Where to go next

- [Domain Views](./views/) — convert a captured probe stream into
  time / magnitude / phase / I-Q views for analysis.
- [Transfer Function Monitor](../transfer-function/overview/) —
  the complementary tool for characterizing LTI blocks (Bode sweeps
  + analytical pole/zero extraction).
- [mp-dsp-python](https://github.com/stillwater-sc/mp-dsp-python) —
  Python peer repo that renders probe CSVs and pole/zero JSON.
