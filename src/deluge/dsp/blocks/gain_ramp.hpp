#pragma once

#include "dsp/stereo_sample.h"
#include <argon.hpp>

namespace deluge::dsp::blocks {
class GainRamp {
public:
	GainRamp(float start, float end) : start_{start}, end_{end} {}

	void processBlock(std::span<float> in, std::span<float> out) const;
	void processBlock(std::span<StereoFloatSample> in, std::span<StereoFloatSample> out);

	[[nodiscard]] constexpr float start() const { return start_; }
	[[nodiscard]] constexpr float end() const { return end_; }

private:
	float start_;
	float end_;
};
} // namespace deluge::dsp::blocks
