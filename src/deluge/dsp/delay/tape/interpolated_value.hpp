#pragma once
#include <cmath>
#include <cstddef>

namespace deluge::dsp::util {

enum class InterpolationType {
	Linear,
	Multiplicative,
};

template <typename T, InterpolationType U = InterpolationType::Linear>
class InterpolatedValue {
public:
	InterpolatedValue(T value) { Init(value, value, 1); }

	InterpolatedValue(T value, T target_value, size_t num_steps) { Init(value, target_value, num_steps); }

	inline void Init(T value, T target_value, size_t num_steps) {
		orig_value_ = value;
		value_ = value;
		target_value_ = target_value;
		num_steps_ = num_steps;
		if (value == target_value) {
			increment_ = 0.0f;
		}
		else {
			CalcIncrement();
		}
	}

	inline float Next() {
		if (value_ != target_value_) {
			if constexpr (U == InterpolationType::Linear) {
				value_ += increment_;
			}
			else if constexpr (U == InterpolationType::Multiplicative) {
				value_ *= increment_;
			}
		}
		return value_;
	}

	inline void Reset(float sample_rate, float ramp_length_in_seconds) {
		Reset(static_cast<size_t>(std::floor(ramp_length_in_seconds * sample_rate)));
	}

	inline void Reset(size_t num_steps) {
		num_steps_ = num_steps;
		value_ = orig_value_;
		CalcIncrement();
	}

	inline void Set(T target_value, size_t num_steps) {
		target_value_ = target_value;
		num_steps_ = num_steps;
		CalcIncrement();
	}

	inline void Set(T value) { set_current_and_target(value); }

	T value() { return value_; }
	T target() { return target_value_; }

	inline void set_steps(size_t num_steps) {
		num_steps_ = num_steps;
		if (is_interpolating()) {
			CalcIncrement();
		}
	}

	inline void set_target(T target_value) {
		target_value_ = target_value;
		CalcIncrement();
	}

	inline void set_current_and_target(T value) {
		value_ = value;
		target_value_ = value;
	}

	inline bool is_interpolating() { return (value_ != target_value_); }

private:
	inline void CalcIncrement() {
		auto num_steps_f = static_cast<float>(num_steps_);
		if constexpr (U == InterpolationType::Linear) {
			increment_ = (target_value_ - value_) / num_steps_f;
		}
		else if constexpr (U == InterpolationType::Multiplicative) {
			increment_ = std::exp((std::log(std::abs(target_value_)) - std::log(std::abs(value_))) / num_steps_f);
		}
	}

	T value_ = 0;
	T orig_value_ = 0;
	T target_value_ = 0;
	float increment_ = 0.f;
	size_t num_steps_ = 1;
};
} // namespace deluge::dsp::util
