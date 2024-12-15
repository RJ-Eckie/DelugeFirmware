/*
 * Copyright (c) 2024 Katherine Whitlock
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "argon/vectorize.hpp"
#include "buffer.hpp"
#include "definitions_cxx.hpp"
#include "dsp/blocks/gain_ramp.hpp"
#include "dsp/interpolate/parameter.hpp"
#include "dsp/stereo_sample.h"
#include "model/types.hpp"
#include "util/fixedpoint.h"
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::delay::simple {

// A simple digital delay inspired by Ableton's Delay
class Delay {

	constexpr static size_t kNumSecsDelayMax = 5;
	constexpr static size_t kNumSamplesMainDelay = (5 * kSampleRate);
	constexpr static size_t kNumSamplesMax =
	    kNumSamplesMainDelay + (kNumSamplesMainDelay / 3.f); // 6.666 seconds maximum
public:
	enum class Mode {
		Repitch,
		Fade,
		Jump,
	};

	enum class ChannelMode {
		Sync, // beat-synced
		Time, // strict timing
	};
	struct ChannelConfig {
		ChannelMode mode;
		Milliseconds duration;   // ms for time, beat divisions for sync, internally stored as ms
		Percentage<float> nudge; // up to 0.33 (33%)

		// Comparison ops
		bool operator==(const ChannelConfig& b) const {
			return mode == b.mode && duration == b.duration && nudge == b.nudge;
		}
		bool operator!=(const ChannelConfig& b) const {
			return mode != b.mode || duration != b.duration || nudge != b.nudge;
		}
	};

	struct Config {
		bool channel_link = true; // uses first config for all settings
		ChannelConfig l_channel;  // stereo
		ChannelConfig r_channel;
		Percentage<float> feedback = 0.5f; // 50% for linear decay rate by default
		bool freeze = false; // disables new input and feedback, cycling the current buffer contents infinitely

		// Standard 2 param bandpass
		Frequency<float> filter_cutoff = 1000.f; // Hz
		QFactor<float> filter_width = 9.0f;      // Q

		// Mod block
		Frequency<float> lfo_rate = 0.5f;
		Percentage<float> lfo_filter_depth = 0.f;
		// float lfo_time_depth = 0.f;

		bool ping_pong = false;

		Percentage<float> dry_wet = 0.5f;
	};

	void setConfig(const Config& config) { config_ = config; }
	void processBlock(std::span<q31_t> buffer) {
		interpolated::Parameter feedback{old_config_.feedback.value, config_.feedback.value, buffer.size()};

		interpolated::Parameter filter_cutoff{old_config_.filter_cutoff.value, config_.filter_cutoff.value,
		                                      buffer.size()};
		interpolated::Parameter filter_width{old_config_.filter_width.value, config_.filter_width.value, buffer.size()};

		interpolated::Parameter lfo_rate{old_config_.lfo_rate.value, config_.lfo_rate.value, buffer.size()};
		interpolated::Parameter lfo_filter_depth{old_config_.lfo_filter_depth.value, config_.lfo_filter_depth.value,
		                                         buffer.size()};
		// interpolated::Parameter lfo_time_depth{old_config_.lfo_time_depth, new_config_.lfo_time_depth,
		// buffer.size()};

		interpolated::Parameter dry_wet{old_config_.dry_wet.value, config_.dry_wet.value, buffer.size()};

		// l channel config has changed, so swap
		if (old_config_.l_channel != config_.l_channel) {
			auto& old_buffer = bufferLeft();
			swapBufferLeft();
			auto& new_buffer = bufferLeft();

			new_buffer.set_size(config_.l_channel.duration                                                    // main
			                    + static_cast<size_t>(config_.l_channel.duration * config_.l_channel.nudge)); // nudge

			if (mode_ == Mode::Repitch) {
				new_buffer.RepitchCopyFrom(old_buffer);
			}
			else if (mode_ == Mode::Fade) {
				new_buffer.CopyFrom(old_buffer);
				new_buffer.ApplyGainRamp(blocks::GainRamp{1.f, 0.f});
			}
			// Jump discards delay state
		}

		if (old_config_.r_channel != config_.r_channel) {
			// r channel config has changed, so swap
			auto& old_buffer = bufferRight();
			swapBufferRight();
			auto& new_buffer = bufferRight();

			new_buffer.set_size(config_.r_channel.duration                                                    // main
			                    + static_cast<size_t>(config_.r_channel.duration * config_.r_channel.nudge)); // nudge

			if (mode_ == Mode::Repitch) {
				new_buffer.RepitchCopyFrom(old_buffer);
			}
			else if (mode_ == Mode::Fade) {
				new_buffer.CopyFrom(old_buffer);
				new_buffer.ApplyGainRamp(blocks::GainRamp{1.f, 0.f});
			}
			// Jump discards delay state
		}

		// when the buffer is frozen, no writing is peformed, only advancement of the rec/play head
		if (config_.freeze) {
			for (auto& [left, right] : argon::vectorize_interleaved<2, int32_t>(buffer)) {
				left = bufferLeft().ReadSIMD().ConvertTo<q31_t, 31>(); // To Q31
				bufferLeft().Advance(4);

				right = bufferRight().ReadSIMD().ConvertTo<q31_t, 31>(); // To Q31
				bufferRight().Advance(4);
			}
			return;
		}

		// Otherwise...
		for (auto& [left, right] : argon::vectorize_interleaved<2, int32_t>(buffer)) {
			feedback.Next();
			filter_cutoff.Next();
			filter_width.Next();
			lfo_rate.Next();
			lfo_filter_depth.Next();
			dry_wet.Next();
		}
		old_config_ = config_;
	}

	void process(std::array<Argon<int32_t>, 2> samples) {}

	std::pair<Buffer<kNumSamplesMax>&, Buffer<kNumSamplesMax>&> buffers() { return {bufferLeft(), bufferRight()}; }

	Buffer<kNumSamplesMax>& bufferLeft() { return l_buffers_[l_buffer_idx_]; }

	Buffer<kNumSamplesMax>& bufferRight() { return l_buffers_[l_buffer_idx_]; }

private:
	void swapBufferLeft() { l_buffer_idx_ = !l_buffer_idx_; }
	void swapBufferRight() { r_buffer_idx_ = !r_buffer_idx_; }

	constexpr size_t calcBufferSize(size_t time_ms) {
		return (time_ms * kSampleRate) / 1000; // divide by 1000 due to ms (i.e. * 0.001)
	}

	std::array<Buffer<kNumSamplesMax>, 2> l_buffers_; // 3D array.
	std::array<Buffer<kNumSamplesMax>, 2> r_buffers_; // 3D array.

	size_t l_buffer_idx_ = 0; // Which buffer is currently selected. This is used for
	size_t r_buffer_idx_ = 0; // when the delay time changes and how to handle it depending on mode

	// config
	Config old_config_;
	Config config_;

	Mode mode_ = Mode::Repitch;
};
} // namespace deluge::dsp::delay::simple
