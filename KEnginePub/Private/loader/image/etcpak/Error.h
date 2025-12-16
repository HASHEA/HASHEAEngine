#ifndef __ERROR_HPP__
#define __ERROR_HPP__

#include "Bitmap.h"
namespace ETC_PAK
{
	float CalcMSE3(const Bitmap& bmp, const Bitmap& out);
	float CalcMSE1(const Bitmap& bmp, const Bitmap& out);
};

#endif
