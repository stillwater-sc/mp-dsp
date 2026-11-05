#pragma once
// rbj.hpp: Robert Bristow-Johnson Audio EQ Cookbook biquad filters
//
// Direct coefficient formulas — no analog prototype needed.
// Reference: http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
//
// The public setup() APIs take `double` parameters (sample_rate, freq, Q,
// gain_db, slope) for interface stability, but all intermediate
// coefficient math runs in CoeffScalar. A posit or fixed-point CoeffScalar
// therefore produces coefficients that were computed end-to-end at the
// caller's declared precision — required for embedded mixed-precision
// deployments where filter design executes on the target.
//
// ADL-friendly trig (using std::cos; cos(x);) picks up sw::universal::cos
// for Universal number types and std::cos for native floats.
//
// Real-time use, and builds without exceptions (#217): each shape is also a
// free function, <shape>_coefficients(w0, ...), with w0 = 2*pi*f/fs in
// radians per sample. The free functions are noexcept, never allocate, and
// return the coefficients; their preconditions are checked with assert, not
// thrown, and rbj_parameters_valid / rbj_shelf_parameters_valid check them
// without either. setup() validates its double arguments, computes w0 with
// angular_frequency<CoeffScalar>, and runs the same formula, so its result is
// bit-identical to the free function given that w0. Built without exceptions,
// setup() aborts where it would have thrown std::invalid_argument.
//
// Copyright (C) 2024-2026 Stillwater Supercomputing, Inc.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sw/dsp/concepts/scalar.hpp>
#include <sw/dsp/math/constants.hpp>
#include <sw/dsp/types/biquad_coefficients.hpp>
#include <sw/dsp/filter/biquad/cascade.hpp>
#include <sw/dsp/filter/biquad/state.hpp>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define SW_DSP_RBJ_HAS_EXCEPTIONS 1
#else
#  define SW_DSP_RBJ_HAS_EXCEPTIONS 0
#endif

