#pragma once

#include "coefficients.hpp"
#include "deluge/dsp/stereo_sample.h"
#include <argon.hpp>
#include <array>
#include <span>

namespace deluge::dsp::blocks::filters::iir {
template <size_t num_stages>
class BiquadDF2TStereo {
	/// @brief store the Coefficients interleaved (it makes for much faster load)
	struct StereoCoefficients {
		float l_b0;
		float r_b0;
		float l_b1;
		float r_b1;
		float l_b2;
		float r_b2;
		float l_a1;
		float r_a1;
		float l_a2;
		float r_a2;
	};

	using State = std::array<float, 4>;

public:
	BiquadDF2TStereo() = default;

	void setCoefficients(size_t stage, Coefficients<float> lr_coefficients) {
		coefficients_[stage] = StereoCoefficients{
		    .l_b0 = lr_coefficients.b0,
		    .r_b0 = lr_coefficients.b0,
		    .l_b1 = lr_coefficients.b1,
		    .r_b1 = lr_coefficients.b1,
		    .l_b2 = lr_coefficients.b2,
		    .r_b2 = lr_coefficients.b2,
		    .l_a1 = -lr_coefficients.a1, // Sign flipped
		    .r_a1 = -lr_coefficients.a1, // Sign flipped
		    .l_a2 = -lr_coefficients.a2, // Sign flipped
		    .r_a2 = -lr_coefficients.a2, // Sign flipped
		};
	}

	void setCoefficients(size_t stage, Coefficients<float> l_coefficients, Coefficients<float> r_coefficients) {
		coefficients_[stage] = StereoCoefficients{
		    .l_b0 = l_coefficients.b0,
		    .r_b0 = r_coefficients.b0,
		    .l_b1 = l_coefficients.b1,
		    .r_b1 = r_coefficients.b1,
		    .l_b2 = l_coefficients.b2,
		    .r_b2 = r_coefficients.b2,
		    .l_a1 = -l_coefficients.a1, // Sign flipped
		    .r_a1 = -r_coefficients.a1, // Sign flipped
		    .l_a2 = -l_coefficients.a2, // Sign flipped
		    .r_a2 = -r_coefficients.a2, // Sign flipped
		};
	}

	void setCoefficients(Coefficients<float> lr_coefficients) {
		for (size_t i = 0; i < num_stages; ++i) {
			setCoefficients(i, lr_coefficients);
		}
	}

	void setCoefficients(Coefficients<float> l_coefficients, Coefficients<float> r_coefficients) {
		for (size_t i = 0; i < num_stages; ++i) {
			setCoefficients(i, l_coefficients, r_coefficients);
		}
	}

	void processStereoBlock(const std::span<StereoFloatSample> in, std::span<StereoFloatSample> out) {
		using namespace argon;
		std::array<float, 6> scratch; // layout : [s1a s1b s2a s2b 0 0]

		auto it = in.begin();
		for (size_t stage = 0; stage < num_stages; ++stage) {
			auto b_coeffs = Neon128<float>::Load(&coefficients_[stage].l_b1); // b1a b1b b2a b2b
			auto a_coeffs = Neon128<float>::Load(&coefficients_[stage].l_a1); // a1a a1b a2a a2b
			auto b0_coeff = Neon64<float>::Load(&coefficients_[stage].l_b0);  // b0a b0b

			// read state into scratch
			*(float32x4_t*)scratch.data() = *(float32x4_t*)states_[stage].data();

			Neon128<float> state;

			for (size_t i = 0; i < out.size(); ++i) {
				/*
				 * General form is given by
				 * y(n)       = b_0 * x(n) + s_1(n)
				 * s_1(n + 1) = b_1 * x(n) + a_1 * y(n) + s_2(n)
				 * s_2(n + 1) = b_2 * x(n) + a_2 * y(n)
				 *
				 * where x(n) is the input sample
				 *       y(n) is the output sample
				 *
				 * NOTE!! SIGNS ARE INVERTED FOR a_1 AND a_2 FROM THE STANDARD DIFFERENCE EQUATION
				 */

				/*
				 * step 1
				 *
				 * 0   | ya = xa * b0a + s1a    // left
				 * 1   | yb = xb * b0b + s1b    // right
				 */

				auto y = Neon64<float>::Load(&scratch[0]);  // load {s1a s1b}
				auto input = Neon64<float>::Load(&it[i].l); // load {l r}

				y = y.MultiplyAdd(input, b0_coeff);
				y.Store(&out[i].l);

				/*
				 * step 2
				 *
				 * 0  | s1a = b1a * xa  +  a1a * ya  +  s2a  // left
				 * 1  | s1b = b1b * xb  +  a1b * yb  +  s2b  // right
				 * 2  | s2a = b2a * xa  +  a2a * ya  +  0    // left
				 * 3  | s2b = b2b * xb  +  a2b * yb  +  0    // right
				 */
				auto y_quad = Neon128<float>{y, y};             // {d1a d1b} . {d1a d1b}
				auto input_quad = Neon128<float>{input, input}; // {l r} . {l r}

				state = Neon128<float>::Load(&scratch[2]); // load {s2a, s2b, 0, 0}
				state = state.MultiplyAdd(y_quad, a_coeffs);
				state = state.MultiplyAdd(input_quad, b_coeffs);
				state.Store(scratch.data());
			}

			// Store the updated state variables into the persistent state storage
			state.Store(states_[stage].data());

			it = out.begin(); // The current stage output is given as the input to the next stage
		}
	}

private:
	std::array<State, num_stages> states_ = {0};
	std::array<StereoCoefficients, num_stages> coefficients_; // array of L-R coefficients
};

} // namespace deluge::dsp::blocks::filters::iir
