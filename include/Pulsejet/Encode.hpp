#pragma once

#include "Common.hpp"
#include "EncodeHelpers.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <vector>

namespace Pulsejet
{
	using namespace Internal;
	using namespace Shims;

	using namespace std;

	/**
	 * Encodes a raw sample stream into a newly-allocated vector.
	 *
	 * Like `Decode`, this function expects `CosF` and `SinF` to be defined
	 * by the user in the `Pulsejet::Shims` namespace before including the
	 * relevant pulsejet header(s). See the documentation for `Decode` for
	 * more information.
	 *
	 * @param sampleStream Input sample stream.
	 * @param sampleStreamSize Input sample stream size in samples.
	 * @param sampleRate Input sample rate in samples per second (hz).
	 *        pulsejet is designed for 44100hz samples only, and its
	 *        psychoacoustics are tuned to that rate. However, other rates
	 *        may do something useful/interesting, so this rate is not
	 *        enforced, and the encoder will happily try to match a target
	 *        bit rate at another sample rate if desired.
	 * @param targetBitRate Target bit rate in kilobits per second (kbps).
	 *        There's no enforced lower/upper bound, but due to codec format
	 *        details, the resulting bit rate will often plateau around
	 *        128kbps (or lower, depending on the material). ~64kbps is
	 *        typically transparent, ~32-64kbps is typically high quality.
	 *        For anything lower, it depends on the material, but it's not
	 *        uncommon for rates <=16kbps to actually be useful. <=0kbps
	 *        will usually end up around 2-3kbps.
	 * @param[out] outTotalBitsEstimate Total bits estimate for the
	 *             encoded sample. This will typically differ slightly
	 *             from the actual size after compression, but on average
	 *             is accurate enough to be useful.
	 * @return Encoded sample stream.
	 */
	static vector<uint8_t> Encode(const float *sampleStream, const uint32_t sampleStreamSize, const double sampleRate, const double targetBitRate, double& outTotalBitsEstimate)
	{
		vector<uint8_t> v;

		// Determine target bits/frame
		const auto targetBitsPerFrame = targetBitRate * 1000.0 * (static_cast<double>(FrameSize) / sampleRate);

		// Write out tag+version number
		WriteCString(v, SampleTag);
		WriteU16LE(v, CodecVersionMajor);
		WriteU16LE(v, CodecVersionMinor);

		// Determine and output number of frames
		auto numFrames = (sampleStreamSize + FrameSize - 1) / FrameSize;
		WriteU16LE(v, static_cast<uint16_t>(numFrames));

		// We're going to decode one more frame than we output, so adjust the frame count
		numFrames++;

		// Allocate internal sample buffer including padding, fill it with silence, and copy input data into it
		const auto numSamples = numFrames * FrameSize;
		const auto numPaddedSamples = numSamples + FrameSize * 2;
		vector<float> paddedSamples(numPaddedSamples, 0.0f);
		memcpy(paddedSamples.data() + FrameSize, sampleStream, sampleStreamSize * sizeof(float));

		// Fill padding regions with mirrored frames from the original sample
		for (uint32_t i = 0; i < FrameSize; i++)
		{
			// Head padding
			paddedSamples[FrameSize - 1 - i] = paddedSamples[FrameSize + i];
			// Tail padding
			paddedSamples[numPaddedSamples - FrameSize + i] = paddedSamples[numPaddedSamples - FrameSize - 1 - i];
		}

		// Allocate separate streams to group correlated data
		vector<uint8_t> windowModeStream, bandEnergyStream, binQStream;

		// Clear quantized band energy predictions
		vector<uint8_t> quantizedBandEnergyPredictions(NumBands, 0);

		// Clear slack bits
		double slackBits = 0.0;

		// Clear total bits estimate
		outTotalBitsEstimate = 0.0;

		// Build transient frame map
		vector<bool> isTransientFrameMap;
		float lastFrameEnergy = 0.0f;
		for (uint32_t frameIndex = 0; frameIndex < numFrames; frameIndex++)
		{
			// Conceptually, frames are centered around the center of each long window
			const auto frameOffset = FrameSize / 2 + frameIndex * FrameSize;
			float frameEnergy = 0.0f;
			for (uint32_t i = 0; i < FrameSize; i++)
			{
				const auto sample = paddedSamples[frameOffset + i];
				frameEnergy += sample * sample;
			}
			isTransientFrameMap.push_back(frameEnergy >= lastFrameEnergy * 2.0f);
			lastFrameEnergy = frameEnergy;
		}

		// Encode frames
		for (uint32_t frameIndex = 0; frameIndex < numFrames; frameIndex++)
		{
			// Determine and output window mode for this frame
			const auto isTransientFrame = isTransientFrameMap[frameIndex];
			WindowMode windowMode = WindowMode::Long;
			if (targetBitRate > 8.0)
			{
				const auto isPrevFrameTransientFrame = frameIndex > 0 && isTransientFrameMap[frameIndex - 1];
				const auto isNextFrameTransientFrame = frameIndex < numFrames - 1 && isTransientFrameMap[frameIndex + 1];
				if (isTransientFrame || (isPrevFrameTransientFrame && isNextFrameTransientFrame))
				{
					windowMode = WindowMode::Short;
				}
				else if (isNextFrameTransientFrame)
				{
					windowMode = WindowMode::Start;
				}
				else if (isPrevFrameTransientFrame)
				{
					windowMode = WindowMode::Stop;
				}
			}
			windowModeStream.push_back(static_cast<uint8_t>(windowMode));

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
			const auto subframeSize = subframeWindowSize / 2;

			const auto targetBitsPerSubframe = targetBitsPerFrame / static_cast<double>(numSubframes);

			// Encode subframe(s)
			for (uint32_t subframeIndex = 0; subframeIndex < numSubframes; subframeIndex++)
			{
				vector<float> windowBins;
				windowBins.reserve(subframeSize);
				{
					// Apply window
					const auto frameOffset = frameIndex * FrameSize;
					const auto windowOffset = subframeWindowOffset + subframeIndex * subframeSize;
					vector<float> windowedSamples;
					windowedSamples.reserve(subframeWindowSize);
					for (uint32_t n = 0; n < subframeWindowSize; n++)
					{
						const auto sample = paddedSamples[frameOffset + windowOffset + n];
						const auto window = MdctWindow(n, subframeWindowSize, windowMode);
						windowedSamples.push_back(sample * window);
					}

					// Perform MDCT
					for (uint32_t k = 0; k < subframeSize; k++)
					{
						float bin = 0.0f;
						for (uint32_t n = 0; n < subframeWindowSize; n++)
							bin += windowedSamples[n] * CosF(static_cast<float>(M_PI) / static_cast<float>(subframeSize) * (static_cast<float>(n) + 0.5f + static_cast<float>(subframeSize / 2)) * (static_cast<float>(k) + 0.5f));
						windowBins.push_back(bin);
					}
				}

				// Search (exhaustively) for an appropriate bin quantization scaling factor
				vector<uint8_t> bestQuantizedBandEnergies;
				vector<uint8_t> bestBandEnergyStream;
				vector<int8_t> bestBinQStream;
				double bestSubframeBitsEstimate = 0.0;

				const uint32_t minScalingFactor = 1;
				const uint32_t maxScalingFactor = 500;
				for (uint32_t scalingFactor = minScalingFactor; scalingFactor <= maxScalingFactor; scalingFactor++)
				{
					vector<uint8_t> candidateQuantizedBandEnergies;
					vector<uint8_t> candidateBandEnergyStream;
					candidateQuantizedBandEnergies.reserve(NumBands);
					candidateBandEnergyStream.reserve(NumBands);
					map<uint8_t, uint32_t> candidateBandEnergyFreqs;
					vector<int8_t> candidateBinQStream;
					candidateBinQStream.reserve(subframeSize);
					map<int8_t, uint32_t> candidateBinQFreqs;

					// Encode bands
					auto bandBins = windowBins.data();
					for (uint32_t bandIndex = 0; bandIndex < NumBands; bandIndex++)
					{
						const auto numBins = BandToNumBins[bandIndex] / numSubframes;

						// Calculate band energy
						const float epsilon = 1e-27f;
						float bandEnergy = epsilon;
						for (uint32_t binIndex = 0; binIndex < numBins; binIndex++)
						{
							const auto bin = bandBins[binIndex];
							bandEnergy += bin * bin;
						}
						bandEnergy = sqrtf(bandEnergy);

						// Quantize and encode band energy
						const auto linearBandEnergy = (clamp(log2f(bandEnergy / static_cast<float>(numBins)), -20.0f, 20.0f) + 20.0f) / 40.0f;
						const auto quantizedBandEnergy = static_cast<uint8_t>(roundf(linearBandEnergy * 64.0f));
						candidateQuantizedBandEnergies.push_back(quantizedBandEnergy);
						const uint8_t quantizedBandEnergyResidual = quantizedBandEnergy - quantizedBandEnergyPredictions[bandIndex];
						candidateBandEnergyStream.push_back(quantizedBandEnergyResidual);
						candidateBandEnergyFreqs.try_emplace(quantizedBandEnergyResidual, 0);
						candidateBandEnergyFreqs.at(quantizedBandEnergyResidual) += 1;

						// Determine band bin quantization scale
						const auto bandBinQuantizeScale = powf(static_cast<float>(BandBinQuantizeScaleBases[bandIndex]) / 200.0f, 3.0f) * static_cast<float>(scalingFactor) / static_cast<float>(maxScalingFactor) * 127.0f * linearBandEnergy * linearBandEnergy;

						// Normalize, quantize, and encode band bins
						for (uint32_t binIndex = 0; binIndex < numBins; binIndex++)
						{
							const auto bin = bandBins[binIndex];
							const auto binQ = static_cast<int8_t>(roundf(bin / (bandEnergy + epsilon) * bandBinQuantizeScale));
							candidateBinQStream.push_back(binQ);
							candidateBinQFreqs.try_emplace(binQ, 0);
							candidateBinQFreqs.at(binQ) += 1;
						}

						bandBins += numBins;
					}

					// Model the order 0 entropy of the quantized stream symbols in order to estimate the total bits used for encoding
					//  Also adjust estimate slightly, as squishy (and likely other compressors) tend to find additional correlations not captured by this simple model
					const auto bandEnergyBitsEstimate = Order0BitsEstimate(candidateBandEnergyFreqs);
					const auto binQBitsEstimate = Order0BitsEstimate(candidateBinQFreqs);
					const double estimateAdjustment = 0.83;
					const auto subframeBitsEstimate = (bandEnergyBitsEstimate + binQBitsEstimate) * estimateAdjustment;

					// Accept these candidate streams if this bit count estimate is closest to the target for the subframe
					const auto targetBitsPerSubframeWithSlackBits = targetBitsPerSubframe + slackBits;
					if (scalingFactor == minScalingFactor || abs(subframeBitsEstimate - targetBitsPerSubframeWithSlackBits) < abs(bestSubframeBitsEstimate - targetBitsPerSubframeWithSlackBits))
					{
						bestQuantizedBandEnergies = candidateQuantizedBandEnergies;
						bestBandEnergyStream = candidateBandEnergyStream;
						bestBinQStream = candidateBinQStream;
						bestSubframeBitsEstimate = subframeBitsEstimate;
					}
				}

				// Update quantized band energy predictions for next subframe
				quantizedBandEnergyPredictions = bestQuantizedBandEnergies;

				// Output the best-performing parameters/coefficients to their respective streams
				move(bestBandEnergyStream.begin(), bestBandEnergyStream.end(), back_inserter(bandEnergyStream));
				move(bestBinQStream.begin(), bestBinQStream.end(), back_inserter(binQStream));

				// Adjust slack bits depending on our estimated bits used for this subframe
				slackBits += targetBitsPerSubframe - bestSubframeBitsEstimate;

				// Update total bits estimate
				outTotalBitsEstimate += bestSubframeBitsEstimate;
			}
		}

		// Concatenate streams
		move(windowModeStream.begin(), windowModeStream.end(), back_inserter(v));
		move(binQStream.begin(), binQStream.end(), back_inserter(v));
		move(bandEnergyStream.begin(), bandEnergyStream.end(), back_inserter(v));

		return v;
	}
}
