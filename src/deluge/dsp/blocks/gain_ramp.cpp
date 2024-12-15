#include "gain_ramp.hpp"
#include "argon/store.hpp"

namespace deluge::dsp::blocks {

void GainRamp::processBlock(const std::span<float> in, std::span<float> out) const {
	float single_step = (end_ - start_) / static_cast<float>(in.size() - 1);

	// NEON-accelerated version
	Argon<float> current = Argon<float>{start_}.MultiplyAdd(single_step, {0.f, 1.f, 2.f, 3.f});

	size_t vec_size = in.size() & ~(Argon<float>::lanes - 1);
	Argon<float> step = single_step * Argon<float>::lanes;

	for (size_t i = 0; i < vec_size; i += Argon<float>::lanes) {
		auto in_sample = Argon<float>::Load(&in[i]); // Load four mono samples
		auto out_sample = in_sample * current;       // Apply gain
		out_sample.StoreTo(&out[i]);                 // Store four mono samples
		current = current + step;                    // Move to next segment of ramp
	}

	// Do remainder that don't fit the vector width
	float single_current = current[1];
	for (size_t i = vec_size; i < in.size(); ++i) {
		out[i] = in[i] * single_current;
		single_current += single_step;
	}
}

void GainRamp::processBlock(std::span<StereoFloatSample> in, std::span<StereoFloatSample> out) {
	float single_step = (end_ - start_) / static_cast<float>(in.size() - 1);

	Argon<float> current = Argon<float>{start_}.MultiplyAdd(single_step, {0.f, 1.f, 2.f, 3.f});

	size_t vec_size = in.size() & ~(Argon<float>::lanes - 1);
	Argon<float> step = single_step * Argon<float>::lanes;

	for (size_t i = 0; i < vec_size; i += Argon<float>::lanes) {
		auto [in_sample_l, in_sample_r] = Argon<float>::LoadInterleaved<2>(&in[i].l); // Load four stereo samples
		Argon<float> out_sample_l = in_sample_l * current;                            // Apply gain
		Argon<float> out_sample_r = in_sample_r * current;                            // Apply gain
		argon::store_interleaved<2>(&out[i].l, out_sample_l, out_sample_r);           // Store four stereo samples
		current = current + step;                                                     // Move to next segment of ramp
	}

	// Do remainder that don't fit the vector width
	float single_current = current[1];
	for (size_t i = vec_size; i < in.size(); ++i) {
		out[i].l = in[i].l * single_current;
		out[i].r = in[i].r * single_current;
		single_current += single_step;
	}
}
} // namespace deluge::dsp::blocks
