// test_rbj_coefficients.cpp: the noexcept RBJ coefficient functions (#217)
//
// lowpass_coefficients() and the other free functions must give exactly the
// coefficients that the matching setup() stores, given w0 from
// angular_frequency<T>, in double, float and posit<32,2>. Also checks the
// parameter-validity helpers, and that setup() still throws the same
// std::invalid_argument messages.
//
// Copyright (C) 2024-2026 Stillwater Supercomputing, Inc.
// SPDX-License-Identifier: MIT

#include <sw/dsp/filter/iir/rbj.hpp>
#include <sw/dsp/math/constants.hpp>

#include <universal/number/posit/posit.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace sw::dsp;
namespace rbj = sw::dsp::iir::rbj;

namespace {

int checks = 0;

template <typename T>
bool same(const BiquadCoefficients<T>& x, const BiquadCoefficients<T>& y) {
	return x.b0 == y.b0 && x.b1 == y.b1 && x.b2 == y.b2 && x.a1 == y.a1 && x.a2 == y.a2;
}

template <typename T>
void require_same(const BiquadCoefficients<T>& from_setup, const BiquadCoefficients<T>& from_free,
                  const char* shape, const char* type, double fs, double f, double p) {
	++checks;
	if (!same(from_setup, from_free))
		throw std::runtime_error(std::string("test failed: ") + shape + "<" + type
		    + "> free function differs from setup() at fs=" + std::to_string(fs)
		    + " f=" + std::to_string(f) + " param=" + std::to_string(p));
}

constexpr std::array<double, 3> sample_rates{1000.0, 8000.0, 48000.0};
constexpr std::array<double, 6> fractions{0.001, 0.01, 0.1, 0.25, 0.45, 0.49};  // of fs
constexpr std::array<double, 5> qs{0.3, 0.7071, 1.0, 5.0, 30.0};
constexpr std::array<double, 5> gains_db{-12.0, -3.0, 0.0, 3.0, 12.0};
constexpr std::array<double, 3> slopes{0.5, 1.0, 2.0};

// Each two-parameter shape: setup(fs, f, q) against <shape>_coefficients(w0, q).
template <typename T, typename Filter, typename Free>
void check_q_shape(const char* shape, const char* type, Free free) {
	for (double fs : sample_rates)
		for (double frac : fractions)
			for (double q : qs) {
				const double f = frac * fs;
				Filter filter;
				filter.setup(fs, f, q);
				const T w0 = rbj::angular_frequency<T>(fs, f);
				require_same(filter.cascade().stage(0), free(w0, T(q)), shape, type, fs, f, q);
			}
}

template <typename T, typename Filter, typename Free>
void check_shelf(const char* shape, const char* type, Free free) {
	for (double fs : sample_rates)
		for (double frac : fractions)
			for (double g : gains_db)
				for (double slope : slopes) {
					const double f = frac * fs;
					Filter filter;
					filter.setup(fs, f, g, slope);
					const T w0 = rbj::angular_frequency<T>(fs, f);
					require_same(filter.cascade().stage(0), free(w0, T(g), T(slope)),
					             shape, type, fs, f, g);
				}
}

template <typename T>
void check_bit_identical(const char* type) {
	check_q_shape<T, rbj::LowPass<T>>("lowpass", type,
	    [](T w0, T q) { return rbj::lowpass_coefficients(w0, q); });
	check_q_shape<T, rbj::HighPass<T>>("highpass", type,
	    [](T w0, T q) { return rbj::highpass_coefficients(w0, q); });
	check_q_shape<T, rbj::BandPass<T>>("bandpass", type,
	    [](T w0, T q) { return rbj::bandpass_coefficients(w0, q); });
	check_q_shape<T, rbj::BandStop<T>>("bandstop", type,
	    [](T w0, T q) { return rbj::bandstop_coefficients(w0, q); });
	check_q_shape<T, rbj::AllPass<T>>("allpass", type,
	    [](T w0, T q) { return rbj::allpass_coefficients(w0, q); });
	check_shelf<T, rbj::LowShelf<T>>("lowshelf", type,
	    [](T w0, T g, T s) { return rbj::lowshelf_coefficients(w0, g, s); });
	check_shelf<T, rbj::HighShelf<T>>("highshelf", type,
	    [](T w0, T g, T s) { return rbj::highshelf_coefficients(w0, g, s); });
	std::cout << "  bit-identical to setup(): " << type << " passed\n";
}

void require(bool ok, const char* what) {
	++checks;
	if (!ok) throw std::runtime_error(std::string("test failed: ") + what);
}

void test_noexcept() {
	static_assert(noexcept(rbj::lowpass_coefficients(0.1, 0.7)));
	static_assert(noexcept(rbj::highpass_coefficients(0.1, 0.7)));
	static_assert(noexcept(rbj::bandpass_coefficients(0.1, 0.7)));
	static_assert(noexcept(rbj::bandstop_coefficients(0.1, 0.7)));
	static_assert(noexcept(rbj::allpass_coefficients(0.1, 0.7)));
	static_assert(noexcept(rbj::lowshelf_coefficients(0.1, 3.0, 1.0)));
	static_assert(noexcept(rbj::highshelf_coefficients(0.1, 3.0, 1.0)));
	static_assert(noexcept(rbj::rbj_parameters_valid(0.1, 0.7)));
	static_assert(noexcept(rbj::rbj_shelf_parameters_valid(0.1, 3.0, 1.0)));
	static_assert(noexcept(rbj::angular_frequency<float>(48000.0, 1000.0)));
	std::cout << "  noexcept: passed\n";
}

void test_parameters_valid() {
	const double inf = std::numeric_limits<double>::infinity();
	const double nan = std::numeric_limits<double>::quiet_NaN();
	require(rbj::rbj_parameters_valid(0.1, 0.7071), "valid w0, q");
	require(rbj::rbj_parameters_valid(3.1, 30.0), "w0 just under pi");
	require(!rbj::rbj_parameters_valid(0.0, 0.7), "w0 = 0");
	require(!rbj::rbj_parameters_valid(-0.1, 0.7), "w0 < 0");
	require(!rbj::rbj_parameters_valid(pi, 0.7), "w0 = pi");
	require(!rbj::rbj_parameters_valid(4.0, 0.7), "w0 > pi");
	require(!rbj::rbj_parameters_valid(nan, 0.7), "w0 NaN");
	require(!rbj::rbj_parameters_valid(0.1, 0.0), "q = 0");
	require(!rbj::rbj_parameters_valid(0.1, -1.0), "q < 0");
	require(!rbj::rbj_parameters_valid(0.1, inf), "q infinite");
	require(!rbj::rbj_parameters_valid(0.1, nan), "q NaN");

	require(rbj::rbj_shelf_parameters_valid(0.1, -12.0, 0.5), "valid shelf");
	require(rbj::rbj_shelf_parameters_valid(0.1, 0.0, 1.0), "shelf gain 0 dB");
	require(!rbj::rbj_shelf_parameters_valid(pi, 3.0, 1.0), "shelf w0 = pi");
	require(!rbj::rbj_shelf_parameters_valid(0.1, inf, 1.0), "shelf gain infinite");
	require(!rbj::rbj_shelf_parameters_valid(0.1, nan, 1.0), "shelf gain NaN");
	require(!rbj::rbj_shelf_parameters_valid(0.1, 3.0, 0.0), "shelf slope = 0");

	using P = sw::universal::posit<32, 2>;
	require(rbj::rbj_parameters_valid(P(0.1), P(0.7071)), "posit valid");
	require(!rbj::rbj_parameters_valid(P(0.1), P(0)), "posit q = 0");
	P nar; nar.setnar();
	require(!rbj::rbj_parameters_valid(P(0.1), nar), "posit q NaR");
	require(!rbj::rbj_parameters_valid(nar, P(0.7)), "posit w0 NaR");
	// Near Nyquist, w0 rounded to float or posit reaches T(pi) although the
	// frequency is below fs/2; the free functions' domain excludes it, since
	// every shape is unstable there, and setup() still accepts the frequency.
	const double f_near = std::nextafter(24000.0, 0.0);
	require(!rbj::rbj_parameters_valid(rbj::angular_frequency<float>(48000.0, f_near), 0.7071f),
	        "float w0 rounded to pi is invalid");
	require(!rbj::rbj_parameters_valid(rbj::angular_frequency<P>(48000.0, f_near), P(0.7071)),
	        "posit w0 rounded past pi is invalid");
	rbj::LowPass<float> near_nyquist;
	near_nyquist.setup(48000.0, f_near, 0.7071);
	++checks;
	std::cout << "  rbj_parameters_valid: passed\n";
}

// setup() keeps throwing std::invalid_argument, with the messages it had.
template <typename Fn>
void require_throws(Fn fn, const std::string& message) {
	++checks;
	try {
		fn();
	} catch (const std::invalid_argument& e) {
		if (message != e.what())
			throw std::runtime_error("test failed: message '" + std::string(e.what())
			                         + "', expected '" + message + "'");
		return;
	}
	throw std::runtime_error("test failed: no exception, expected '" + message + "'");
}

void test_setup_still_throws() {
	require_throws([] { rbj::LowPass<> f; f.setup(0.0, 100.0); },
	               "rbj::LowPass: sample_rate must be > 0 and finite");
	require_throws([] { rbj::HighPass<> f; f.setup(48000.0, -1.0); },
	               "rbj::HighPass: frequency must be > 0 and finite");
	require_throws([] { rbj::BandPass<> f; f.setup(48000.0, 24000.0); },
	               "rbj::BandPass: frequency must be < Nyquist (sample_rate/2)");
	require_throws([] { rbj::BandStop<> f; f.setup(48000.0, 1000.0, 0.0); },
	               "rbj::BandStop: Q/bandwidth must be > 0 and finite");
	require_throws([] { rbj::LowShelf<> f; f.setup(48000.0, 1000.0, 3.0, 0.0); },
	               "rbj::LowShelf: slope must be > 0");
	require_throws([] { rbj::HighShelf<> f; f.setup(48000.0, 1000.0, std::nan(""), 1.0); },
	               "rbj::HighShelf: gain_db must be finite");
	std::cout << "  setup() error messages: passed\n";
}

} // namespace

int main() {
	try {
		std::cout << "RBJ coefficient functions (#217)\n";
		test_noexcept();
		check_bit_identical<double>("double");
		check_bit_identical<float>("float");
		check_bit_identical<sw::universal::posit<32, 2>>("posit<32,2>");
		test_parameters_valid();
		test_setup_still_throws();
		std::cout << "all " << checks << " checks passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
