---
title: Synchronization — AGC, Timing and Carrier Recovery
description: The three feedback loops a coherent receiver needs, the PI loop filter they share, and why loop precision fails as a threshold rather than a slope
---

```cpp
#include <sw/dsp/sdr/agc.hpp>                // AutomaticGainControl
#include <sw/dsp/sdr/timing_recovery.hpp>    // TimingRecovery
#include <sw/dsp/sdr/carrier_recovery.hpp>   // CarrierRecovery
#include <sw/dsp/sdr/loop_filter.hpp>        // PiLoopFilter
```

Three feedback loops stand between a received waveform and a demappable
constellation. They remove, in order, the wrong **amplitude**, the wrong
**sampling instant**, and the wrong **phase**. Structurally they are
siblings — each measures an error, filters it, and corrects — but they
differ in how badly they behave when the arithmetic runs out, and that
difference is the most useful thing this page has to say.

## Why loops are different from datapath blocks

A datapath block degrades **gracefully**. Halve the mantissa and the
EVM roughly doubles; the relationship is smooth and a designer can
interpolate it.

A feedback loop degrades **as a threshold**. Its integrator takes a
per-step increment of $k_i \cdot e$, which for a narrow loop runs around
$10^{-4}$. If the integrator's state is held at a magnitude where the
scalar type's ULP exceeds that increment, the integrator cannot take a
small step *at all*: it either ignores the correction entirely or jumps
a whole ULP. Under noise the second case is a random walk that rails at
whatever clamp bounds it. There is no intermediate regime — the loop
tracks correctly right up until it does not track at all.

The fix is never "use a wider type". It is to change **what the state
holds**, so the working magnitude sits where resolution is finest:

| Loop | The trap | The fix |
|---|---|---|
| Timing | Absolute time accumulator. At 2 samples/symbol it reaches 16000 by symbol 8000, where `posit<16,2>`'s ULP is ~8; a step of ~2 cannot move it and the loop freezes with the eye shut. | Hold the next symbol's position **relative** to the newest sample. It stays in $[-1, 2]$. |
| Carrier | Unbounded phase sum. At 0.01 rad/sample it passes $10^4$ within a million samples, where a 16-bit float's ULP exceeds the increment and the oscillator stops turning. | **Wrap to $[-\pi, \pi)$ every sample.** |
| Both | Integrator holding an absolute samples-per-symbol (~2) or radians-per-sample. | Hold a **deviation from nominal**, always starting at zero. The caller adds the nominal. |

That last row is enforced structurally: `PiLoopFilter` documents and
implements the deviation convention, so both loops inherit it rather
than each re-deriving it. Measured, holding a deviation is the
difference between `posit<16,2>` failing at *every* loop bandwidth and
tracking correctly at all of them.

## The shared loop filter

Both synchronization loops need the same thing: turn a normalized noise
bandwidth and a damping factor into a pair of gains, then run a
proportional-integral filter over a detector's error. `PiLoopFilter`
holds that once.

```cpp
using namespace sw::dsp::sdr;

LoopFilterConfig<double> cfg;
cfg.bandwidth     = 0.01;    // Bn*T, normalized to the symbol rate
cfg.damping       = 0.707;
cfg.detector_gain = 1.0;     // Kp, the detector's slope at its zero crossing

PiLoopFilter<double> loop(cfg);
double correction = loop.advance(error);
loop.clamp_integral(max_deviation);
```

The gain mapping follows Rice, *Digital Communications: A Discrete-Time
Approach*, §7.2, for normalized noise bandwidth $B_nT$, damping $\zeta$
and detector slope $K_p$:

$$
\theta = \frac{B_nT}{\zeta + \frac{1}{4\zeta}},
\qquad
k_p = \frac{4\zeta\theta}{1 + 2\zeta\theta + \theta^2}\cdot\frac{1}{K_p},
\qquad
k_i = \frac{4\theta^2}{1 + 2\zeta\theta + \theta^2}\cdot\frac{1}{K_p}
$$

so a caller asks for a **bandwidth and a damping** rather than for two
opaque numbers. $K_p$ is the detector's slope, which depends on the
signal; it is exposed for calibration and defaults to 1, in which case
the achieved loop bandwidth is simply scaled by $1/K_p$.

`clamp_integral` takes the limit from the caller — the filter does not
invent a bound, because what the integrator *means* (samples per
symbol, radians per symbol) is the caller's business.

## Automatic gain control

```cpp
AgcConfig<double> cfg;
cfg.reference_level  = 1.0;
cfg.attack_time_s    = 0.001;   // fast: too loud, gain coming down
cfg.decay_time_s     = 0.100;   // slow: too quiet, gain coming up
cfg.averaging_time_s = 0.001;
cfg.sample_rate_hz   = 1.0;     // leave at 1 to specify times in samples
cfg.detector         = LevelDetector::rms;

AutomaticGainControl<double, std::complex<double>> agc(cfg);
auto y = agc.process(x);
```

