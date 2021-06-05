#include "FastSinusoids.hpp"

#include <cstdint>

static inline constexpr uint32_t fastCosTabLog2Size = 10; // size = 1024
static inline constexpr uint32_t fastCosTabSize = 1 << fastCosTabLog2Size;
static double fastCosTab[fastCosTabSize + 1];

namespace FastSinusoids
{
	void Init()
	{
		for (uint32_t i = 0; i < fastCosTabSize + 1; i++)
		{
			const auto phase = static_cast<double>(i) * M_PI * 2.0 / static_cast<double>(fastCosTabSize);
			fastCosTab[i] = cos(phase);
		}
	}

	double Cos(double x)
	{
		x = fabs(x); // cosine is symmetrical around 0, let's get rid of negative values

		// normalize range from 0..2PI to 1..2
		const auto phaseScale = 1.0 / (M_PI * 2.0);
		const auto phase = 1.0 + x * phaseScale;

		const auto phaseAsInt = *reinterpret_cast<const uint64_t *>(&phase);
		const auto exponent = static_cast<int32_t>(phaseAsInt >> 52) - 1023;

		const auto fractBits = 32 - fastCosTabLog2Size;
		const auto fractScale = 1 << fractBits;
		const auto fractMask = fractScale - 1;

		const auto significand = static_cast<uint32_t>((phaseAsInt << exponent) >> (52 - 32));
		const auto index = significand >> fractBits;
		const auto fract = significand & fractMask;

		const auto left = fastCosTab[index];
		const auto right = fastCosTab[index + 1];

		const auto fractMix = fract * (1.0 / fractScale);
		return left + (right - left) * fractMix;
	}
}
