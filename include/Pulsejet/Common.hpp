#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdint>

namespace Pulsejet::Internal
{
	using namespace Shims;

	static const char *SampleTag = "PLSJ";

	inline constexpr uint16_t CodecVersionMajor = 0;
	inline constexpr uint16_t CodecVersionMinor = 1;

	inline constexpr uint32_t FrameSize = 1024;
	inline constexpr uint32_t NumShortWindowsPerFrame = 8;
	inline constexpr uint32_t LongWindowSize = FrameSize * 2;
	inline constexpr uint32_t ShortWindowSize = LongWindowSize / NumShortWindowsPerFrame;

	inline constexpr uint32_t NumBands = 20;
	inline constexpr uint32_t NumTotalBins = 856;

	enum class WindowMode {
		Long = 0,
		Short = 1,
		Start = 2,
		Stop = 3,
	};

	static const uint8_t BandToNumBins[NumBands] =
	{
		8, 8, 8, 8, 8, 8, 8, 8, 16, 16, 24, 32, 32, 40, 48, 64, 80, 120, 144, 176,
	};

	static float VorbisWindow(const float nPlusHalf, const uint32_t size)
	{
		const auto sineWindow = SinF(static_cast<float>(M_PI) / static_cast<float>(size) * nPlusHalf);
		return SinF(static_cast<float>(M_PI_2) * sineWindow * sineWindow);
	}

	inline float MdctWindow(const uint32_t n, const uint32_t size, const WindowMode mode)
	{
		const auto nPlusHalf = static_cast<float>(n) + 0.5f;
		if (mode == WindowMode::Start)
		{
			const auto shortWindowOffset = LongWindowSize * 3 / 4 - ShortWindowSize / 4;
			if (n >= shortWindowOffset + ShortWindowSize / 2)
			{
				return 0.0f;
			}
			else if (n >= shortWindowOffset)
			{
				return 1.0f - VorbisWindow(nPlusHalf - static_cast<float>(shortWindowOffset), ShortWindowSize);
			}
			else if (n >= LongWindowSize / 2)
			{
				return 1.0f;
			}
		}
		else if (mode == WindowMode::Stop)
		{
			const auto shortWindowOffset = LongWindowSize / 4 - ShortWindowSize / 4;
			if (n < shortWindowOffset)
			{
				return 0.0f;
			}
			else if (n < shortWindowOffset + ShortWindowSize / 2)
			{
				return VorbisWindow(nPlusHalf - static_cast<float>(shortWindowOffset), ShortWindowSize);
			}
			else if (n < LongWindowSize / 2)
			{
				return 1.0f;
			}
		}
		return VorbisWindow(nPlusHalf, size);
	}
}
