#ifndef __DARKRL__BITMAPDOWNSAMPLED_HPP__
#define __DARKRL__BITMAPDOWNSAMPLED_HPP__

#include "Bitmap.h"
namespace ETC_PAK
{
	class BitmapDownsampled : public Bitmap
	{
	public:
		BitmapDownsampled(const Bitmap& bmp, unsigned int lines, bool linearize);
		~BitmapDownsampled();
	};
};

#endif