### The loop is closed in the log domain

Two reasons, both about dynamic range:

- A multiplicative correction becomes **additive**, so convergence is
  the same rate whether the loop is climbing out of −60 dB or trimming
  3 dB. A linear-gain loop is sluggish when the gain is small and
  twitchy when it is large.
- The gain never has to be *represented* as a huge or tiny number.
  60 dB of range is 1000× linear but only **6.9 nepers**, which any
  scalar type holds comfortably.

State is stored in nepers because that is what the loop arithmetic
wants; the public interface is in dB because that is what engineers
want.

### Attack and decay

"Attack" is the fast direction, taken when the signal is **too loud**
and the gain must come down before something clips. "Decay" (release)
is the slow direction. The asymmetry is deliberate: a fast release
pumps the noise floor up between bursts. Measured by
`tests/test_sdr_agc.cpp` with the default configuration: **attack 59
samples, decay 1198** — the intended ~20:1 ratio.

### Detector choice

| `LevelDetector` | Behaviour | Measured gain ripple |
|---|---|---|
| `rms` *(default)* | Square root of a one-pole average of the squared output magnitude; regulates average power | **1.374 dB** |
| `magnitude` | Instantaneous output magnitude; sees the modulation as well as the envelope | **2.219 dB** |

For QAM the `rms` detector is what you want: it smooths across the
constellation instead of chasing each symbol's amplitude. `magnitude`
is fine for constant-envelope signals and reacts immediately.

### Two scalars, not three

`AutomaticGainControl<StateScalar, SampleScalar>` — `SampleScalar` may
be real or complex, because an SDR chain carries I/Q, while
`StateScalar` is always real, because a gain and a level are real
quantities whatever they multiply. There is no third parameter because
there are **no coefficients**: the attack, decay and averaging
constants are derived from time constants at construction.

### Precision behaviour

Measured residual against the gain state's precision, $\tau = 200$
samples, target gain 26.021 dB:

| Gain state | Residual | Achieved gain |
|---|---|---|
| `double` | 4.008e-14 | 26.021 dB |
| `posit<32,2>` | 1.337e-06 | 26.021 dB |
| `float` | 2.146e-05 | 26.020 dB |
| `cfloat<32,8>` | 2.146e-05 | 26.020 dB |
| `posit<16,2>` | **8.618e-02** | **25.250 dB** |

`posit<16,2>` stalls three quarters of a dB short — the loop's final
approach steps fall below its ULP at that gain magnitude, so it stops
before reaching the target. And the stall **scales with loop rate**,
which is the diagnostic that identifies it as a resolution problem
rather than a convergence one:

| Configuration | Residual |
|---|---|
| `posit<16,2>`, $\tau = 200$ | 8.618e-02 |
| `posit<16,2>`, $\tau = 10$ | 3.418e-03 |
| `double`, $\tau = 10$ | 1.887e-15 |

A faster loop takes larger steps, which clear the ULP, so the stall
shrinks by 25×. A loop that was merely slow to converge would show the
opposite dependence.

## Symbol timing recovery

```cpp
TimingRecoveryConfig<double> cfg;
cfg.samples_per_symbol = 2;
cfg.loop_bandwidth     = 0.01;   // Bn*T
cfg.damping            = 0.707;
cfg.detector           = TimingDetector::gardner;
cfg.max_deviation      = 0.05;   // omega clamp, fraction of nominal

TimingRecovery<double, std::complex<double>> tr(cfg);

for (auto x : stream) {
    auto [ready, symbol] = tr.process(x);
    if (ready) { /* one symbol at the recovered instant */ }
}
```

### Structure

```text
input at N samples/symbol
     │
     ▼
cubic Farrow interpolator  ◄── fractional position from the loop
     │
     ▼
timing error detector (Gardner or Mueller-Muller)
     │
     ▼
proportional-integral loop filter
     │
     ▼
symbol clock: next symbol is omega samples after this one
```

### Why a Farrow interpolator, not FractionalDelay

The library already ships a polyphase
[`FractionalDelay`](../../multirate/fractional-delay/), but it
quantizes the delay to $1/L$ — and that quantization would appear
directly as **timing jitter**, the very quantity this module exists to
measure. The four-point cubic Lagrange (Farrow) form takes a continuous
fractional position, is exact for cubic inputs, and costs four taps.
Using a quantized interpolator here would make the instrument report
its own resolution as the signal's impairment.

### Detectors

