#pragma once

#include "Common.hpp"
#include "MetaHelpers.hpp"

#include <cstdint>
#include <string>

namespace Pulsejet
{
	using namespace Internal;

	using namespace std;

	/**
	 * Returns a string that represents this pulsejet library version.
	 *
	 * This version follows semver (https://semver.org).
	 *
	 * @return a string that represents this pulsejet library version.
	 */
	inline string LibraryVersionString()
	{
		return string(VersionPrefix) + "0.1.0";
	}

	/**
	 * Returns a string that represents the pulsejet codec version supported by
	 * this library.
	 *
	 * Encoded pulsejet samples begin with a header which includes this major/minor
	 * version pair.
	 *
	 * This version follows a custom scheme with both a major and a minor
	 * version. The major version is used to determine encoder/decoder compatibility.
	 * Attempting to decode a sample containing a major version that does not match
	 * that of the decoder library results in undefined behavior. Minor versions,
	 * however, represent codec changes that do not affect the decoder implementation,
	 * and are thus compatible. This can be used by tooling to, for example, determine
	 * if an encoded sample could be re-encoded by a newer encoder to achieve
	 * (ideally) higher sample quality with the same decoder. While the header
	 * format is currently opaque and subject to change, the `CheckSampleVersion`
	 * function can be used to determine if a given library and sample have compatible
	 * codec versions.
	 *
	 * @return A string that represents the pulsejet codec version supported by
	 *         this library.
	 */
	inline string CodecVersionString()
	{
		return VersionStringInternal(CodecVersionMajor, CodecVersionMinor);
	}

	/**
	 * Returns a string that represents the pulsejet codec version included in an
	 * encoded sample stream.
	 *
	 * Encoded pulsejet samples begin with a header which includes this major/minor
	 * version pair. See `CodecVersionString` for more info.
	 *
	 * @return A string that represents the pulsejet codec version included in an
	 *         encoded sample stream.
	 */
	inline string SampleVersionString(const uint8_t *inputStream)
	{
		const auto versionMajor = reinterpret_cast<const uint16_t *>(inputStream)[2];
		const auto versionMinor = reinterpret_cast<const uint16_t *>(inputStream)[3];
		return VersionStringInternal(versionMajor, versionMinor);
	}

	/**
	 * Checks to see if the given stream represents a pulsejet sample.
	 *
	 * Currently, only part of the encoded sample header is checked, and behavior
	 * is undefined if the given stream is not actually large enough to include
	 * this data.
	 *
	 * @param inputStream Encoded pulsejet byte stream (hopefully).
	 * @return Whether or not the given stream represents a pulsejet sample.
	 */
	inline bool CheckSample(const uint8_t *inputStream)
	{
		return !strcmp(reinterpret_cast<const char *>(inputStream), SampleTag);
	}

	/**
	 * Determines if this library and the given encoded pulsejet byte stream have
	 * compatible codec versions.
	 *
	 * This function assumes that `inputStream` represents an encoded pulsejet
	 * byte stream. `CheckSample` can be used to verify this assumption.
	 *
	 * See `CodecVersionString` for more info.
	 *
	 * @param inputStream Encoded pulsejet byte stream.
	 * @return Whether or not this library and the given encoded pulsejet byte
	 *         stream have compatible codec versions.
	 */
	inline bool CheckSampleVersion(const uint8_t *inputStream)
	{
		const auto versionMajor = reinterpret_cast<const uint16_t *>(inputStream)[2];
		return versionMajor == CodecVersionMajor;
	}
}
