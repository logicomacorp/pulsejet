#pragma once

#include "Common.hpp"

#include <cstdint>
#include <cstring>

namespace Pulsejet
{
	using namespace Internal;
	using namespace Shims;

	/**
	 * Decodes an encoded pulsejet sample into a newly-allocated buffer.
	 *
	 * This function is optimized for size and designed to be compiled in a
	 * size-constrained environment. In such environments, it's common not
	 * to have access to all of the required math functions, and instead
	 * implement them by hand. For this reason, this decoder does not
	 * depend on any such functions directly, and instead expects that
	 * `CosF`, `Exp2F`, `SinF`, and `SqrtF` functions are defined in the
	 * `Pulsejet::Shims` namespace before including relevant pulsejet
	 * header(s). pulsejet expects that these functions behave similarly
	 * to the corresponding similarly-named cmath functions. This shim
	 * mechanism can also be used to provide less accurate, speed-optimized
	 * versions of these functions if desired.
	 *
	 * Additionally, this function will not perform any error checking or
	 * handling. The included metadata API can be used for high-level error
	 * checking before decoding takes place if required (albeit not in a
	 * non-size-constrained environment).
	 *
	 * @param inputStream Encoded pulsejet byte stream.
	 * @param[out] outNumSamples Number of decoded samples.
	 * @return Decoded samples in the [-1, 1] range (normalized).
	 *         This buffer is allocated by `new []` and should be freed
	 *         using `delete []`.
	 */
	static float *Decode(const uint8_t *inputStream, uint32_t *outNumSamples)
	{
		// Skip tag and codec version
		inputStream += 8;

		// Read frame count, determine number of samples, and allocate output sample buffer
		auto numFrames = static_cast<uint32_t>(*(reinterpret_cast<const uint16_t *>(inputStream)));
		inputStream += sizeof(uint16_t);
		const auto numSamples = numFrames * FrameSize;
		*outNumSamples = numSamples;
		const auto samples = new float[numSamples];

		// We're going to decode one more frame than we output, so adjust the frame count
		numFrames++;

		// Set up and skip window mode stream
		auto windowModeStream = inputStream;
		inputStream += numFrames;

		// Set up and skip quantized band bin stream
		auto quantizedBandBinStream = reinterpret_cast<const int8_t *>(inputStream);
		inputStream += numFrames * NumTotalBins;

		// Allocate padded sample buffer, and fill with silence
		const auto numPaddedSamples = numSamples + FrameSize * 2;
		const auto paddedSamples = new float[numPaddedSamples]();

		// Initialize LCG
		uint32_t lcgState = 0;

		// Clear quantized band energy predictions
		uint8_t quantizedBandEnergyPredictions[NumBands] = {};

		// Decode frames
		for (uint32_t frameIndex = 0; frameIndex < numFrames; frameIndex++)
		{
			// Read window mode for this frame
			const auto windowMode = static_cast<WindowMode>(*windowModeStream++);

			// Determine subframe configuration from window mode
			uint32_t numSubframes = 1;
			uint32_t subframeWindowOffset = 0;
			uint32_t subframeWindowSize = LongWindowSize;
			if (windowMode == WindowMode::Short)
			{
				numSubframes = NumShortWindowsPerFrame;
				subframeWindowOffset = LongWindowSize / 4 - ShortWindowSize / 4;
				subframeWindowSize = ShortWindowSize;
			}

			// Decode subframe(s)
			for (uint32_t subframeIndex = 0; subframeIndex < numSubframes; subframeIndex++)
			{
				// Decode bands
				float windowBins[FrameSize] = {};
				auto bandBins = windowBins;
				for (uint32_t bandIndex = 0; bandIndex < NumBands; bandIndex++)
				{
					// Decode band bins
					const auto numBins = BandToNumBins[bandIndex] / numSubframes;
					uint32_t numNonzeroBins = 0;
					for (uint32_t binIndex = 0; binIndex < numBins; binIndex++)
					{
						const auto binQ = *quantizedBandBinStream++;
						if (binQ)
							numNonzeroBins++;
						const auto bin = static_cast<float>(binQ);
						bandBins[binIndex] = bin;
					}

					// If this band is significantly sparse, fill in (nearly) spectrally flat noise
					const auto binFill = static_cast<float>(numNonzeroBins) / static_cast<float>(numBins);
					const auto noiseFillThreshold = 0.1f;
					if (binFill < noiseFillThreshold)
					{
						const auto binSparsity = (noiseFillThreshold - binFill) / noiseFillThreshold;
						const auto noiseFillGain = binSparsity * binSparsity;
						for (uint32_t binIndex = 0; binIndex < numBins; binIndex++)
						{
							const auto noiseSample = static_cast<float>(static_cast<int8_t>(lcgState >> 16)) / 127.0f;
							bandBins[binIndex] += noiseSample * noiseFillGain;

							// Transition LCG state using Numerical Recipes parameters
							lcgState = lcgState * 1664525 + 1013904223;
						}
					}

					// Decode band energy
					const auto quantizedBandEnergyResidual = *inputStream++;
					const uint8_t quantizedBandEnergy = quantizedBandEnergyPredictions[bandIndex] + quantizedBandEnergyResidual;
					quantizedBandEnergyPredictions[bandIndex] = quantizedBandEnergy;
					const auto bandEnergy = Exp2f(static_cast<float>(quantizedBandEnergy) / 64.0f * 40.0f - 20.0f) * static_cast<float>(numBins);

					// Normalize band bins and scale by band energy
					const float epsilon = 1e-27f;
					auto bandBinEnergy = epsilon;
					for (uint32_t binIndex = 0; binIndex < numBins; binIndex++)
					{
						const auto bin = bandBins[binIndex];
						bandBinEnergy += bin * bin;
					}
					bandBinEnergy = SqrtF(bandBinEnergy);
					const auto binScale = bandEnergy / bandBinEnergy;
					for (uint32_t binIndex = 0; binIndex < numBins; binIndex++)
						bandBins[binIndex] *= binScale;

					bandBins += numBins;
				}

				// Apply the IMDCT to the subframe bins, then apply the appropriate window to the resulting samples, and finally accumulate them into the padded output buffer
				const auto frameOffset = frameIndex * FrameSize;
				const auto windowOffset = subframeWindowOffset + subframeIndex * subframeWindowSize / 2;
				for (uint32_t n = 0; n < subframeWindowSize; n++)
				{
					const auto nPlusHalf = static_cast<float>(n) + 0.5f;

					auto sample = 0.0f;
					for (uint32_t k = 0; k < subframeWindowSize / 2; k++)
						sample += (2.0f / static_cast<float>(subframeWindowSize / 2)) * windowBins[k] * CosF(static_cast<float>(M_PI) / static_cast<float>(subframeWindowSize / 2) * (nPlusHalf + static_cast<float>(subframeWindowSize / 4)) * (static_cast<float>(k) + 0.5f));

					auto window = MdctWindow(n, subframeWindowSize, windowMode);
					paddedSamples[frameOffset + windowOffset + n] += sample * window;
				}
			}
		}

		// Copy samples without padding to the output buffer
		memcpy(samples, paddedSamples + FrameSize, numSamples * sizeof(float));

		// Free padded sample buffer
		delete [] paddedSamples;

		return samples;
	}
}
