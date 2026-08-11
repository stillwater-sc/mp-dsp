# Changelog

All notable changes to `mixed-precision-dsp` are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Releases before v0.8.0 were tagged without a changelog file; the entries below
those are summarized from their release commits and are intentionally terse.

## [Unreleased]

**Milestone v0.10 — Software Defined Radio — is complete.** Epic #85 and all
eleven of its sub-issues (#95–#105) are closed, along with #208 and #209. The
epic's stated premise did not survive its own capstone measurement: see the
`applications/sdr_demo` and `--study` entries below, and the correction
recorded on #85. Not yet released; `version.hpp` still reads 0.9.0.

### Added

- `applications/sdr_demo --study`: the 8-bit investigation (#209, follows
  from #104). Sweeps the whole 8-bit design space — `fixpnt<8,4..7>`,
  `posit<8,0..2>`, `cfloat<8,2..4>` — runs a three-scalar factorial over all
  eight narrow/double corners of `CoeffScalar`/`StateScalar`/`SampleScalar`,
  and takes loss-versus-Eb/N0 curves from 6 dB below the operating point to
  12 dB above. Writes `sdr_demo_study.csv`. The standard sweep is unchanged;
  `--study` runs the investigation instead.

  **The 8-bit ordering from #104 did not survive.** `posit<8,1>` (0.36 dB)
  and `cfloat<8,3>` (0.48 dB) both beat `fixpnt<8,5>` (0.81 dB), and neither
  was in the original sweep. The spread *within* each family — fixpnt
  3.35 dB, posit 1.10 dB, cfloat 0.80 dB — dwarfs the 0.46 dB gap between
  families that drove the original conclusion. At 8 bits the exponent size or
  binary point is the dominant variable and the family label is not.

  `posit<8,2>` and `cfloat<8,4>` agreed to three digits because both leave
  **3 mantissa bits at unity amplitude**, so they quantize a
  unit-average-power constellation identically — a coincidence of
  parameterization, not the shared bottleneck it looked like. The study
  reproduces the effect with a second pair: `cfloat<8,2>` and `fixpnt<8,5>`
  also land on exactly 0.81 dB.

  **Most of what those two lose is a systematic gain error, not precision.**
  Both show a fitted cloud gain of 0.935 — they round the constellation
  amplitudes down by the same relative amount. Fitting that out, as a
  receiver's AGC does for free, drops `posit<8,2>` from 1.27 dB to 0.58 dB
  and `cfloat<8,4>` from 1.27 dB to 0.59 dB, both inside the budget. The
  table now reports raw loss and AGC-corrected loss side by side.

  **`fixpnt<8,7>` is a metric trap.** It reports a *negative* implementation
  loss — better than the double chain, which no precision effect can produce.
  Its range is +/-0.992 against a waveform peaking near 0.97, so the datapath
  saturates: a compressive nonlinearity that clips the noise tail (lowering
  EVM) while pulling every symbol toward its decision boundary (raising BER).
  Such rows are flagged `LIMITING` and excluded from the ranking. A negative
  implementation loss is a signal that EVM has stopped being a valid proxy,
  which is why the table now carries BER and fitted gain beside it.

  The factorial puts **essentially the whole 8-bit loss in `StateScalar`** —
  1.28 dB of 1.28 dB for `posit<8,2>` — against 0.05 dB for coefficients and
  0.19 dB for the sample stream. That refutes the reading of
  `analysis/sdr_precision`'s attribution, which had named the constellation
  table the largest contributor; the two tools answer different questions,
  and the docs now say which one a design decision needs.

  The ranking is stable in Eb/N0: no pair swaps by more than 0.1 dB anywhere
  across the range measured. What #104 got wrong was which members it
  compared, not where it compared them.

- `ChainResult` gains `gain` and `residual_evm`, fitted with
  `sdr::iq_imbalance`, so a compressive datapath is visible as a gain below 1
  rather than hiding inside an EVM number. The standard sweep's figures are
  unchanged.

- Documentation: SDR mixed-precision guide (#105, closes epic #85). Six new
  docs-site pages under `sdr/` — overview, constellation/pulse-shaping/metrics,
  synchronization, OFDM, channelizer, and precision analysis — plus a new
  "SDR Modulation & Demodulation" sidebar section. The existing `acquisition/`
  section is relabelled "SDR Receiver Front-End" so the two halves of the SDR
  stack are distinguishable, and the two now cross-link in both directions.

  The precision page carries the **corrected epic premise**. #85 assumed that
  posit reaches a higher modulation order than cfloat or fixpnt at equal bit
  width; the #104 sweep measured the opposite at full scale, and the page
  states why — an amplitude-normalized link keeps the waveform inside one
  octave, and a uniform absolute step is optimal by construction against an
  absolute error metric. It documents the axis the families genuinely differ
  on (input backoff / dynamic range) and flags the unresolved 8-bit ordering
  as #209 rather than presenting it as settled.

  Every measured number is sourced from a test or demo run at this revision
  rather than from theory: RRC truncation-vs-quantization floors, AGC stall
  against gain-state precision, the timing loop's bandwidth/jitter trade,
  Costas AFC pull-in range, OFDM pilot spacing against multipath, PAPR against
  FFT size, and channelizer reconstruction against prototype length.

  The pages also record the module's central mixed-precision finding: datapath
  blocks separate the number systems across orders of magnitude, while the
  synchronization loops do not separate them at all — a loop is
  precision-insensitive until it crosses a ULP threshold and stops moving
  entirely. That is why the fix for a failing loop is changing what the state
  holds (relative position, wrapped phase, deviation from nominal), not
  widening the type.

- `applications/sdr_demo`: SDR link demonstrator (#104, capstone for #85).
  Sweeps a complete TX -> AWGN -> RX link (bits -> Gray map -> RRC
  interpolation -> channel -> matched filter -> symbol sampling -> demap)
  over 15 number-system configurations x 4 modulations (QPSK, 16-QAM,
  64-QAM, 256-QAM), across IEEE, cfloat, posit and fixpnt at 8, 12, 16 and
  32 bits. Writes `sdr_demo.csv` (the sweep), `sdr_demo_pareto.csv` (bits of
  arithmetic against delivered bits/symbol), `sdr_demo_constellations.csv`
  (clouds for plotting), `sdr_demo_blocks.csv` (per-block attribution) and
  `sdr_demo_backoff.csv` (dynamic range).

  Unlike `analysis/sdr_precision`, which projects coefficients into the
  narrow type and computes in `double`, the demo instantiates
  `PolyphaseInterpolator<Coeff,State,Sample>` and
  `FIRFilter<Coeff,State,Sample>` on the type under test: coefficients,
  accumulators and samples all run narrow. Both views are reported, labeled.

  The reported metric is **implementation loss** — `10*log10(EVM_narrow^2 /
  EVM_double^2)` at the Eb/N0 that puts the double chain at BER 1e-3, with
  1 dB the usability threshold. The reference is re-measured at the sweep's
  own symbol count and seed so the narrow chain sees the same noise
  realization and most sampling variance cancels in the ratio.

  **The epic's expectation that posit reaches a higher modulation order than
  its rivals at equal width is not what the measurement shows at full
  scale.** On 16-QAM at 8 bits, `fixpnt<8,5>` costs 0.81 dB (usable) against
  1.27 dB for both `posit<8,2>` and `cfloat<8,4>` (not usable), so fixed-
  point carries 16-QAM at 8 bits where the other two carry only QPSK. This
  is the expected result once stated plainly: the link is amplitude-
  normalized end to end, the waveform lives in one octave, and a uniform
  absolute step is exactly what EVM rewards.

  The demo therefore also measures the condition under which the tapered
  format does win — an input-backoff sweep that attenuates signal and noise
  together (Eb/N0 unchanged) and restores the level before measuring. There
  `fixpnt<8,5>` is best at full scale and gone by -12 dB, while `posit<8,2>`
  holds to -36 dB and `posit<16,2>` to -48 dB against 24 dB for
  `fixpnt<16,13>`. Dynamic range, not precision at full scale, is the axis
  that separates the families. The conclusion is derived from the sweep data
  at run time, so changing the parameters changes the finding rather than
  leaving a stale claim in the output.

- `analysis/sdr_precision`: end-to-end SDR precision analysis (#103, part of
  #85). `run_link()` measures EVM, MER and BER across bits -> constellation ->
  RRC shaping -> AWGN -> matched filter -> demap; `analyze_blocks()` attributes
  the result per block; `evm_budget()` and CSV writers support Pareto and
  visualization work. The CSV schema matches `precision_sweep.csv` at the
  identifier columns.

  Attribution narrows **one block at a time** with everything else at double,
  and subtracts the reference **in power**. Both matter: every configuration
  carries a common floor — here the RRC truncation ISI, 7.26e-03 at a
  10-symbol span — that is often larger than the arithmetic under test, so
  raw EVMs agree to four digits and say nothing.

  The measured contributions **do not add**, and which way they miss depends
  on precision: on 16-QAM the parts sum to 2.97e-04 against a whole-chain
  3.41e-04 for posit16 (more than the sum) and 1.37e-02 against 9.47e-03 for
  posit8 (less). A breakdown is an attribution, not a budget; `whole_chain`
  is therefore measured rather than summed.

  The all-double reference reproduces the closed-form 16-QAM BER curve within
  5% from 8 to 12 dB, which is what validates the noise scaling, pulse
  shaping, sampling instant and demapper together.

  The chain deliberately excludes the timing and carrier loops: those converge
  stochastically, and their residual would be indistinguishable from the
  arithmetic residual being measured.

- `sdr/ofdm`: OFDM modulator and demodulator (#101, part of #85).
  `OfdmLayout` allocates DC-null, guard, pilot and data subcarriers from one
  config shared by both halves so they cannot disagree; the modulator loads
  the grid, inverse-transforms and prepends a cyclic prefix; the demodulator
  strips the prefix, transforms, estimates the channel by least squares at
  the pilots with linear interpolation between them, and equalizes.
  `papr_db()` measures peak-to-average power ratio.

  A **flat channel equalizes to machine precision at every pilot spacing** —
  interpolating a constant is exact, which isolates the equalizer from the
  interpolator. Under frequency-selective multipath the residual is
  interpolation error and behaves like it: EVM falls with pilot density
  (3.6e-02 to 4.4e-03 at spacing 8 down to 2 on a 2-tap channel) and rises
  with delay spread at every density.

  Intercarrier interference against transform precision, 64-QAM on an ideal
  channel, where nothing but the transform contributes:

  | | EVM | |
  |---|---:|---:|
  | double | 3.4e-16 | -309 dB |
  | posit32 | 1.2e-08 | -158 dB |
  | float, cfloat32 | 1.6e-07 | -136 dB |
  | posit16 | 4.0e-04 | -68 dB |

  posit16 stays two orders inside 64-QAM's 0.309 decision distance. Measured
  PAPR grows with subcarrier count as expected: 5.3 dB at 16 subcarriers,
  7.7 dB at 64, 12.0 dB at 256.

- `sdr/channelizer`: 2x-oversampled analysis/synthesis filter bank with
  perfect reconstruction (#102, part of #85). `OversampledChannelizer` splits
  a wideband stream into M channels at a hop of M/2;
  `OversampledSynthesizer` recombines them. The existing maximally-decimated
  analysis bank is re-exported alongside them.

  Reconstruction is exact — 1e-16 relative error at M from 4 to 64 — and the
  cascade delay formula is exact too, confirmed against least-squares
  alignment.

  Two structural facts drove the design, both established by measurement:

  * **A maximally-decimated bank cannot reconstruct** with a plain
    prototype. Across all four FFT-direction and commutator-order
    conventions the best was a 17% relative residual: each channel is
    decimated by M, the channel responses overlap, and the aliasing does not
    cancel. Hence the 2x oversampling.
  * **The prototype spans exactly one transform.** A longer one must be
    folded into M bins first, and that fold aliases in time — measured
    residual 1.1e-16 at K=1, 4.5e-02 at K=2, 2.2e-01 at K=4. So K is not
    offered as a parameter. Sharper channel responses without reconstruction
    remain available through `multirate::Channelizer`, which takes
    `taps_per_phase`.

  Adjacent-channel rejection is 22 dB three channels out — modest, and the
  documented price of exact reconstruction from a single-transform window.

- `sdr/carrier_recovery`, `sdr/loop_filter`: Costas carrier recovery (#99,
  part of #85), completing phase 2 of the SDR epic. BPSK, QPSK and
  decision-directed detectors, AFC for wide-offset acquisition, and frequency
  and phase outputs for monitoring.

  The PI loop filter is now shared with timing recovery through
  `PiLoopFilter`, which also carries the deviation-from-nominal integrator
  convention in one place rather than two.

  The phase accumulator wraps to `[-pi, pi)` every symbol — the same lesson
  the timing loop's symbol clock carries, in another guise: an unbounded
  phase passes 1e4 within a million samples, where a narrow type's ULP
  exceeds the per-symbol increment and the oscillator stops turning. Verified
  bounded over a 200,000-symbol run.

  AFC needed a modulation-stripped frequency detector. The obvious
  `Im(y[k]*conj(y[k-1]))` measures rotation between consecutive symbols, but
  on QPSK those already differ by a random multiple of 90 degrees, so the
  data swamps the frequency — measured, it turned an exact lock into a 0.76
  error vector at every offset. Stripping with the sliced decision first
  leaves the rotation between *errors*, which is frequency. With it the loop
  acquires a 0.25 rad/symbol offset that the bare PLL cannot reach.

- `sdr/timing_recovery`: symbol timing recovery (#98, part of #85). Gardner
  (non-data-aided, >= 2 samples/symbol) and Mueller-Muller (decision-directed,
  1 sample/symbol) detectors, a proportional-integral loop filter
  parameterized by normalized noise bandwidth and damping, a cubic Farrow
  interpolator, and a lock detector.

  Interpolation uses a four-point cubic Lagrange rather than the library's
  polyphase `FractionalDelay`, which quantizes delay to 1/L — that
  quantization would appear directly as timing jitter, the quantity this
  module exists to measure.

  Two state-representation decisions are precision-critical rather than
  stylistic, and both were found by measurement:

  * The symbol clock holds the next symbol's position **relative** to the
    newest input sample, never an absolute time. An absolute accumulator
    grows without bound — 8000 symbols at 2 samples each reaches 16000, where
    `posit16`'s ULP is about 8, so a step of ~2 cannot advance it and the
    loop freezes with the eye shut. Kept relative it stays within `[-1, 2]`.
  * The integrator holds the **deviation** from nominal samples-per-symbol,
    not the absolute value, so its small corrections land near zero where
    resolution is finest.

  With both, `posit16` tracks indistinguishably from `double` (omega 1.999512
  against 1.999680, identical eye opening); without the first it failed at
  every loop bandwidth tested.

  Measured bandwidth trade, acquisition against jitter:

  | Bn*T | symbols to acquire | mu jitter |
  |---:|---:|---:|
  | 0.002 | 4922 | 2.85e-03 |
  | 0.005 |  421 | 5.37e-03 |
  | 0.020 |   20 | 1.91e-02 |
  | 0.050 |    7 | 5.07e-02 |

- `sdr/agc`: automatic gain control (#100, part of #85), the first stateful
  component in the SDR module.

  The loop is closed in the **log domain**, which is what buys the wide
  dynamic range: a multiplicative correction becomes additive, so convergence
  rate is the same whether the loop is climbing out of -60 dB or trimming
  3 dB, and 60 dB of range is 6.9 nepers rather than 1000x linear. State is
  kept in nepers because that is what the loop arithmetic wants; the public
  interface is dB because that is what engineers want.

  Attack is the fast direction, taken when the signal is too loud and the
  gain must come down; decay is the slow one. Both are time constants in
  seconds against a configured sample rate. `LevelDetector` selects
  instantaneous magnitude or a smoothed RMS — the latter regulates average
  power rather than chasing each QAM symbol, and measures 1.37 dB of gain
  ripple on 16-QAM against 2.22 dB for instantaneous.

  Two scalar parameters rather than this library's usual three: the gain and
  level are real quantities whatever they multiply, so `SampleScalar` may be
  complex while `StateScalar` stays real, and there are no coefficients to
  carry a precision of their own.

  Class invariants are exposed through a public `invariants_hold()` predicate
  and asserted from the tests, rather than by `assert()` in the header — CI
  runs Release, where `NDEBUG` strips assertions.

- `sdr/metrics`: EVM, MER, BER and constellation impairment measurement
  (#97, part of #85), completing phase 1 of the SDR epic.

  `evm()` reports RMS and peak error vector magnitude as fraction, percent and
  dB; `mer_db()` is the same measurement with the sign flipped, provided
  because both appear in specifications and offering only one invites a sign
  error at the call site. Everything normalizes by the **mean reference symbol
  power** — the 3GPP convention. Standards that normalize by peak
  constellation magnitude report smaller figures for the same signal.

  `theoretical_ber_awgn()` gives Gray-coded AWGN bit error probability against
  Eb/N0. BPSK and QPSK are exact and share a curve; the higher orders are the
  standard nearest-neighbour approximations, tight at moderate-to-high SNR.
  `esn0_db_from_ebn0_db()` and its inverse handle the conversion.

  `iq_imbalance()` fits the general real-linear model plus DC term by least
  squares and separates gain error, DC offset, common rotation and quadrature
  error, reporting the EVM that survives removing all of them. That residual
  is what distinguishes an arithmetic problem — which scatters symbols
  isotropically and survives the fit — from a structured analog one, which
  does not. A reference confined to one axis (BPSK) leaves I/Q gain
  unobservable and is rejected rather than silently fitted.

  Metrics accumulate in double whatever scalar type the symbols arrive in, so
  a posit16 link and a double reference land on the same axis, matching the
  convention in `analysis/`.

- `sdr/rrc`: root-raised-cosine and raised-cosine pulse-shaping filter design
  (#96, part of #85), plus `peak_isi()` for measuring residual intersymbol
  interference from a composite response.

  These are design functions returning taps, not stateful processors: the
  shaping itself reuses the multirate primitives already in the library —
  `PolyphaseInterpolator` with RRC taps is the transmit shaper,
  `PolyphaseDecimator` with the same taps is the receive matched filter. RRC
  is symmetric, so the "time-reversed" matched filter is the same tap set and
  no reversal helper is provided.

  Both removable singularities are handled by their limits: `t = 0`, and
  `|4*alpha*t/T| = 1`, which lands on an actual tap whenever
  `samples_per_symbol / (4*alpha)` is an integer — `alpha = 1` with 4 samples
  per symbol, for instance. Normalization defaults to unit energy, matching
  MATLAB `rcosdesign()`, which makes an RRC pair composite to a peak of
  exactly 1 at the symbol instant so the zero-ISI property reads directly off
  the composite samples. `num_taps` must be odd, so that a tap lands on `t = 0`
  — the instant the property is defined at.

- `sdr/constellation`: QAM/PSK constellation mapping and demapping, the first
  piece of the SDR modulation/demodulation epic (#95, part of #85). Supports
  BPSK, QPSK, 8-PSK, 16-QAM, 64-QAM and 256-QAM with Gray labelling and unit
  average power, offering hard-decision (minimum-distance) demapping and both
  exact and max-log soft-decision LLRs.

  `Constellation<T>` is an immutable table rather than a stateful processor —
  no delay line, no phase — so one instance is safe to share across streams
  and map/demap are pure functions of their arguments.

  The exact LLR uses the max-subtraction form of log-sum-exp so the
  exponentials stay bounded at any noise variance. It needs `exp`/`log` for
  `T`, but being a template member it is only instantiated when called, so a
  scalar type without transcendentals can still use everything else on the
  class — including max-log LLRs, which need only arithmetic and `min`.

  Conventions, fixed because a mismatch here silently ruins a receiver: bits
  are MSB-first one per `uint8_t`; a positive LLR means bit 0 is more likely;
  `noise_variance` is `E[|n|^2]` for the complex noise, not per dimension.

  New `sdr` CTest label. Tests pin the design's five class invariants
  explicitly — table size, unit average power, distinctness, Gray adjacency
  per family, and the scheme/bit-count agreement — since the class has no
  mutators and the invariants are properties of its construction.

### Fixed

- `multirate/fractional_delay`: the default `taps_per_phase = 12` was rejected
  by the class's own validator, so `FractionalDelay<double> fd(64)` — the
  documented defaults — always threw (#208). The default is now `11`. The
  parameter documentation named 12, 16 and 24 as recommended values; every
  one of them is even and therefore also threw, so the recommendations are
  now 11 and 15–25.

  **The constraint was kept, not relaxed**, because it is load-bearing. Odd
  `K` keeps the intrinsic group delay `(K-1)/2` an integer, which makes phase
  0 `sinc(k - center)` sampled on the integers — exactly one non-zero tap, an
  unfiltered passthrough. Measured, a request at the group-delay floor
  reproduces its input to 3e-17. With even `K` the floor is a half-integer,
  so *no* request lands on an unfiltered tap and every output, including a
  nominally integer delay, pays interpolation error and passband droop.

  Validation moved from the constructor body into `design_bank`, which is
  called from the initializer list. The `L == 0` check in the body could not
  stop `design_bank` or the ring-buffer sizing beside it from running on a
  bad value first, since the body runs after the whole initializer list. The
  error message now names the offending value.

  The class had **no unit test at all**, which is why the default could sit
  broken: the demo passes an explicit odd 15, so nothing ever constructed it
  the documented way. `tests/test_multirate_fractional_delay.cpp` covers the
  regression, the validation paths, the phase-0 passthrough property, delay
  accuracy across the phase grid, the 1/L quantization, the floor and
  `max_int_delay` boundaries, `reset()`, and a precision sweep. New
  `multirate` CTest label.

  Delay is measured as the DC group delay of the impulse response,
  `sum k*h[k] / sum h[k]`, which *is* `-dphi/domega` at DC rather than an
  estimate of it. Correlating a delayed tone — the obvious alternative — is
  ambiguous modulo the tone period, and at any frequency inside the passband
  that period exceeds the delays under test; broadband noise removes the
  ambiguity but leaves a peak too broad to refine (7e-02 samples of bias
  against 6e-04 for the moment).

- `acquisition/nco`, `acquisition/ddc`: absolute RF frequencies and sample
  rates overflowed narrow state types (#207). `NCO::set_frequency` took both
  arguments at `StateScalar` and divided only afterwards, so each was
  converted to the narrow type *before* the division that would have brought
  their ratio back into range. A 1.2 GHz carrier on a 5 GSPS front end — an
  ordinary direct-sampling configuration — produced a NaN phase accumulator
  and NaN samples thereafter, with nothing indicating why; `fixpnt<32,24>`
  tripped the existing positivity check instead, since 5e9 is not
  representable at all.

  Frequency and sample rate are now treated as configuration rather than
  datapath state: the ratio is formed in `double` and only the result is
  converted, which every state type represents comfortably since it always
  lies in [0, 0.5). `DDC` holds its rate and centre frequency the same way.
  The constructors and setters accept anything convertible to `double`, so
  existing callers passing `StateScalar` are unaffected.

  A post-condition rejects a phase increment that is not finite in the
  configured type, turning any residual case into a clear error rather than a
  silent NaN. All six types in the issue's table now yield 0.24.

- Tests: four test files hardcoded `/tmp/...` output paths, which do not exist
  on Windows, so `test_probe_signal_probe`, `test_probe_views`,
  `test_transfer_function_bode`, and `test_transfer_function_pole_zero` failed
  under MSVC while passing on every other platform. They now resolve scratch
  files through `std::filesystem::temp_directory_path()`. Test-only change; no
  library code is affected. The breakage dates from v0.8.0 and left CI red on
  the Windows job for the whole v0.9 cycle.

## [0.9.0] — 2026-08-05

A bug-fix release covering the filter-design path. Four defects closed —
#203, #204, #205, #206 — spanning the Parks-McClellan exchange, the
half-band designer, and the Constantinides band transformations.

One theme runs through all four: every one shipped because its tests asserted
STRUCTURE rather than RESPONSE. Tap counts, symmetry, a zero centre tap, poles
in the left half-plane, root counts — all held perfectly while the filters
themselves were wrong. Each fix here adds response-shape assertions (ripple,
stopband depth, band edges, notch placement) and each new test was verified to
fail against the pre-fix code.

The v0.9 milestone remains open: it tracks the SDR feature backlog, of which
only #204 is part. This release is the bug-fix work that accumulated
alongside it.

### Fixed

- `filter/fir/remez`: the Parks-McClellan designers returned filters that were
  not equiripple (#203). `remez()`, `design_fir_equiripple_lowpass/bandpass`,
  and `design_halfband()` all share the affected path. Four defects, each
  independently sufficient to break the result:
  - **`compute_delta()` returned the ripple with the wrong sign.** The delta
    formula must carry a leading minus to match the `D + (-1)^i * delta / W`
    convention `eval_approx()` interpolates. With the wrong sign the
    interpolant misses the last reference point, the error curve alternates
    only n-1 times instead of n, the extremal search can never assemble a full
    alternating set, and the exchange freezes on its first reference set — the
    "converges in two iterations and then sits still" symptom in the report.
  - **The tap extraction was not an orthogonal transform.** The inverse DCT
    sampled the half-open interval `[0, 0.5)` with uniform weights; DCT-I
    orthogonality requires the closed interval with half-weighted endpoints.
    Every recovered coefficient carried an O(1/M) error. This was the root
    cause identified in the report.
  - **The extremal search compared across transition bands.** The grid is a
    concatenation of per-band segments, so testing a band's last point against
    the next band's first point compares across a gap. Band edges — always
    extremal in a Parks-McClellan solution — could never enter the reference
    set, leaving the largest errors in the design uncontrolled. The search is
    now band-aware and treats every band edge as a candidate.
  - **The final extraction paired the reference set with a stale delta.** The
    exchange replaces its reference set after computing delta, so the tap
    extraction interpolated values the final set does not satisfy. On specs
    where the last exchange still moved points this produced filters whose
    measured stopband bore no relation to the reported ripple (45 dB claimed,
    9 dB delivered). Delta is now re-derived from the final set.

  Measured against `scipy.signal.remez` over 30 lowpass specifications
  (varying length, band edges, and 1:1 / 1:10 / 10:1 / 1:100 band weights),
  27 agree within 3% and the remaining 3 within 3.8%. For the report's
  reference case — 95 taps, bands {0, 0.20, 0.25, 0.5} — passband ripple goes
  from 2.40 dB to 0.0017 dB with unity DC gain, and stopband attenuation from
  49 dB to 80.2 dB. `design_halfband()` attenuation is now monotonic in both
  tap count and transition width; every cell where scipy converges now matches
  it.

- `transfer_function/pole_zero`: `lp_to_bp()` and `lp_to_bs()` produced
  constellations that were not bandpass and bandstop (#204).

  Both transforms mishandled the prototype's zeros at infinity. Under
  `s -> (s^2 + w0^2)/(BW*s)` each such zero contributes exactly **one** zero at
  the origin — its partner stays at infinity — but `lp_to_bp` padded the zero
  list to `2*order`, adding N spurious zeros at DC and dragging the response
  peak out to ~3.5x the band centre. Under `s -> BW*s/(s^2 + w0^2)` each maps
  onto the **pair** `+/- j*w0`, which is the notch; `lp_to_bs` generated none
  of them, so an all-pole prototype became a bandstop with no finite zeros
  whose response *peaked* at the band centre instead of nulling there. A
  4th-order Butterworth now transforms to 8 poles and 4 origin zeros for
  bandpass, and 8 poles and 8 zeros exactly on the jw axis at `+/- j*w0` for
  bandstop. Biproper prototypes (Chebyshev II, elliptic) have no zeros at
  infinity and correctly gain none at the origin, so their bandpass does not
  null at DC.

  A second defect, shared by `lp_to_hp()` and found while verifying the fix:
  the Constantinides substitutions are stated for a prototype normalized to
  `omega_c = 1`, and none of the three transforms divided the prototype roots
  by their own `omega_c` first. Every result was therefore scaled by the
  prototype cutoff. The example in the module documentation — a 1 kHz
  Butterworth through `lp_to_hp(p, 500.0)` — placed its -3 dB point at
  0.0796 Hz rather than 500 Hz, and `lp_to_bp(p, 800, 1200)` yielded a
  passband spanning 337..2849 Hz. All three now normalize first, so the target
  frequencies mean what they say: that highpass is -3 dB at 500.3 Hz and that
  bandpass at 800.1 and 1199.9 Hz, independent of the cutoff the prototype was
  built at.

  `PoleZeroPlot::cutoff_hz` is now both read and written by the transforms —
  it records the frequency the constellation is normalized to, so `lp_to_bp`
  and `lp_to_bs` set it to the band centre `sqrt(low*high)` rather than
  leaving a stale lowpass cutoff behind.

  The existing tests asserted root counts and stability only, which held while
  `lp_to_bp` emitted twice the zeros it should and `lp_to_bs` emitted none.
  They now measure the transformed response: peak location, -3 dB band edges,
  notch depth, passband flatness, and that every bandstop zero lands on the
  jw axis.

- `acquisition/halfband`: **`design_halfband()` now returns the equiripple
  design by default.** It takes a third parameter, `bool exact_dc_gain`,
  which defaults to `false`; passing `true` restores the previous behaviour of
  rescaling the odd-offset taps so the DC gain is exactly 1 (#206).

  This is a behaviour change for every existing caller. Filters gain a
  consistent 6.0 dB of stopband attenuation and give up an exact DC gain,
  which becomes `1 -/+ delta` instead.

  The two properties are mutually exclusive, and that is a property of the
  half-band structure rather than of the design method. Writing the zero-phase
  amplitude as `A(f) = 0.5 + sum_{k odd} 2*h[c+k]*cos(2*pi*f*k)` and using
  `cos(pi*k) = -1` for odd `k` gives `A(0) + A(0.5) = 1` identically. `A(0.5)`
  is a stopband extremum in the equiripple solution, so `|A(0.5)| = delta` and
  therefore `A(0) = 1 -/+ delta`. Forcing `A(0) = 1` forces `A(0.5) = 0`, which
  is reachable only by scaling the whole odd part by `1/(1 -/+ 2*delta)` — and
  that lifts every other stopband ripple from `delta` to about `2*delta`.

  | taps | tw | `exact_dc_gain=true` | default | recovered |
  |---:|---:|---:|---:|---:|
  | 31 | 0.10 | 51.3 dB | 57.4 dB | 6.03 |
  | 51 | 0.10 | 81.1 dB | 87.1 dB | 6.00 |
  | 67 | 0.10 | 104.4 dB | 110.4 dB | 6.01 |
  | 95 | 0.10 | 144.7 dB | 150.7 dB | 5.99 |

  The default is `false` because the function advertises an equiripple
  half-band and should return one. The DC error is bounded by `delta` — the
  same ripple the design already accepts in its passband — so it is negligible
  exactly when the filter is good, and only material for short filters where
  the ripple is large anyway. Pass `true` when unity DC gain through cascaded
  stages matters more than stopband depth.

  Downstream, `software_radio` adjacent-channel rejection improves from
  119.5 dB to 125.9 dB on the reference config, and `acquisition_demo` gains
  ~3.5 dB across the number-system sweep and ~5.5 dB across the ADC bit-depth
  scan. Both documentation pages carry re-measured tables.

  The DC-gain tests now assert `|DC - 1| == delta` against the design's own
  measured ripple rather than a fixed 1e-3 tolerance, which is both the
  mathematically correct statement and a tighter one; the `exact_dc_gain=true`
  path keeps its own coverage.

- `filter/fir/remez`: all four linear-phase FIR types are now designed
  correctly (#205). The exchange solves for a cosine polynomial, which is only
  valid for Type I; Types II, III and IV realize their zero-phase amplitude as
  `A(f) = q(f) * P(cos 2*pi*f)` for `q = cos(pi*f)`, `sin(2*pi*f)` and
  `sin(pi*f)` respectively. That factor is now folded into the problem
  statement (`W' = W*q`, `D' = D/q`) so the exchange sees a pure
  cosine-polynomial problem, and is reapplied analytically when converting
  the recovered coefficients to taps. The polynomial degree was also wrong for
  Types II and III — both were one too high — and the grid now excludes the
  frequencies where `q` vanishes, since `D/q` is singular there.

  Type II (even tap counts) previously returned roughly half the intended DC
  gain with ~90 dB of passband ripple; at 64 taps on `{0, 0.20, 0.25, 0.5}` it
  now measures DC 0.9987, ripple 0.0234 dB, stopband 57.4 dB — the
  Parks-McClellan values to three digits. A 31-tap Hilbert transformer over
  `[0.05, 0.45]` went from 1.487 dB of ripple to 0.047 dB (reference: 0.047).
  Across 42 specifications spanning all four types and both parities, 40 agree
  with `scipy.signal.remez` within 3%; the two that do not are wide-band
  Hilbert designs where both implementations are past -160 dB.

  The convergence check added for #203 now applies to all four types rather
  than only the symmetric ones, since `delta` finally describes what every
  type returns.

- `filter/fir/remez`: the exchange keeps the best iterate seen rather than the
  last one, and the barycentric scale factor is derived from the actual node
  range instead of being fixed at 2. Together these fix designs whose optimum
  lies near the double-precision floor — a wide-band Hilbert transformer with
  a generous tap budget — where `delta` collapses toward rounding noise and
  the reference set could stampede into a narrow cluster and blow up the
  interpolation. A 64-tap Hilbert over `[0.10, 0.5]` now lands at 9.0e-10
  against scipy's 3.1e-10, where before it diverged outright.

- `filter/fir/remez`: `remez()` now throws `std::runtime_error` when the
  exchange fails to converge, instead of returning the non-converged iterate.
  Some specifications — notably half-band band edges with many taps and a
  narrow transition — demand more attenuation than double precision can
  represent and are degenerate for the exchange; reference implementations
  report this rather than returning a filter whose stopband sits above its
  passband. The check is the alternation theorem applied to the design that is
  about to be returned. A second acceptance route admits designs whose worst
  weighted error is below 1e-4 of the problem scale (about -80 dB) without
  proving them equiripple, since specifications solved almost exactly drive
  `delta` into rounding noise and the alternation test then compares against a
  meaningless number. Specifications that neither route admits are the ones
  `scipy.signal.remez` also refuses.
- `filter/fir/remez`: barycentric weights now fold a factor of 2 into each node
  difference, the standard Parks-McClellan scaling. The factor is common to all
  weights and cancels in every ratio it appears in, but it keeps the products
  near O(N) instead of growing like 2^N.

## [0.8.0] — 2026-08-03

Two multi-issue tracks land on top of v0.7: the multirate demonstrator backlog
and the Pipeline Probe & Transfer-Function Monitor epic. All 10 issues in the
v0.8 milestone are closed.

### Added

**Pipeline Probe module — `sw::dsp::probe`**

- `SignalProbe` + `NoOpProbe` + `ProbedStage` capture infrastructure, letting any
  pipeline stage be instrumented without changing its type when probing is off
  (#155). `include/sw/dsp/probe/signal_probe.hpp`
- Four domain views over captured buffers — time, magnitude, phase, and I-Q
  constellation (#156). `include/sw/dsp/probe/views.hpp`

**Transfer-Function Monitor module — `sw::dsp::transfer_function`**

- `sweep_bode` numerical Bode analyzer for arbitrary LTI blocks — magnitude,
  phase, and group-delay sweeps (#157). `include/sw/dsp/transfer_function/bode.hpp`
- Closed-form analytical pole/zero extraction for all five filter families —
  Butterworth, Chebyshev I, Chebyshev II, Bessel, and Elliptic (#158, #202).
  `include/sw/dsp/transfer_function/pole_zero.hpp`

**Multirate demonstrators — `applications/multirate_examples/`**

- `audio_resampler` — 44.1/48 kHz rational sample-rate conversion (#136)
- `fractional_delay` — polyphase 1/L-sample fractional delay (#138)
- `channelizer` — Bellanger polyphase M-channel analysis bank (#137)
- `software_radio` — 100 MHz → 100 kHz SDR receiver chain (#139)

Every demo carries an acceptance criterion that is checked at exit, so a
regression in the underlying DSP fails the demo rather than printing bad numbers.

**Library**

- `sw::dsp::multirate` gains the `FractionalDelay` and `Channelizer` classes.
- `elliptic_sn_series` promoted from an internal helper in `filter/iir/elliptic.hpp`
  to public API in `include/sw/dsp/math/elliptic_integrals.hpp`, so the elliptic
  pole/zero prototype and the elliptic filter design share one implementation.

**Documentation**

- New docs-site sidebar categories for Pipeline Probes and the Transfer Function
  Monitor, with overview pages for `probe`, `probe/views`, and
  `transfer-function`, plus cross-references from the `analysis` and
  `acquisition` overviews (#159).

### Changed

- `filter/iir/elliptic.hpp` now consumes the shared `elliptic_sn_series` instead
  of its own local copy (net −23 lines).

## [0.7.0] — 2026-08-02

### Added

- `scope_demo_2ch` — two-channel oscilloscope demo (#173) exercising
  `CrossChannelTrigger` (AandB coincidence mode) and `ChannelAligner`
  (0.3-sample fractional skew compensation). Five precision plans including an
  asymmetric per-channel plan (posit32 chA + posit16 chB); acceptance criterion
  is cross-channel Pearson correlation > 0.99.

## [0.6.1] — 2026-08-02

Tagged as PATCH per project preference; scope covers everything on main since
v0.6.0 (2026-04-27), which under strict semver would have warranted a MINOR bump.

### Added

- Full spectrum-analyzer module: `RealtimeSpectrum` streaming FFT engine, RBW/VBW
  filters, swept-LO chirp generator, front-end corrector, trace averaging
  (5 modes), waterfall buffer, markers + peak-find, 5 detector reducers, and the
  `spectrum_analyzer_demo` application.
- `scope_demo` capstone application with per-stage precision plans and
  calibration-profile pre-distortion.
- `CMakePresets.json` with `dev` / `release` / `ci` / `ci-regression` presets,
  regression-test scaffolding, and the CTest label taxonomy.

### Fixed

- `windows`: `dolph_chebyshev_window` collapsed to a near-constant window at every
  (N, atten_db). The antisymmetric-real spectrum canceled in the cos-only IDFT,
  leaving only the DC bin. Fixed by dropping the (−1)^k twist and taking `abs()`
  of the Chebyshev polynomial to make W symmetric, then fftshifting the cos-IDFT
  result (#200).
- `filter/fir`: `design_fir_lowpass` produced NaN at the center tap under narrow
  cfloat types (#201).

## [0.6.0] — 2026-04-27

Version bump release; see the GitHub Release notes generated from conventional
commits for the change set.

## [0.5.0] — 2026-04-19

Version bump release; see the GitHub Release notes generated from conventional
commits for the change set.

## [0.4.1] — 2026-04-17

### Added

- Tag-based release workflow (`.github/workflows/release.yml`) that creates a
  GitHub Release with an auto-generated changelog from conventional commits when
  a `v*` tag is pushed, verifying the tag matches the CMake version first.

[Unreleased]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.6.1...v0.7.0
[0.6.1]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/stillwater-sc/mixed-precision-dsp/compare/v0.4.1...v0.5.0
[0.4.1]: https://github.com/stillwater-sc/mixed-precision-dsp/releases/tag/v0.4.1
