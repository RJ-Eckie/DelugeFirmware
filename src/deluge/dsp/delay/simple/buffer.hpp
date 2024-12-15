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
#include "dsp/blocks/gain_ramp.hpp"
#include "dsp/interpolate/interpolate.h"
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::delay::simple {

/**
 * @brief This class is essentially a fractional delay line/FIFO-queue combined with a circular buffer.
 */
template <size_t max_delay>
class Buffer {
public:
	static_assert(Argon<float>::lanes == 4);
	Buffer(size_t size) : size_(size) {}
	~Buffer() = default;

	constexpr void Reset() { idx_ = 0; }
	constexpr void Clear() { raw_buffer_.fill(0); }

	constexpr void Write(size_t index, const float sample, float feedback = 0.f) {
		buffer_[index] = sample + (buffer_[index] * feedback);
	}

	constexpr void WriteSIMD(Argon<float> sample, Argon<float> feedback) {
		// No wraparound, so we can do this with NEON
		if (idx_ < size_ - 4) [[likely]] {
			// reverse so that "oldest" sample is at highest index
			feedback = feedback.Reverse();
			sample = sample.Reverse();

			auto old_sample = Argon<float>::Load(&buffer_[idx_]);
			auto new_sample = sample.MultiplyAdd(old_sample, feedback); // sample + (old * feedback)
			new_sample.StoreTo(&buffer_[idx_]);
			idx_ = 0;
			return;
		}

		// Wraparound, but on the doubleword boundary
		if (idx_ == size_ - 2) {
			// do "low" half (oldest samples)
			ArgonHalf<float> feedback_low = feedback.GetLow().Reverse();
			ArgonHalf<float> sample_low = sample.GetLow().Reverse();
			auto old_sample_low = ArgonHalf<float>::Load(&buffer_[idx_]);
			sample_low.MultiplyAdd(old_sample_low, feedback_low).StoreTo(&buffer_[idx_]);

			// do "high" half (newest samples)
			ArgonHalf<float> feedback_high = sample.GetHigh().Reverse();
			ArgonHalf<float> sample_high = sample.GetHigh().Reverse();
			auto old_sample_high = ArgonHalf<float>::Load(buffer_.data());
			sample_high.MultiplyAdd(old_sample_high, feedback_high).StoreTo(buffer_.data());
			idx_ = 2;
			return;
		}

		// Wraparound case in the middle of a doubleword, need to do each lane indivdually :(
		for (size_t lane = 0; lane < Argon<float>::lanes; ++lane) {
			Write(idx_, sample[lane], feedback[lane]);
			Advance();
		}
	}

	///@brief Advance the Rec/Play heads by \p count samples
	constexpr void Advance(size_t count = 1) {
		// must be signed so that the result can be negative
		idx_ += count;
		if (idx_ >= size_) {
			idx_ = 0;
		}
	}

	[[nodiscard]] constexpr float Read(size_t integral = 0) const { return buffer_[wrap(idx_ + integral)]; }

	[[nodiscard]] constexpr float ReadFractional(float index) const {
		return InterpolateHermiteTable(buffer_, wrap(idx_ + index));
	}

	[[nodiscard]] constexpr Argon<float> ReadSIMD(size_t integral = 0) const {
		auto read_idx = idx_ + integral;

		// can read using quadword without wrap
		if (read_idx < size_ - 4) [[likely]] {
			return Argon<float>::Load(&buffer_[read_idx]);
		}

		// can read using doubleword
		if (read_idx == size_ - 2) [[unlikely]] {
			return Argon<float>{ArgonHalf<float>::Load(&buffer_[size_ - 2]), ArgonHalf<float>::Load(buffer_.data())};
		}

		// basically a LoadGather, without the writebacck
		return Argon<float>::GenerateWithIndex([read_idx, this](uint32_t offset) {
			auto lane_index = read_idx + offset;
			if (lane_index >= size_) {
				lane_index -= size_;
			}
			return buffer_[lane_index];
		});
	}

	[[nodiscard]] constexpr Argon<float> ReadFractionalSIMD(Argon<float> index) const {
		auto index_integral = index.ConvertTo<uint32_t>() + static_cast<uint32_t>(idx_);
		Argon<float> index_fractional = index - index_integral.ConvertTo<float>();

		// Fast wraparound: do a comparison against the length to get a vector of bitmasks, bitwise-and them with the
		// vector of the length so that only the lanes _over_ that length are populated, then subtract that from the
		// original set of indices
		index_integral = index_integral - (Argon<uint32_t>(size_) & (index_integral >= size_));

		return InterpolateHermiteTableSIMD<float>(buffer_, index_integral, index_fractional);
	}

	///@brief Prepares the buffer for fractional read via Hermite interpolation without needing to wrap the indices
	void PrepForInterpolate() {
		buffer_[-1] = buffer_[size_ - 1];
		buffer_[size_] = buffer_[0];
		buffer_[size_ + 1] = buffer_[1];
	}

	template <size_t othersize>
	void RepitchCopyFrom(Buffer<othersize>& origin) {
		if (size() == origin.size()) [[unlikely]] {
			const size_t read_pos = origin.pos();
			if (read_pos == 0) {
				std::copy(origin.buffer_.begin(), origin.buffer_.end(), buffer_.begin());
				return;
			}

			// copy over in two halves: pos to end, start to pos;
			auto first_half_size = origin.size() - read_pos;
			std::copy(&origin.buffer_[read_pos], &origin.buffer_[origin.size()], buffer_.data());
			std::copy(&origin.buffer_[0], &origin.buffer_[read_pos], &buffer_[first_half_size]);
			return;
		}

		// prep the other for fractional read
		origin.PrepForInterpolate();

		// repitch copy via Hermite interpolation
		const float step = origin.size() / size_; // ratio
		const Argon<float> step_simd = step * 4;

		// main loop
		Argon<float> pos = Argon{step} * Argon{0.f, 1.f, 2.f, 3.f};
		for (size_t idx = 0; idx < (size_ & ~0b11); idx += 4) {
			// You might think that reading the first sample (x = 0) causes a problem,
			// as it's using an invalid sample for the x-1 value. However, because there's
			// no fractional component for the first sample, the interpolation samples are
			// disregarded via multiplication by 0 (the fractional coefficients),
			// leaving us with simply y = x0

			origin.ReadFractionalSIMD(pos).StoreTo(&buffer_[idx]);
			pos = pos + step_simd;
		}

		// tail loop
		float tail_pos = pos[1]; // same as pos[0] + step
		for (size_t idx = (size_ & ~0b11); idx < size_; ++idx) {
			buffer_[idx] = origin.ReadFractional(tail_pos);
			tail_pos += step;
		}
	}

	/**
	 * @brief Copy from one buffer to another, retaining only the most recent samples (discard oldest)
	 */
	template <size_t othersize>
	void CopyFrom(const Buffer<othersize>& origin) {
		// The chunk of newest samples from the current write head to the end, is longer
		// than the new buffer, so we don't need to copy in segments
		if (origin.pos() >= size()) {
			// copies n newest samples to an n-sized buffer
			std::copy(&origin.buffer_[origin.pos() - size()], &origin.buffer_[origin.pos()], buffer_.begin());
			return;
		}

		size_t wrap_size = size() - origin.pos();
		std::copy(&origin.buffer_[origin.size() - wrap_size], &origin.buffer_[origin.size()], buffer_.begin());
		std::copy(&origin.buffer_[0], &origin.buffer_[origin.size()], &buffer_[wrap_size]);
	}

	void ApplyGainRamp(blocks::GainRamp gain_ramp) {
		const float start = gain_ramp.start();
		const float end = gain_ramp.end();

		const float breakpoint = end - ((end - start) * (pos() / size()));

		std::span first_block{buffer_.data(), pos()};
		blocks::GainRamp{breakpoint, start}.processBlock(first_block, first_block);

		std::span second_block{&buffer_[pos()], &buffer_[size()]};
		blocks::GainRamp{end, breakpoint}.processBlock(second_block, second_block);
	}

	[[nodiscard]] constexpr size_t size() const { return size_; }
	constexpr void set_size(size_t size) { size_ = size; }
	[[nodiscard]] constexpr size_t pos() const { return idx_; }

private:
	[[nodiscard]] constexpr size_t wrap(size_t index) const {
		if (index >= size_) {
			return index - size_;
		}
		return index;
	}

	std::array<float, max_delay + 3> raw_buffer_{}; // + 3 for hermite samples (two lookahead, one lookbehind)
	std::span<float> buffer_{&raw_buffer_[1], max_delay};

	size_t size_;

	size_t idx_ = 0;
};
} // namespace deluge::dsp::delay::simple