| `TimingDetector` | Error | Needs | Position in the chain |
|---|---|---|---|
| `gardner` | $\mathrm{Re}\{(y_k - y_{k-1})\,\overline{y_{k-1/2}}\}$ | ≥ 2 samples/symbol (the mid-symbol tap) | **Before** carrier recovery — it is blind to carrier phase |
| `mueller_muller` | $\mathrm{Re}\{\overline{a_{k-1}}y_k - \overline{a_k}y_{k-1}\}$ | 1 sample/symbol, but a roughly correct constellation | **After** carrier recovery — it is decision-directed |

Choosing a detector is choosing a receiver order. The constructor
enforces the Gardner constraint and throws if `samples_per_symbol < 2`
with that detector selected, naming Mueller-Muller as the alternative.

**The two detectors' S-curves have opposite slopes** — measured
open-loop, Gardner about $+0.7$ per symbol and Mueller-Muller about
$-1.8$. Sharing one loop filter without normalizing that would make
whichever detector was not tuned for drive itself *away* from lock, so
the Mueller-Muller output is negated internally. Both then hand the
loop the same convention: **positive means sampling late**.

### The lock metric is a mean, not an RMS

A locked Gardner loop still produces a sizeable instantaneous error —
about 0.37 RMS for unit-power BPSK — because the detector output
depends on the data pattern as well as on the timing. That self-noise
never goes away. What the loop actually drives to zero is the
**average**, so `lock_metric()` thresholds the smoothed *mean* error,
normalized by signal power. Thresholding the RMS would mean lock is
never declared no matter how well the loop tracks.

Since the detector output runs as $K_p \cdot \tau \cdot \text{power}$,
dividing by power leaves roughly $K_p \tau$ — so the metric reads
directly as **residual timing offset in symbols**, scaled by the
detector slope.

### Bandwidth trade-off

Measured by `tests/test_sdr_timing_recovery.cpp` — symbols to reach 90%
of the final $\omega$, against the jitter that costs:

| $B_nT$ | $\omega$ @ 90% | $\mu$ jitter |
|---|---|---|
| 0.002 | 4922 | 2.848e-03 |
| 0.005 | 421 | 5.365e-03 |
| 0.020 | 20 | 1.905e-02 |
| 0.050 | 7 | 5.070e-02 |

The classic loop trade: a 25× wider bandwidth acquires **700× faster**
and costs **18× the jitter**. There is no setting that is good at both,
which is why real receivers switch bandwidth after acquisition.

### Precision behaviour

$B_nT = 0.01$, 200 ppm clock error:

| Loop precision | Jitter | $\omega$ | Smallest symbol magnitude | Locked |
|---|---|---|---|---|
| `double` | 2.361e-01 | 1.999680 | 0.8537 | yes |
| `float` | 2.361e-01 | 1.999680 | 0.8537 | yes |
| `posit<32,2>` | 2.361e-01 | 1.999680 | 0.8537 | yes |
| `cfloat<32,8>` | 2.361e-01 | 1.999680 | 0.8537 | yes |
| `posit<16,2>` | 2.363e-01 | 1.999512 | 0.8538 | yes |

**Every type is identical to three digits**, `posit<16,2>` included.
That is the threshold behaviour from the top of this page, seen from
the good side: with the loop state held relative and the integrator
holding a deviation, the jitter is set by the loop bandwidth and the
detector's self-noise, not by the arithmetic. Precision buys nothing
here — until it buys everything, at the point where the increment falls
under the ULP.

## Carrier recovery

```cpp
CarrierRecoveryConfig<double> cfg;
cfg.loop_bandwidth = 0.01;
cfg.damping        = 0.707;
cfg.detector       = CarrierDetector::qpsk;
cfg.enable_afc     = true;
cfg.max_frequency  = 0.5;   // radians per symbol

CarrierRecovery<double, std::complex<double>> cr(cfg);
auto y = cr.process(symbol);   // de-rotated
```

### Detectors

| `CarrierDetector` | Error | Ambiguity | When |
|---|---|---|---|
| `bpsk` | $\mathrm{Im}(y)\,\mathrm{sgn}(\mathrm{Re}(y))$ | 180° | One decision axis; tolerates any phase within ±90° |
| `qpsk` | $\mathrm{Im}(y)\,\mathrm{sgn}(\mathrm{Re}(y)) - \mathrm{Re}(y)\,\mathrm{sgn}(\mathrm{Im}(y))$ | 90° | Two axes; the default, and what acquires |
| `decision_directed` | $\mathrm{Im}(y\,\overline{a})$ against the sliced point $a$ | constellation | Required denser than QPSK, where a coordinate's sign no longer determines the decision — but needs the loop close already |

**All three leave a phase ambiguity**, and no amount of loop design
removes it: a Costas loop cannot tell which constellation rotation it
locked to, because the signal is symmetric under exactly that rotation.
Resolving it needs a known preamble or differential encoding, both
outside this class.