namespace sw::dsp::iir::rbj {

/// w0 = 2*pi*freq/sample_rate, computed in T exactly as setup() computes it.
template <DspField T>
T angular_frequency(double sample_rate, double freq) noexcept {
	return T(two_pi) * T(freq) / T(sample_rate);
}

namespace detail {

// Normalize biquad by dividing all coefficients by a0
template <DspField T>
BiquadCoefficients<T> normalize(T b0, T b1, T b2, T a0, T a1, T a2) noexcept {
	return BiquadCoefficients<T>(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
}

// x - x is 0 for a finite x, and NaN (or NaR) for an infinite or NaN one.
template <DspField T>
bool is_finite(T x) noexcept {
	return x - x == T(0);
}

// setup()'s argument errors: std::invalid_argument("<who>: <what>"), or
// std::abort() when built without exceptions.
[[noreturn]] inline void invalid_argument(const char* who, const char* what) {
#if SW_DSP_RBJ_HAS_EXCEPTIONS
	throw std::invalid_argument(std::string(who) + ": " + what);
#else
	(void)who; (void)what;
	std::abort();
#endif
}

inline void validate_freq(double sample_rate, double freq, const char* name) {
	if (!(sample_rate > 0.0) || !std::isfinite(sample_rate))
		invalid_argument(name, "sample_rate must be > 0 and finite");
	if (!(freq > 0.0) || !std::isfinite(freq))
		invalid_argument(name, "frequency must be > 0 and finite");
	if (freq >= sample_rate * 0.5)
		invalid_argument(name, "frequency must be < Nyquist (sample_rate/2)");
}

inline void validate_q(double q, const char* name) {
	if (!(q > 0.0) || !std::isfinite(q))
		invalid_argument(name, "Q/bandwidth must be > 0 and finite");
}

inline void validate_shelf(double gain_db, double slope, const char* name) {
	if (!(slope > 0.0)) invalid_argument(name, "slope must be > 0");
	if (!std::isfinite(gain_db)) invalid_argument(name, "gain_db must be finite");
}

// The cookbook formulas, unchecked. setup() calls these after validating its
// double arguments: near Nyquist, w0 rounded to a low-precision T can reach
// T(pi) although freq < sample_rate/2, and setup() has always accepted that.

template <DspField T>
BiquadCoefficients<T> lowpass(T w0, T q) noexcept {
	using std::cos; using std::sin;
	constexpr T one = T(1);
	constexpr T two = T(2);
	const T cs = cos(w0);
	const T sn = sin(w0);
	const T alpha = sn / (two * q);
	return normalize<T>((one - cs) / two, one - cs, (one - cs) / two,
	                    one + alpha, -(two * cs), one - alpha);
}

template <DspField T>
BiquadCoefficients<T> highpass(T w0, T q) noexcept {
	using std::cos; using std::sin;
	constexpr T one = T(1);
	constexpr T two = T(2);
	const T cs = cos(w0);
	const T sn = sin(w0);
	const T alpha = sn / (two * q);
	return normalize<T>((one + cs) / two, -(one + cs), (one + cs) / two,
	                    one + alpha, -(two * cs), one - alpha);
}

template <DspField T>
BiquadCoefficients<T> bandpass(T w0, T q) noexcept {
	using std::cos; using std::sin;
	constexpr T zero = T(0);
	constexpr T one  = T(1);
	constexpr T two  = T(2);
	const T cs = cos(w0);
	const T sn = sin(w0);
	const T alpha = sn / (two * q);
	return normalize<T>(alpha, zero, -alpha,
	                    one + alpha, -(two * cs), one - alpha);
}

template <DspField T>
BiquadCoefficients<T> bandstop(T w0, T q) noexcept {
	using std::cos; using std::sin;
	constexpr T one = T(1);
	constexpr T two = T(2);
	const T cs = cos(w0);
	const T sn = sin(w0);
	const T alpha = sn / (two * q);
	return normalize<T>(one, -(two * cs), one,
	                    one + alpha, -(two * cs), one - alpha);
}

template <DspField T>
BiquadCoefficients<T> allpass(T w0, T q) noexcept {
	using std::cos; using std::sin;
	constexpr T one = T(1);
	constexpr T two = T(2);
	const T cs = cos(w0);
	const T sn = sin(w0);
	const T alpha = sn / (two * q);
	return normalize<T>(one - alpha, -(two * cs), one + alpha,
	                    one + alpha, -(two * cs), one - alpha);
}

// The shelves' shared terms: A, cos(w0) and 2*sqrt(A)*alpha.
template <DspField T>
struct shelf_terms { T A, cs, sq; };

template <DspField T>
shelf_terms<T> shelf(T w0, T gain_db, T slope) noexcept {
	using std::cos; using std::sin; using std::pow; using std::sqrt;
	constexpr T zero  = T(0);
	constexpr T one   = T(1);
	constexpr T two   = T(2);
	constexpr T ten   = T(10);
	constexpr T forty = T(40);
	const T A = pow(ten, gain_db / forty);
	const T cs = cos(w0);
	const T sn = sin(w0);
	const T inv_A = one / A;
	const T inv_slope = one / slope;
	T radicand = (A + inv_A) * (inv_slope - one) + two;
	if (radicand < zero) radicand = zero;
	const T alpha = sn / two * sqrt(radicand);
	return {A, cs, two * sqrt(A) * alpha};
}

template <DspField T>
BiquadCoefficients<T> lowshelf(T w0, T gain_db, T slope) noexcept {
	constexpr T one = T(1);
	constexpr T two = T(2);
	const auto [A, cs, sq] = shelf<T>(w0, gain_db, slope);
	const T Ap1 = A + one;
	const T Am1 = A - one;
	return normalize<T>(A * (Ap1 - Am1*cs + sq), two * A * (Am1 - Ap1*cs),
	                    A * (Ap1 - Am1*cs - sq),
	                    Ap1 + Am1*cs + sq, -(two * (Am1 + Ap1*cs)), Ap1 + Am1*cs - sq);
}

template <DspField T>
BiquadCoefficients<T> highshelf(T w0, T gain_db, T slope) noexcept {
	constexpr T one = T(1);
	constexpr T two = T(2);
	const auto [A, cs, sq] = shelf<T>(w0, gain_db, slope);
	const T Ap1 = A + one;
	const T Am1 = A - one;
	return normalize<T>(A * (Ap1 + Am1*cs + sq), -(two * A * (Am1 + Ap1*cs)),
	                    A * (Ap1 + Am1*cs - sq),
	                    Ap1 - Am1*cs + sq, two * (Am1 - Ap1*cs), Ap1 - Am1*cs - sq);
}

} // namespace detail

// -- Coefficients without exceptions or allocation (#217) -------------------

/// True when 0 < w0 < pi and q is finite and > 0: the preconditions of
/// lowpass/highpass/bandpass/bandstop/allpass_coefficients (q is the
/// bandwidth parameter for bandpass and bandstop).
template <DspField T>
bool rbj_parameters_valid(T w0, T q) noexcept {
	return T(0) < w0 && w0 < T(pi) && detail::is_finite(q) && T(0) < q;
}

/// True when 0 < w0 < pi, gain_db is finite and slope > 0: the preconditions
/// of lowshelf/highshelf_coefficients.
template <DspField T>
bool rbj_shelf_parameters_valid(T w0, T gain_db, T slope) noexcept {
	return T(0) < w0 && w0 < T(pi) && detail::is_finite(gain_db) && T(0) < slope;
}

template <DspField T>
BiquadCoefficients<T> lowpass_coefficients(T w0, T q) noexcept {
	assert(rbj_parameters_valid(w0, q));
	return detail::lowpass(w0, q);
}

template <DspField T>
BiquadCoefficients<T> highpass_coefficients(T w0, T q) noexcept {
	assert(rbj_parameters_valid(w0, q));
	return detail::highpass(w0, q);
}

template <DspField T>
BiquadCoefficients<T> bandpass_coefficients(T w0, T bandwidth) noexcept {
	assert(rbj_parameters_valid(w0, bandwidth));
	return detail::bandpass(w0, bandwidth);
}

template <DspField T>
BiquadCoefficients<T> bandstop_coefficients(T w0, T bandwidth) noexcept {
	assert(rbj_parameters_valid(w0, bandwidth));
	return detail::bandstop(w0, bandwidth);
}

template <DspField T>
BiquadCoefficients<T> allpass_coefficients(T w0, T q) noexcept {
	assert(rbj_parameters_valid(w0, q));
	return detail::allpass(w0, q);
}

template <DspField T>
BiquadCoefficients<T> lowshelf_coefficients(T w0, T gain_db, T slope) noexcept {
	assert(rbj_shelf_parameters_valid(w0, gain_db, slope));
	return detail::lowshelf(w0, gain_db, slope);
}

template <DspField T>
BiquadCoefficients<T> highshelf_coefficients(T w0, T gain_db, T slope) noexcept {
	assert(rbj_shelf_parameters_valid(w0, gain_db, slope));
	return detail::highshelf(w0, gain_db, slope);
}

// Each RBJ filter is a single biquad with three-scalar parameterization.
// CoeffScalar is used for coefficient computation; StateScalar and
// SampleScalar are forwarded to the cascade/state for processing.

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class LowPass {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double cutoff_freq, double q = 0.7071) {
		detail::validate_freq(sample_rate, cutoff_freq, "rbj::LowPass");
		detail::validate_q(q, "rbj::LowPass");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::lowpass(
			angular_frequency<CoeffScalar>(sample_rate, cutoff_freq), CoeffScalar(q));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class HighPass {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double cutoff_freq, double q = 0.7071) {
		detail::validate_freq(sample_rate, cutoff_freq, "rbj::HighPass");
		detail::validate_q(q, "rbj::HighPass");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::highpass(
			angular_frequency<CoeffScalar>(sample_rate, cutoff_freq), CoeffScalar(q));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class BandPass {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double center_freq, double bandwidth = 1.0) {
		detail::validate_freq(sample_rate, center_freq, "rbj::BandPass");
		detail::validate_q(bandwidth, "rbj::BandPass");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::bandpass(
			angular_frequency<CoeffScalar>(sample_rate, center_freq), CoeffScalar(bandwidth));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class BandStop {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double center_freq, double bandwidth = 1.0) {
		detail::validate_freq(sample_rate, center_freq, "rbj::BandStop");
		detail::validate_q(bandwidth, "rbj::BandStop");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::bandstop(
			angular_frequency<CoeffScalar>(sample_rate, center_freq), CoeffScalar(bandwidth));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class AllPass {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double center_freq, double q = 0.7071) {
		detail::validate_freq(sample_rate, center_freq, "rbj::AllPass");
		detail::validate_q(q, "rbj::AllPass");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::allpass(
			angular_frequency<CoeffScalar>(sample_rate, center_freq), CoeffScalar(q));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class LowShelf {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double cutoff_freq, double gain_db, double slope = 1.0) {
		detail::validate_freq(sample_rate, cutoff_freq, "rbj::LowShelf");
		detail::validate_shelf(gain_db, slope, "rbj::LowShelf");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::lowshelf(
			angular_frequency<CoeffScalar>(sample_rate, cutoff_freq),
			CoeffScalar(gain_db), CoeffScalar(slope));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

template <DspField CoeffScalar = double,
          DspField StateScalar = CoeffScalar,
          DspScalar SampleScalar = StateScalar>
class HighShelf {
public:
	using coeff_scalar  = CoeffScalar;
	using state_scalar  = StateScalar;
	using sample_scalar = SampleScalar;
	static constexpr int max_stages = 1;

	void setup(double sample_rate, double cutoff_freq, double gain_db, double slope = 1.0) {
		detail::validate_freq(sample_rate, cutoff_freq, "rbj::HighShelf");
		detail::validate_shelf(gain_db, slope, "rbj::HighShelf");
		cascade_.set_num_stages(1);
		cascade_.stage(0) = detail::highshelf(
			angular_frequency<CoeffScalar>(sample_rate, cutoff_freq),
			CoeffScalar(gain_db), CoeffScalar(slope));
	}

	const Cascade<CoeffScalar, 1>& cascade() const { return cascade_; }

private:
	Cascade<CoeffScalar, 1> cascade_{};
};

} // namespace sw::dsp::iir::rbj
