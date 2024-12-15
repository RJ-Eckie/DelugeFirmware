#pragma once
#include "util/fixedpoint.h"
#include <ratio>
#include <string_view>

template <typename T>
struct Value {
	T value = 0;
	operator T() { return value; }
	auto operator<=>(const Value<T>& o) const = default;
};

template <typename T>
struct Frequency : Value<T> {
	Frequency() : Value<T>() {}
	Frequency(T value) : Value<T>{value} {}
	static constexpr std::string_view unit = "Hz";
};

template <typename T>
struct Percentage;

template <>
struct Percentage<float> : Value<float> {
	static constexpr std::string_view unit = "%";

	Percentage() = default;
	Percentage(float value) : Value<float>{value} {}
	Percentage(float value, float lower_bound, float upper_bound)
	    : Value<float>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	float lower_bound = 0.f;
	float upper_bound = 1.f;
};

template <>
struct Percentage<q31_t> : Value<q31_t> {
	static constexpr std::string_view unit = "%";

	Percentage() = default;
	Percentage(q31_t value) : Value<q31_t>{value} {}
	Percentage(q31_t value, q31_t lower_bound, q31_t upper_bound)
	    : Value<q31_t>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	Percentage(float value) : Value<q31_t>{q31_from_float(value)} {}
	Percentage(float value, float lower_bound, float upper_bound)
	    : Value<q31_t>{q31_from_float(value)}, lower_bound{q31_from_float(lower_bound)},
	      upper_bound{q31_from_float(upper_bound)} {}

	q31_t lower_bound = q31_from_float(0.f);
	q31_t upper_bound = q31_from_float(1.f);
};

template <typename T>
struct QFactor : Value<T> {
	QFactor(T value) : Value<T>{value} {}
};

struct Milliseconds : Value<int32_t> {
	static constexpr std::string_view unit = "ms";
};
