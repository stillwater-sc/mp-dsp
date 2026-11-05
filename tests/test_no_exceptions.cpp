// test_no_exceptions.cpp: the biquad design path builds with -fno-exceptions (#217)
//
// Flight-controller and other bare-metal firmware builds with -fno-exceptions.
// tests/CMakeLists.txt compiles this file with -fno-exceptions -Werror on GCC
// and Clang, so a try, catch or throw that reaches these headers breaks the
// build on every CI job using those compilers. The test runs a real-time
// redesign loop: an RPM-tracked notch whose coefficients change every tick.
//
// Unlike the other tests it cannot throw on failure; it prints the failure and
// returns nonzero. Universal's posit does not yet build without exceptions, so
// only native types are used here; test_rbj_coefficients checks posit<32,2>.
//
// NDEBUG is undefined so that the free functions' precondition asserts are
// compiled and run here even in the Release builds CI uses.
//
// Copyright (C) 2024-2026 Stillwater Supercomputing, Inc.
// SPDX-License-Identifier: MIT

#ifdef NDEBUG
#  undef NDEBUG
#endif

#include <sw/dsp/types/biquad_coefficients.hpp>
#include <sw/dsp/filter/biquad/biquad.hpp>
#include <sw/dsp/filter/biquad/cascade.hpp>
#include <sw/dsp/filter/biquad/smooth.hpp>
#include <sw/dsp/filter/biquad/state.hpp>
#include <sw/dsp/filter/iir/rbj.hpp>
#include <sw/dsp/filter/iir/butterworth.hpp>
#include <sw/dsp/analysis/stability.hpp>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#  error "this test must be built with exceptions disabled"
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <span>

using namespace sw::dsp;
namespace rbj = sw::dsp::iir::rbj;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("FAILED: %s\n", what);
		++failures;
	}
}

template <typename T>
void run(const char* type) {
	// The control-loop case: redesign a notch every tick as the motor's RPM
	// moves, and filter one sample with it.
	constexpr double fs = 1000.0;                       // 1 kHz loop
	Cascade<T, 1> notch;
	notch.set_num_stages(1);
	std::array<DirectFormII<T>, 1> state{};
	bool all_valid = true, all_stable = true;
	T y{};
	for (int tick = 0; tick < 200; ++tick) {
		const double rpm_hz = 80.0 + 1.5 * tick;        // 80 .. 378.5 Hz
		const T w0 = rbj::angular_frequency<T>(fs, rpm_hz);
		const T q = T(8);
		all_valid = all_valid && rbj::rbj_parameters_valid(w0, q);
		notch.stage(0) = rbj::bandstop_coefficients(w0, q);
		all_stable = all_stable && is_stable(notch);
		y = notch.process(T(std::sin(0.3 * tick)), state);
	}
	check(all_valid, "notch parameters valid every tick");
	check(all_stable, "notch stable every tick");
	check(std::isfinite(double(y)), "notch output finite");

	// Every shape, and setup() on valid arguments.
	const T w0 = rbj::angular_frequency<T>(48000.0, 1000.0);
	const auto lp = rbj::lowpass_coefficients(w0, T(0.7071));
	rbj::LowPass<T> lp_filter;
	lp_filter.setup(48000.0, 1000.0, 0.7071);
	check(lp.b0 == lp_filter.cascade().stage(0).b0 && lp.a2 == lp_filter.cascade().stage(0).a2,
	      "lowpass_coefficients matches setup()");
	check(is_stable(rbj::highpass_coefficients(w0, T(0.7071))), "highpass stable");
	check(is_stable(rbj::bandpass_coefficients(w0, T(1))), "bandpass stable");
	check(is_stable(rbj::allpass_coefficients(w0, T(0.7071))), "allpass stable");
	check(is_stable(rbj::lowshelf_coefficients(w0, T(6), T(1))), "lowshelf stable");
	check(is_stable(rbj::highshelf_coefficients(w0, T(-6), T(1))), "highshelf stable");
	check(rbj::rbj_shelf_parameters_valid(w0, T(6), T(1)), "shelf parameters valid");
	rbj::HighShelf<T> hs;
	hs.setup(48000.0, 1000.0, -6.0);
	check(is_stable(hs.cascade()), "HighShelf::setup stable");

	// A Butterworth design, run through a smoothed cascade.
	iir::ButterworthLowPass<4, T> bw;
	bw.setup(4, 48000.0, 2000.0);
	check(is_stable(bw.cascade()), "Butterworth stable");
	SmoothedCascade<T, 2> smooth(16);
	smooth.set_cascade(bw.cascade());
	std::array<DirectFormII<T>, 2> bw_state{};
	std::array<T, 32> block{};
	block[0] = T(1);
	smooth.process_block(std::span<T>(block), bw_state);
	check(std::isfinite(double(block[31])), "Butterworth impulse response finite");

	if (failures == 0) std::printf("  %s: passed\n", type);
}

} // namespace

int main() {
	std::printf("biquad design path without exceptions (#217)\n");
	run<double>("double");
	run<float>("float");
	if (failures == 0) std::printf("all checks passed\n");
	return failures == 0 ? 0 : 1;
}
