#pragma once

#include "Common.hpp"

#include <cstdint>
#include <string>

namespace Pulsejet::Internal
{
	using namespace std;

	static const char *VersionPrefix = "pulsejet v";

	inline string VersionStringInternal(uint16_t versionMajor, uint16_t versionMinor)
	{
		return VersionPrefix + to_string(versionMajor) + "." + to_string(versionMinor);
	}
}
