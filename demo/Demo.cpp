#include "FastSinusoids.hpp"

// Required by `Pulsejet::Encode` and `Pulsejet::Decode`
namespace Pulsejet::Shims
{
	inline float CosF(float x)
	{
		return FastSinusoids::CosF(x);
	}

	inline float Exp2f(float x)
	{
		return exp2f(x);
	}

	inline float SinF(float x)
	{
		return FastSinusoids::SinF(x);
	}

	inline float SqrtF(float x)
	{
		return sqrtf(x);
	}
}
#include <Pulsejet/Pulsejet.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace std;

static void PrintUsage(const char **argv)
{
	cout << "Usage:\n";
	cout << "  encode: " << argv[0] << " -e <target bit rate in kbps> <input.raw> <output.pulsejet>\n";
	cout << "  decode: " << argv[0] << " -d <input.pulsejet> <output.raw>\n";
}

static void ErrorInvalidArgs(const char **argv)
{
	cout << "ERROR: Invalid args\n\n";
	PrintUsage(argv);
}

static vector<uint8_t> ReadFile(const char *fileName)
{
	ifstream inputFile(fileName, ios::binary | ios::ate);
	const auto inputFileSize = inputFile.tellg();
	inputFile.seekg(0, ios::beg);

	vector<uint8_t> ret(inputFileSize);
	inputFile.read(reinterpret_cast<char *>(ret.data()), inputFileSize);

	return ret;
}

int main(int argc, const char **argv)
{
	if (argc < 4)
	{
		ErrorInvalidArgs(argv);
		return 1;
	}

	cout << "library version: " << Pulsejet::LibraryVersionString() << "\n";
	cout << "codec version: " << Pulsejet::CodecVersionString() << "\n";

	FastSinusoids::Init();

	if (!strcmp(argv[1], "-e"))
	{
		if (argc != 5)
		{
			ErrorInvalidArgs(argv);
			return 1;
		}

		const double targetBitRate = stod(argv[2]);
		const auto inputFileName = argv[3];
		const auto outputFileName = argv[4];

		cout << "reading ... " << flush;
		const auto input = ReadFile(inputFileName);
		cout << "ok\n";

		cout << "size check ... " << flush;
		if (input.size() % sizeof(float))
		{
			cout << "ERROR: Input size is not aligned to float size\n\n";
			return 1;
		}
		cout << "ok\n";

		cout << "encoding ... " << flush;
		const uint32_t numSamples = static_cast<uint32_t>(input.size()) / sizeof(float);
		const double sampleRate = 44100.0;
		double totalBitsEstimate;
		const auto encodedSample = Pulsejet::Encode(reinterpret_cast<const float *>(input.data()), numSamples, sampleRate, targetBitRate, totalBitsEstimate);
		const auto bitRateEstimate = totalBitsEstimate / 1000.0 / (static_cast<double>(numSamples) / sampleRate);
		cout << "ok, compressed size estimate: " << static_cast<uint32_t>(ceil(totalBitsEstimate / 8.0)) << " byte(s) (~" << setprecision(4) << bitRateEstimate << "kbps)\n";

		cout << "writing ... " << flush;
		ofstream outputFile(outputFileName, ios::binary);
		outputFile.write(reinterpret_cast<const char *>(encodedSample.data()), encodedSample.size());
		cout << "ok\n";

		cout << "encoding successful!\n";
	}
	else if (!strcmp(argv[1], "-d"))
	{
		if (argc != 4)
		{
			ErrorInvalidArgs(argv);
			return 1;
		}

		const auto inputFileName = argv[2];
		const auto outputFileName = argv[3];

		cout << "reading ... " << flush;
		const auto input = ReadFile(inputFileName);
		cout << "ok\n";

		cout << "sample check ... " << flush;
		if (!Pulsejet::CheckSample(input.data()))
		{
			cout << "ERROR: Input is not a pulsejet sample\n\n";
			return 1;
		}
		cout << "ok\n";

		cout << "sample version: " << Pulsejet::SampleVersionString(input.data()) << "\n";
		cout << "sample version check ... " << flush;
		if (!Pulsejet::CheckSampleVersion(input.data()))
		{
			cout << "ERROR: Incompatible codec and sample versions\n\n";
			return 1;
		}
		cout << "ok\n";

		cout << "decoding ... " << flush;
		uint32_t numDecodedSamples;
		const auto decodedSample = Pulsejet::Decode(input.data(), &numDecodedSamples);
		cout << "ok, " << numDecodedSamples << " samples\n";

		cout << "writing ... " << flush;
		ofstream outputFile(outputFileName, ios::binary);
		outputFile.write(reinterpret_cast<const char *>(decodedSample), numDecodedSamples * sizeof(float));
		cout << "ok\n";

		cout << "cleanup ... " << flush;
		delete [] decodedSample;
		cout << "ok\n";

		cout << "decoding successful!\n";
	}
	else
	{
		ErrorInvalidArgs(argv);
		return 1;
	}

	return 0;
}