The decision-directed error is normalized by $|a|^2$, so an outer
16-QAM point does not dominate an inner one and change the effective
loop gain with the data.

### AFC, and why the obvious frequency detector fails

A phase detector's pull-in range is roughly the loop bandwidth, so a
frequency offset much larger than that never acquires. AFC adds a
frequency-error term with a far wider capture range. Measured:

| Offset (rad/symbol) | PLL only | With AFC |
|---|---|---|
| 0.02 | lock | lock |
| 0.06 | lock | lock |
| 0.12 | lock | lock |
| 0.25 | **FAIL** | **lock** |

**The frequency detector must be modulation-stripped.** The obvious
form, $\mathrm{Im}(y_k \overline{y_{k-1}})$, measures the rotation
between consecutive symbols — but on QPSK data consecutive symbols
already differ by a random multiple of 90°, so that quantity is
dominated by the *data* and swamps the frequency it is meant to
measure. Measured, feeding it to the integrator turned an exact lock
into a **0.76 error vector at every offset tested**.

Stripping first fixes it. With $d = y\,\overline{a}$ for the sliced
decision $a$, only the phase error remains, so
$\mathrm{Im}(d_k \overline{d_{k-1}})$ is the rotation *between errors*
— frequency, with the modulation gone. The term also **fades out as the
loop locks**, since near lock the cross-symbol rotation is dominated by
noise and would only add jitter.

### Residual phase noise against bandwidth

| $B_nT$ | Residual EVM |
|---|---|
| 0.050 | 1.4754% |
| 0.020 | 1.4456% |
| 0.005 | 1.4313% |

A narrow loop tracks less noise, as expected — though over this range
the effect is small compared with the timing loop's, because the
carrier loop's error is not amplified by an interpolator.

### Precision behaviour

$B_nT = 0.02$, frequency offset 0.03 rad/symbol:

| Loop precision | Residual EVM | Tracked frequency | Locked |
|---|---|---|---|
| `double` | 4.2806% | 0.03049 | yes |
| `float` | 4.2806% | 0.03049 | yes |
| `posit<32,2>` | 4.2806% | 0.03049 | yes |
| `cfloat<32,8>` | 4.2806% | 0.03049 | yes |
| `posit<16,2>` | 4.2799% | 0.03049 | yes |

Same story as the timing loop, and for the same reason: the phase
accumulator is wrapped and the integrator holds a deviation, so no
state ever reaches a magnitude where a 16-bit type runs out of ULP. The
test also runs **200k symbols** confirming the phase stays wrapped —
that is the regression that would catch an unbounded accumulator
creeping back in.

## Putting the loops together

```cpp
// Typical order for a QPSK receiver with a Gardner detector.
// C is the complex sample type, e.g. std::complex<double>.
AutomaticGainControl<double, C> agc(agc_cfg);
FIRFilter<double, C, C>         matched(rrc_filter<double>(41, 4, 0.35));
TimingRecovery<double, C>       timing(timing_cfg);
CarrierRecovery<double, C>      carrier(carrier_cfg);
Constellation<double>           table(Modulation::qpsk);

for (C x : rx_stream) {
    const C levelled = agc.process(x);
    const C filtered = matched.process(levelled);
    auto [ready, sym] = timing.process(filtered);
    if (!ready) continue;
    const C derotated = carrier.process(sym);
    if (timing.is_locked() && carrier.is_locked())
        table.demap_hard_bits(derotated, bits);
}
```

Note the matched filter's scalars: with a complex `SampleScalar` the
`StateScalar` must be complex too, since the delay line holds samples.
`CoeffScalar` stays real — the RRC taps are real. The alternative
idiom, used in
[`tests/test_sdr_rrc.cpp`](https://github.com/stillwater-sc/mp-dsp/blob/main/tests/test_sdr_rrc.cpp),
is two real filters run on I and Q separately, which is what a hardware
implementation does.

Both loops expose `is_locked()`, and both declare lock only after
$4\times$ their averaging window has elapsed, so an early accidental
dip below threshold cannot be mistaken for convergence. Gate the
demapper on both.

Every loop also exposes `invariants_hold()` — a **predicate**, not an
`assert()`. This project's CI runs in Release where `NDEBUG` strips
assertions, so an invariant worth stating is worth asserting from a
test that always executes.

## See also

- [SDR Overview](./overview/) — where the loops sit in the link and
  why their order is fixed
- [Constellation and Metrics](./constellation/) — the demapper the
  loops feed, and the EVM measurement they are graded on
- [Fractional Delay](../../multirate/fractional-delay/) — the polyphase
  interpolator the timing loop deliberately does *not* use, and why
- [SDR Precision Analysis](./precision/) — the datapath counterpart to
  this page's loop measurements
- [NCO](../../acquisition/nco/) — the front-end oscillator whose
  residual offset carrier recovery removes
