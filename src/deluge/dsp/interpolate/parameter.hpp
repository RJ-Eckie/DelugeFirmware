#pragma once
#include <cstddef>

namespace deluge::dsp::interpolated {
class Parameter {
public:
	Parameter(float& state, float new_value, size_t size)
	    : state_(state), value_(new_value), increment_((new_value - state) / static_cast<float>(size)) {}

	constexpr float Next() {
		value_ += increment_;
		return value_;
	}

	constexpr float subsample(float t) { return value_ + (increment_ * t); }

private:
	float& state_;
	float value_;
	float increment_;
};
} // namespace deluge::dsp::interpolated
