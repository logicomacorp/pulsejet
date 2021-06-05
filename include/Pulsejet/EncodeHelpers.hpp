#pragma once

#include "Common.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace Pulsejet::Internal
{
	using namespace std;

	static const uint8_t BandBinQuantizeScaleBases[NumBands] =
	{
		200, 200, 200, 200, 200, 200, 200, 200, 198, 193, 188, 183, 178, 173, 168, 163, 158, 153, 148, 129,
	};

	template<typename Key>
	double Order0BitsEstimate(const map<Key, uint32_t>& freqs)
	{
		uint32_t numSymbols = 0;
		for (const auto& kvp : freqs)
		{
			const auto freq = kvp.second;
			numSymbols += freq;
		}
		double bitsEstimate = 0.0;
		for (const auto& kvp : freqs)
		{
			const auto freq = static_cast<double>(kvp.second);
			const auto prob = freq / static_cast<double>(numSymbols);
			bitsEstimate += -log2(prob) * freq;
		}
		return bitsEstimate;
	}

	static void WriteCString(vector<uint8_t>& v, const char *s)
	{
		while (true)
		{
			const char c = *s++;
			if (!c)
				break;
			v.push_back(static_cast<uint8_t>(c));
		}
	}

	static void WriteU16LE(vector<uint8_t>& v, uint16_t value)
	{
		v.push_back(static_cast<uint8_t>(value >> 0));
		v.push_back(static_cast<uint8_t>(value >> 8));
	}
}
