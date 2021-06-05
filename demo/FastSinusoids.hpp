#pragma once

#define _USE_MATH_DEFINES
#include <cmath>

namespace FastSinusoids
{
	void Init();
	double Cos(double x);
	inline double Sin(double x)
	{
		return Cos(x - M_PI_2);
	}
	inline float CosF(float x)
	{
		return static_cast<float>(Cos(static_cast<double>(x)));
	}
	inline float SinF(float x)
	{
		return static_cast<float>(Sin(static_cast<double>(x)));
	}
}
