///	DDS file support, does decoding, _not_ direct uploading
///	(use SOIL for that ;-)

///	A bunch of DirectDraw Surface structures and flags

typedef struct
{
	unsigned int    dwSize;
	unsigned int    dwFlags;
	unsigned int    dwFourCC;
	unsigned int    dwRGBBitCount;
	unsigned int    dwRBitMask;
	unsigned int    dwGBitMask;
	unsigned int    dwBBitMask;
	unsigned int    dwAlphaBitMask;
} DDS_PIXELFORMAT;

typedef struct {
	unsigned int    dwMagic;
	unsigned int    dwSize;
	unsigned int    dwFlags;
	unsigned int    dwHeight;
	unsigned int    dwWidth;
	unsigned int    dwPitchOrLinearSize;
	unsigned int    dwDepth;
	unsigned int    dwMipMapCount;
	unsigned int    dwReserved1[11];

	DDS_PIXELFORMAT sPixelFormat;

	//  DDCAPS2
	struct {
		unsigned int    dwCaps1;
		unsigned int    dwCaps2;
		unsigned int    dwDDSX;
		unsigned int    dwReserved;
	}               sCaps;
	unsigned int    dwReserved2;
} DDS_header;

//	the following constants were copied directly off the MSDN website

//	The dwFlags member of the original DDSURFACEDESC2 structure
//	can be set to one or more of the following values.
#define DDSD_CAPS	0x00000001
#define DDSD_HEIGHT	0x00000002
#define DDSD_WIDTH	0x00000004
#define DDSD_PITCH	0x00000008
#define DDSD_PIXELFORMAT	0x00001000
#define DDSD_MIPMAPCOUNT	0x00020000
#define DDSD_LINEARSIZE	0x00080000
#define DDSD_DEPTH	0x00800000

//	DirectDraw Pixel Format
#define DDPF_ALPHAPIXELS	0x00000001
#define DDPF_FOURCC	0x00000004
#define DDPF_RGB	0x00000040
#define DDPF_LUMINANCE   0x00020000  // DDPF_LUMINANCE
#define DDPF_ALPHA       0x00000002  // DDPF_ALPHA

//	The dwCaps1 member of the DDSCAPS2 structure can be
//	set to one or more of the following values.
#define DDSCAPS_COMPLEX	0x00000008
#define DDSCAPS_TEXTURE	0x00001000
#define DDSCAPS_MIPMAP	0x00400000

//	The dwCaps2 member of the DDSCAPS2 structure can be
//	set to one or more of the following values.
#define DDSCAPS2_CUBEMAP	0x00000200
#define DDSCAPS2_CUBEMAP_POSITIVEX	0x00000400
#define DDSCAPS2_CUBEMAP_NEGATIVEX	0x00000800
#define DDSCAPS2_CUBEMAP_POSITIVEY	0x00001000
#define DDSCAPS2_CUBEMAP_NEGATIVEY	0x00002000
#define DDSCAPS2_CUBEMAP_POSITIVEZ	0x00004000
#define DDSCAPS2_CUBEMAP_NEGATIVEZ	0x00008000
#define DDSCAPS2_VOLUME	0x00200000






struct _DDS_HEADER_DXT10
{
	_DXGI_FORMAT     dxgiFormat;
	unsigned int        resourceDimension;
	unsigned int        miscFlag; // see D3D11_RESOURCE_MISC_FLAG
	unsigned int        arraySize;
	unsigned int        miscFlags2;
};

#define _DDS_FOURCC 0x00000004
#define _MAKEFOURCC(ch0, ch1, ch2, ch3) \
	((unsigned int)(unsigned char)(ch0) | ((unsigned int)(unsigned char)(ch1) << 8) | \
	((unsigned int)(unsigned char)(ch2) << 16) | ((unsigned int)(unsigned char)(ch3) << 24 ))



//struct {
//	unsigned int    dwSize;
//	unsigned int    dwFlags;
//	unsigned int    dwFourCC;
//	unsigned int    dwRGBBitCount;
//	unsigned int    dwRBitMask;
//	unsigned int    dwGBitMask;
//	unsigned int    dwBBitMask;
//	unsigned int    dwAlphaBitMask;
//}               sPixelFormat;

#define _ISBITMASK( r,g,b,a ) ( ddpf.dwRBitMask == r && ddpf.dwGBitMask == g && ddpf.dwBBitMask == b && ddpf.dwAlphaBitMask == a )
#define _DDS_RGB         0x00000040  // DDPF_RGB
#define _DDS_LUMINANCE   0x00020000  // DDPF_LUMINANCE
#define _DDS_ALPHA       0x00000002  // DDPF_ALPHA
static _DXGI_FORMAT GetDXGIFormat(const DDS_PIXELFORMAT& ddpf)
{
	if (ddpf.dwFlags & _DDS_RGB)
	{
		// Note that sRGB formats are written using the "DX10" extended header

		switch (ddpf.dwRGBBitCount)
		{
		case 32:
			if (_ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
			{
				return _DXGI_FORMAT_R8G8B8A8_UNORM;
			}

			if (_ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
			{
				return _DXGI_FORMAT_B8G8R8A8_UNORM;
			}

			if (_ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000))
			{
				return _DXGI_FORMAT_B8G8R8X8_UNORM;
			}

			// No DXGI format maps to ISBITMASK(0x000000ff,0x0000ff00,0x00ff0000,0x00000000) aka D3DFMT_X8B8G8R8

			// Note that many common DDS reader/writers (including D3DX) swap the
			// the RED/BLUE masks for 10:10:10:2 formats. We assume
			// below that the 'backwards' header mask is being used since it is most
			// likely written by D3DX. The more robust solution is to use the 'DX10'
			// header extension and specify the DXGI_FORMAT_R10G10B10A2_UNORM format directly

			// For 'correct' writers, this should be 0x000003ff,0x000ffc00,0x3ff00000 for RGB data
			if (_ISBITMASK(0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000))
			{
				return _DXGI_FORMAT_R10G10B10A2_UNORM;
			}

			// No DXGI format maps to ISBITMASK(0x000003ff,0x000ffc00,0x3ff00000,0xc0000000) aka D3DFMT_A2R10G10B10

			if (_ISBITMASK(0x0000ffff, 0xffff0000, 0x00000000, 0x00000000))
			{
				return _DXGI_FORMAT_R16G16_UNORM;
			}

			if (_ISBITMASK(0xffffffff, 0x00000000, 0x00000000, 0x00000000))
			{
				// Only 32-bit color channel format in D3D9 was R32F
				return _DXGI_FORMAT_R32_FLOAT; // D3DX writes this out as a FourCC of 114
			}
			break;

		case 24:
			// No 24bpp DXGI formats aka D3DFMT_R8G8B8
			break;

		case 16:
			if (_ISBITMASK(0x7c00, 0x03e0, 0x001f, 0x8000))
			{
				return _DXGI_FORMAT_B5G5R5A1_UNORM;
			}
			if (_ISBITMASK(0xf800, 0x07e0, 0x001f, 0x0000))
			{
				return _DXGI_FORMAT_B5G6R5_UNORM;
			}

			// No DXGI format maps to ISBITMASK(0x7c00,0x03e0,0x001f,0x0000) aka D3DFMT_X1R5G5B5

			if (_ISBITMASK(0x0f00, 0x00f0, 0x000f, 0xf000))
			{
				return _DXGI_FORMAT_B4G4R4A4_UNORM;
			}

			// No DXGI format maps to ISBITMASK(0x0f00,0x00f0,0x000f,0x0000) aka D3DFMT_X4R4G4B4

			// No 3:3:2, 3:3:2:8, or paletted DXGI formats aka D3DFMT_A8R3G3B2, D3DFMT_R3G3B2, D3DFMT_P8, D3DFMT_A8P8, etc.
			break;
		}
	}
	else if (ddpf.dwFlags & _DDS_LUMINANCE)
	{
		if (8 == ddpf.dwRGBBitCount)
		{
			if (_ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x00000000))
			{
				return _DXGI_FORMAT_R8_UNORM; // D3DX10/11 writes this out as DX10 extension
			}

			// No DXGI format maps to ISBITMASK(0x0f,0x00,0x00,0xf0) aka D3DFMT_A4L4
		}

		if (16 == ddpf.dwRGBBitCount)
		{
			if (_ISBITMASK(0x0000ffff, 0x00000000, 0x00000000, 0x00000000))
			{
				return _DXGI_FORMAT_R16_UNORM; // D3DX10/11 writes this out as DX10 extension
			}
			if (_ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x0000ff00))
			{
				return _DXGI_FORMAT_R8G8_UNORM; // D3DX10/11 writes this out as DX10 extension
			}
		}
	}
	else if (ddpf.dwFlags & _DDS_ALPHA)
	{
		if (8 == ddpf.dwRGBBitCount)
		{
			return _DXGI_FORMAT_A8_UNORM;
		}
	}
	else if (ddpf.dwFlags & _DDS_FOURCC)
	{
		if (_MAKEFOURCC('D', 'X', 'T', '1') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC1_UNORM;
		}
		if (_MAKEFOURCC('D', 'X', 'T', '3') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC2_UNORM;
		}
		if (_MAKEFOURCC('D', 'X', 'T', '5') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC3_UNORM;
		}

		// While pre-multiplied alpha isn't directly supported by the DXGI formats,
		// they are basically the same as these BC formats so they can be mapped
		if (_MAKEFOURCC('D', 'X', 'T', '2') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC2_UNORM;
		}
		if (_MAKEFOURCC('D', 'X', 'T', '4') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC3_UNORM;
		}

		if (_MAKEFOURCC('A', 'T', 'I', '1') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC4_UNORM;
		}
		if (_MAKEFOURCC('B', 'C', '4', 'U') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC4_UNORM;
		}
		if (_MAKEFOURCC('B', 'C', '4', 'S') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC4_SNORM;
		}

		if (_MAKEFOURCC('A', 'T', 'I', '2') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC5_UNORM;
		}
		if (_MAKEFOURCC('B', 'C', '5', 'U') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC5_UNORM;
		}
		if (_MAKEFOURCC('B', 'C', '5', 'S') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_BC5_SNORM;
		}

		// BC6H and BC7 are written using the "DX10" extended header

		if (_MAKEFOURCC('R', 'G', 'B', 'G') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_R8G8_B8G8_UNORM;
		}
		if (_MAKEFOURCC('G', 'R', 'G', 'B') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_G8R8_G8B8_UNORM;
		}

		if (_MAKEFOURCC('Y', 'U', 'Y', '2') == ddpf.dwFourCC)
		{
			return _DXGI_FORMAT_YUY2;
		}

		// Check for D3DFORMAT enums being set here
		switch (ddpf.dwFourCC)
		{
		case 36: // D3DFMT_A16B16G16R16
			return _DXGI_FORMAT_R16G16B16A16_UNORM;

		case 110: // D3DFMT_Q16W16V16U16
			return _DXGI_FORMAT_R16G16B16A16_SNORM;

		case 111: // D3DFMT_R16F
			return _DXGI_FORMAT_R16_FLOAT;

		case 112: // D3DFMT_G16R16F
			return _DXGI_FORMAT_R16G16_FLOAT;

		case 113: // D3DFMT_A16B16G16R16F
			return _DXGI_FORMAT_R16G16B16A16_FLOAT;

		case 114: // D3DFMT_R32F
			return _DXGI_FORMAT_R32_FLOAT;

		case 115: // D3DFMT_G32R32F
			return _DXGI_FORMAT_R32G32_FLOAT;

		case 116: // D3DFMT_A32B32G32R32F
			return _DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
	}

	return _DXGI_FORMAT_UNKNOWN;
}


size_t IsDDSCompressed(_DXGI_FORMAT format)
{
	switch (format)
	{
	case _DXGI_FORMAT_BC1_TYPELESS:
	case _DXGI_FORMAT_BC1_UNORM:
	case _DXGI_FORMAT_BC1_UNORM_SRGB:
	case _DXGI_FORMAT_BC2_TYPELESS:
	case _DXGI_FORMAT_BC2_UNORM:
	case _DXGI_FORMAT_BC2_UNORM_SRGB:
	case _DXGI_FORMAT_BC3_TYPELESS:
	case _DXGI_FORMAT_BC3_UNORM:
	case _DXGI_FORMAT_BC3_UNORM_SRGB:
	case _DXGI_FORMAT_BC4_TYPELESS:
	case _DXGI_FORMAT_BC4_UNORM:
	case _DXGI_FORMAT_BC4_SNORM:
	case _DXGI_FORMAT_BC5_TYPELESS:
	case _DXGI_FORMAT_BC5_UNORM:
	case _DXGI_FORMAT_BC5_SNORM:
	case _DXGI_FORMAT_BC6H_TYPELESS:
	case _DXGI_FORMAT_BC6H_UF16:
	case _DXGI_FORMAT_BC6H_SF16:
	case _DXGI_FORMAT_BC7_TYPELESS:
	case _DXGI_FORMAT_BC7_UNORM:
	case _DXGI_FORMAT_BC7_UNORM_SRGB:
		return true;

	default:
		return false;
	}
}

_DXGI_FORMAT GetDDSFormat(const char* buffer)
{
	_DXGI_FORMAT format = _DXGI_FORMAT_UNKNOWN;
	DDS_header* header = (DDS_header*)buffer;

	if ((header->sPixelFormat.dwFlags & _DDS_FOURCC) &&
		(_MAKEFOURCC('D', 'X', '1', '0') == header->sPixelFormat.dwFourCC))
	{
		auto d3d10ext = reinterpret_cast<const _DDS_HEADER_DXT10*>((const char*)header + sizeof(DDS_header));
		format = d3d10ext->dxgiFormat;
	}
	else
	{
		format = GetDXGIFormat(header->sPixelFormat);
	}
	return format;
}



static int dds_test(stbi* s)
{
	//	check the magic number
	if (get8(s) != 'D') return 0;
	if (get8(s) != 'D') return 0;
	if (get8(s) != 'S') return 0;
	if (get8(s) != ' ') return 0;
	//	check header size
	if (get32le(s) != 124) return 0;
	return 1;
}
#ifndef STBI_NO_STDIO
int      stbi_dds_test_file(FILE* f)
{
	stbi s;
	int r, n = ftell(f);
	if (n == -1) return 0;

	start_file(&s, f);
	r = dds_test(&s);

	int nRet = fseek(f, n, SEEK_SET);
	return nRet == 0 ? r : 0;
}
#endif

int      stbi_dds_test_memory(stbi_uc const* buffer, int len)
{
	stbi s;
	start_mem(&s, buffer, len);
	return dds_test(&s);
}

//	helper functions
int stbi_convert_bit_range(int c, int from_bits, int to_bits)
{
	int b = (1 << (from_bits - 1)) + c * ((1 << to_bits) - 1);
	return (b + (b >> from_bits)) >> from_bits;
}
void stbi_rgb_888_from_565(unsigned int c, int* r, int* g, int* b)
{
	*r = stbi_convert_bit_range((c >> 11) & 31, 5, 8);
	*g = stbi_convert_bit_range((c >> 05) & 63, 6, 8);
	*b = stbi_convert_bit_range((c >> 00) & 31, 5, 8);
}
void stbi_decode_DXT1_block(
	unsigned char uncompressed[16 * 4],
	unsigned char compressed[8])
{
	int next_bit = 4 * 8;
	int i, r, g, b;
	int c0, c1;
	unsigned char decode_colors[4 * 4];
	//	find the 2 primary colors
	c0 = compressed[0] + (compressed[1] << 8);
	c1 = compressed[2] + (compressed[3] << 8);
	stbi_rgb_888_from_565(c0, &r, &g, &b);
	decode_colors[0] = r;
	decode_colors[1] = g;
	decode_colors[2] = b;
	decode_colors[3] = 255;
	stbi_rgb_888_from_565(c1, &r, &g, &b);
	decode_colors[4] = r;
	decode_colors[5] = g;
	decode_colors[6] = b;
	decode_colors[7] = 255;
	if (c0 > c1)
	{
		//	no alpha, 2 interpolated colors
		decode_colors[8] = (2 * decode_colors[0] + decode_colors[4]) / 3;
		decode_colors[9] = (2 * decode_colors[1] + decode_colors[5]) / 3;
		decode_colors[10] = (2 * decode_colors[2] + decode_colors[6]) / 3;
		decode_colors[11] = 255;
		decode_colors[12] = (decode_colors[0] + 2 * decode_colors[4]) / 3;
		decode_colors[13] = (decode_colors[1] + 2 * decode_colors[5]) / 3;
		decode_colors[14] = (decode_colors[2] + 2 * decode_colors[6]) / 3;
		decode_colors[15] = 255;
	}
	else
	{
		//	1 interpolated color, alpha
		decode_colors[8] = (decode_colors[0] + decode_colors[4]) / 2;
		decode_colors[9] = (decode_colors[1] + decode_colors[5]) / 2;
		decode_colors[10] = (decode_colors[2] + decode_colors[6]) / 2;
		decode_colors[11] = 255;
		//decode_colors[12] = 0;
		//decode_colors[13] = 0;
		//decode_colors[14] = 0;
		//decode_colors[15] = 0;
		memset(&decode_colors[12], 0, sizeof(unsigned char) * 4);
	}
	//	decode the block
	for (i = 0; i < 16 * 4; i += 4)
	{
		int idx = ((compressed[next_bit >> 3] >> (next_bit & 7)) & 3) * 4;
		next_bit += 2;
		//uncompressed[i+0] = decode_colors[idx+0];
		//uncompressed[i+1] = decode_colors[idx+1];
		//uncompressed[i+2] = decode_colors[idx+2];
		//uncompressed[i+3] = decode_colors[idx+3];
		memcpy(&uncompressed[i], &decode_colors[idx], sizeof(unsigned char) * 4);
	}



	//	done
}
void stbi_decode_DXT23_alpha_block(
	unsigned char uncompressed[16 * 4],
	unsigned char compressed[8])
{
	int i, next_bit = 0;
	//	each alpha value gets 4 bits
	for (i = 3; i < 16 * 4; i += 4)
	{
		uncompressed[i] = stbi_convert_bit_range(
			(compressed[next_bit >> 3] >> (next_bit & 7)) & 15,
			4, 8);
		next_bit += 4;
	}
}
void stbi_decode_DXT45_alpha_block(
	unsigned char uncompressed[16 * 4],
	unsigned char compressed[8])
{
	int i, next_bit = 8 * 2;
	unsigned char decode_alpha[8];
	//	each alpha value gets 3 bits, and the 1st 2 bytes are the range
	decode_alpha[0] = compressed[0];
	decode_alpha[1] = compressed[1];
	if (decode_alpha[0] > decode_alpha[1])
	{
		//	6 step intermediate
		decode_alpha[2] = (6 * decode_alpha[0] + 1 * decode_alpha[1]) / 7;
		decode_alpha[3] = (5 * decode_alpha[0] + 2 * decode_alpha[1]) / 7;
		decode_alpha[4] = (4 * decode_alpha[0] + 3 * decode_alpha[1]) / 7;
		decode_alpha[5] = (3 * decode_alpha[0] + 4 * decode_alpha[1]) / 7;
		decode_alpha[6] = (2 * decode_alpha[0] + 5 * decode_alpha[1]) / 7;
		decode_alpha[7] = (1 * decode_alpha[0] + 6 * decode_alpha[1]) / 7;
	}
	else
	{
		//	4 step intermediate, pluss full and none
		decode_alpha[2] = (4 * decode_alpha[0] + 1 * decode_alpha[1]) / 5;
		decode_alpha[3] = (3 * decode_alpha[0] + 2 * decode_alpha[1]) / 5;
		decode_alpha[4] = (2 * decode_alpha[0] + 3 * decode_alpha[1]) / 5;
		decode_alpha[5] = (1 * decode_alpha[0] + 4 * decode_alpha[1]) / 5;
		decode_alpha[6] = 0;
		decode_alpha[7] = 255;
	}
	for (i = 3; i < 16 * 4; i += 4)
	{
		int idx = 0, bit;
		bit = (compressed[next_bit >> 3] >> (next_bit & 7)) & 1;
		idx += bit << 0;
		++next_bit;
		bit = (compressed[next_bit >> 3] >> (next_bit & 7)) & 1;
		idx += bit << 1;
		++next_bit;
		bit = (compressed[next_bit >> 3] >> (next_bit & 7)) & 1;
		idx += bit << 2;
		++next_bit;
		uncompressed[i] = decode_alpha[idx & 7];
	}
	//	done
}
void stbi_decode_DXT_color_block(
	unsigned char uncompressed[16 * 4],
	unsigned char compressed[8])
{
	int next_bit = 4 * 8;
	int i, r, g, b;
	int c0, c1;
	unsigned char decode_colors[4 * 3];
	//	find the 2 primary colors
	c0 = compressed[0] + (compressed[1] << 8);
	c1 = compressed[2] + (compressed[3] << 8);
	stbi_rgb_888_from_565(c0, &r, &g, &b);
	decode_colors[0] = r;
	decode_colors[1] = g;
	decode_colors[2] = b;
	stbi_rgb_888_from_565(c1, &r, &g, &b);
	decode_colors[3] = r;
	decode_colors[4] = g;
	decode_colors[5] = b;
	//	Like DXT1, but no choicees:
	//	no alpha, 2 interpolated colors
	decode_colors[6] = (2 * decode_colors[0] + decode_colors[3]) / 3;
	decode_colors[7] = (2 * decode_colors[1] + decode_colors[4]) / 3;
	decode_colors[8] = (2 * decode_colors[2] + decode_colors[5]) / 3;
	decode_colors[9] = (decode_colors[0] + 2 * decode_colors[3]) / 3;
	decode_colors[10] = (decode_colors[1] + 2 * decode_colors[4]) / 3;
	decode_colors[11] = (decode_colors[2] + 2 * decode_colors[5]) / 3;
	//	decode the block
	for (i = 0; i < 16 * 4; i += 4)
	{
		int idx = ((compressed[next_bit >> 3] >> (next_bit & 7)) & 3) * 3;
		next_bit += 2;
		//uncompressed[i+0] = decode_colors[idx+0];
		//uncompressed[i+1] = decode_colors[idx+1];
		//uncompressed[i+2] = decode_colors[idx+2];
		memcpy(&uncompressed[i], &decode_colors[idx], sizeof(unsigned char) * 3);
	}
	//	done
}

void GetDDSFormatInfo(_DXGI_FORMAT format, int& hasAlpha, int& channels)
{
	switch (format)
	{
	case _DXGI_FORMAT_UNKNOWN:
        hasAlpha = 1;
        channels = 4;
		break;
	case _DXGI_FORMAT_R32G32B32A32_TYPELESS:	
	case _DXGI_FORMAT_R32G32B32A32_FLOAT:		
	case _DXGI_FORMAT_R32G32B32A32_UINT:		
	case _DXGI_FORMAT_R32G32B32A32_SINT:
    case _DXGI_FORMAT_R16G16B16A16_TYPELESS:        
    case _DXGI_FORMAT_R16G16B16A16_FLOAT:        
    case _DXGI_FORMAT_R16G16B16A16_UNORM:        
    case _DXGI_FORMAT_R16G16B16A16_UINT:        
    case _DXGI_FORMAT_R16G16B16A16_SNORM:        
    case _DXGI_FORMAT_R16G16B16A16_SINT:
        hasAlpha = 1;
        channels = 4;
		break;
	case _DXGI_FORMAT_R32G32B32_TYPELESS:		
	case _DXGI_FORMAT_R32G32B32_FLOAT:		
	case _DXGI_FORMAT_R32G32B32_UINT:		
	case _DXGI_FORMAT_R32G32B32_SINT:
        hasAlpha = 0;
        channels = 3;
		break;
	case _DXGI_FORMAT_R32G32_TYPELESS:		
	case _DXGI_FORMAT_R32G32_FLOAT:		
	case _DXGI_FORMAT_R32G32_UINT:		
	case _DXGI_FORMAT_R32G32_SINT:
        hasAlpha = 0;
        channels = 2;
		break;
	case _DXGI_FORMAT_R10G10B10A2_TYPELESS:		
	case _DXGI_FORMAT_R10G10B10A2_UNORM:		
	case _DXGI_FORMAT_R10G10B10A2_UINT:
        hasAlpha = 1;
        channels = 4;
		break;
	case _DXGI_FORMAT_R11G11B10_FLOAT:	
		break;
	case _DXGI_FORMAT_R8G8B8A8_TYPELESS:		
	case _DXGI_FORMAT_R8G8B8A8_UNORM:		
	case _DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:		
	case _DXGI_FORMAT_R8G8B8A8_UINT:		
	case _DXGI_FORMAT_R8G8B8A8_SNORM:		
	case _DXGI_FORMAT_R8G8B8A8_SINT:
        hasAlpha = 1;
        channels = 4;
		break;
	case _DXGI_FORMAT_R16G16_TYPELESS:		
	case _DXGI_FORMAT_R16G16_FLOAT:		
	case _DXGI_FORMAT_R16G16_UNORM:		
	case _DXGI_FORMAT_R16G16_UINT:		
	case _DXGI_FORMAT_R16G16_SNORM:		
	case _DXGI_FORMAT_R16G16_SINT:
        hasAlpha = 0;
        channels = 2;
		break;
	case _DXGI_FORMAT_R32_TYPELESS:		
	case _DXGI_FORMAT_D32_FLOAT:		
	case _DXGI_FORMAT_R32_FLOAT:		
	case _DXGI_FORMAT_R32_UINT:		
	case _DXGI_FORMAT_R32_SINT:
        hasAlpha = 0;
        channels = 1;
		break;
	case _DXGI_FORMAT_R8G8_TYPELESS:		
	case _DXGI_FORMAT_R8G8_UNORM:		
	case _DXGI_FORMAT_R8G8_UINT:		
	case _DXGI_FORMAT_R8G8_SNORM:		
	case _DXGI_FORMAT_R8G8_SINT:
        hasAlpha = 0;
        channels = 2;
		break;
	case _DXGI_FORMAT_R16_TYPELESS:		
	case _DXGI_FORMAT_R16_FLOAT:		
	case _DXGI_FORMAT_D16_UNORM:		
	case _DXGI_FORMAT_R16_UNORM:		
	case _DXGI_FORMAT_R16_UINT:		
	case _DXGI_FORMAT_R16_SNORM:		
	case _DXGI_FORMAT_R16_SINT:
        hasAlpha = 0;
        channels = 1;
		break;
	case _DXGI_FORMAT_R8_TYPELESS:		
	case _DXGI_FORMAT_R8_UNORM:		
	case _DXGI_FORMAT_R8_UINT:		
	case _DXGI_FORMAT_R8_SNORM:		
	case _DXGI_FORMAT_R8_SINT:		
	case _DXGI_FORMAT_A8_UNORM:		
	case _DXGI_FORMAT_R1_UNORM:
        hasAlpha = 0;
        channels = 1;
		break;
	case _DXGI_FORMAT_BC1_TYPELESS:		
	case _DXGI_FORMAT_BC1_UNORM:		
	case _DXGI_FORMAT_BC1_UNORM_SRGB:		
	case _DXGI_FORMAT_BC2_TYPELESS:		
	case _DXGI_FORMAT_BC2_UNORM:		
	case _DXGI_FORMAT_BC2_UNORM_SRGB:		
	case _DXGI_FORMAT_BC3_TYPELESS:		
	case _DXGI_FORMAT_BC3_UNORM:		
	case _DXGI_FORMAT_BC3_UNORM_SRGB:
		hasAlpha = 1;
		channels = 4;
		break;
	case _DXGI_FORMAT_BC4_TYPELESS:		
	case _DXGI_FORMAT_BC4_UNORM:		
	case _DXGI_FORMAT_BC4_SNORM:
        hasAlpha = 0;
        channels = 1;
		break;
	case _DXGI_FORMAT_BC5_TYPELESS:		
	case _DXGI_FORMAT_BC5_UNORM:		
	case _DXGI_FORMAT_BC5_SNORM:
        hasAlpha = 0;
        channels = 2;
		break;
	case _DXGI_FORMAT_B5G6R5_UNORM:
        hasAlpha = 0;
        channels = 3;
		break;
	case _DXGI_FORMAT_B5G5R5A1_UNORM:
        hasAlpha = 1;
        channels = 4;
		break;
	case _DXGI_FORMAT_B8G8R8A8_UNORM:
	case _DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case _DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        hasAlpha = 1;
        channels = 4;
		break;
	case _DXGI_FORMAT_B8G8R8X8_UNORM:
	case _DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case _DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        hasAlpha = 0;
        channels = 4;
		break;			
	case _DXGI_FORMAT_BC6H_TYPELESS:		
	case _DXGI_FORMAT_BC6H_UF16:		
	case _DXGI_FORMAT_BC6H_SF16:
        hasAlpha = 0;
        channels = 3;
		break;
	case _DXGI_FORMAT_BC7_TYPELESS:		
	case _DXGI_FORMAT_BC7_UNORM:		
	case _DXGI_FORMAT_BC7_UNORM_SRGB:	
        hasAlpha = 1;
        channels = 4;
		break;
	default:
		assert(0);
		break;
	}
}

int _Is2n(int v) { return v != 0 && (v & (v - 1)) == 0; }
static void GetDDSInfo(stbi_uc const* buffer, int len, int* x, int* y, int* comp, int* faces, int* bCompressed, _DXGI_FORMAT* format)
{
	stbi b;
	stbi* s = &b;
	start_mem(s, buffer, len);
	//	all variables go up front
	stbi_uc* dds_data = NULL;
	//stbi_uc block[16 * 4];
	//stbi_uc compressed[8];
	unsigned flags;
	//int DXT_family;
	int has_alpha, has_mipmap;
	int is_compressed, cubemap_faces;
	int block_pitch, num_blocks;
	DDS_header header;
	//int i, sz, cf;
	*faces = 0;
	*bCompressed = 0;
	//	load the header
	if (sizeof(DDS_header) != 128)
	{
		return;
	}

	*format = GetDDSFormat((const char*)buffer);
	*bCompressed = (int)IsDDSCompressed(*format);

	getn(s, (stbi_uc*)(&header), 128);
	//	and do some checking
	if (header.dwMagic != (('D' << 0) | ('D' << 8) | ('S' << 16) | (' ' << 24))) return;
	if (header.dwSize != 124) return;
	flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	if ((header.dwFlags & flags) != flags) return;
	/*	According to the MSDN spec, the dwFlags should contain
		DDSD_LINEARSIZE if it's compressed, or DDSD_PITCH if
		uncompressed.  Some DDS writers do not conform to the
		spec, so I need to make my reader more tolerant	*/
	if (header.sPixelFormat.dwSize != 32) return;
	flags = DDPF_FOURCC | DDPF_RGB | DDPF_ALPHA | DDPF_LUMINANCE;
	if ((header.sPixelFormat.dwFlags & flags) == 0) return;
	if ((header.sCaps.dwCaps1 & DDSCAPS_TEXTURE) == 0) return;
	//	get the image data
	s->img_x = header.dwWidth;
	s->img_y = header.dwHeight;
	//s->img_n = 4;
	int hasAlpha = 0;
	GetDDSFormatInfo(*format, hasAlpha, s->img_n);
	is_compressed = (header.sPixelFormat.dwFlags & DDPF_FOURCC) / DDPF_FOURCC;
	has_alpha = (header.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) / DDPF_ALPHAPIXELS;
	//assert(has_alpha == hasAlpha);
	has_mipmap = (header.sCaps.dwCaps1 & DDSCAPS_MIPMAP) && (header.dwMipMapCount > 1);
	cubemap_faces = (header.sCaps.dwCaps2 & DDSCAPS2_CUBEMAP) / DDSCAPS2_CUBEMAP;
	/*	I need cubemaps to have square faces	*/
	cubemap_faces &= (s->img_x == s->img_y);
	cubemap_faces *= 5;
	cubemap_faces += 1;
	block_pitch = (s->img_x + 3) >> 2;
	num_blocks = block_pitch * ((s->img_y + 3) >> 2);
	/*	let the user know what's going on	*/
	*x = s->img_x;
	*y = s->img_y;
	*comp = s->img_n;
	*faces = cubemap_faces;
	//if (!_Is2n(*x) || !_Is2n(*y))
	//{
	//	return;
	//}
	///*	is this uncompressed?	*/
	//if (is_compressed)
	//{
	//	/*	compressed	*/
	//	//	note: header.sPixelFormat.dwFourCC is something like (('D'<<0)|('X'<<8)|('T'<<16)|('1'<<24))
	//	DXT_family = 1 + (header.sPixelFormat.dwFourCC >> 24) - '1';
	//	if ((DXT_family < 1) || (DXT_family > 5))
	//	{
	//		return;
	//	}
	//	/*	check the expected size...oops, nevermind...
	//		those non-compliant writers leave
	//		dwPitchOrLinearSize == 0	*/
	//		//	passed all the tests, get the RAM for decoding
	//	sz = (s->img_x) * (s->img_y) * 4 * cubemap_faces;
	//	dds_data = (unsigned char*)malloc(sz);
	//	/*	do this once for each face	*/
	//	for (cf = 0; cf < cubemap_faces; ++cf)
	//	{
	//		//	now read and decode all the blocks
	//		for (i = 0; i < num_blocks; ++i)
	//		{
	//			//	where are we?
	//			//int bx = 0, by = 0;
	//			int bw = 4, bh = 4;
	//			int ref_x = 4 * (i % block_pitch);
	//			int ref_y = 4 * (i / block_pitch);
	//			//	get the next block's worth of compressed data, and decompress it
	//			if (DXT_family == 1)
	//			{
	//				//	DXT1
	//				getn(s, compressed, 8);
	//				stbi_decode_DXT1_block(block, compressed);
	//			}
	//			else if (DXT_family < 4)
	//			{
	//				//	DXT2/3
	//				getn(s, compressed, 8);
	//				stbi_decode_DXT23_alpha_block(block, compressed);
	//				getn(s, compressed, 8);
	//				stbi_decode_DXT_color_block(block, compressed);
	//			}
	//			else
	//			{
	//				//	DXT4/5
	//				getn(s, compressed, 8);
	//				stbi_decode_DXT45_alpha_block(block, compressed);
	//				getn(s, compressed, 8);
	//				stbi_decode_DXT_color_block(block, compressed);
	//			}
	//			//	is this a partial block?
	//			if (ref_x + 4 > (int)s->img_x)
	//			{
	//				bw = s->img_x - ref_x;
	//			}
	//			if (ref_y + 4 > (int)s->img_y)
	//			{
	//				bh = s->img_y - ref_y;
	//			}
	//			//	now drop our decompressed data into the buffer
	//			//for( by = 0; by < bh; ++by )
	//			//{
	//			//	int idx = 4*((ref_y+by+cf*s->img_x)*s->img_x + ref_x);
	//			//	for( bx = 0; bx < bw*4; ++bx )
	//			//	{
	//			//		dds_data[idx+bx] = block[by*16+bx];
	//			//	}
	//			//}
	//			//去除循环结构的优化
	//			int idx = 4 * ((ref_y + 0 + cf * s->img_x) * s->img_x + ref_x);
	//			memcpy(&dds_data[idx], &block[0 * 16], bw * 4);
	//			idx = 4 * ((ref_y + 1 + cf * s->img_x) * s->img_x + ref_x);
	//			memcpy(&dds_data[idx], &block[1 * 16], bw * 4);
	//			idx = 4 * ((ref_y + 2 + cf * s->img_x) * s->img_x + ref_x);
	//			memcpy(&dds_data[idx], &block[2 * 16], bw * 4);
	//			idx = 4 * ((ref_y + 3 + cf * s->img_x) * s->img_x + ref_x);
	//			memcpy(&dds_data[idx], &block[3 * 16], bw * 4);
	//		}
	//		/*	done reading and decoding the main image...
	//			skip MIPmaps if present	*/
	//		if (has_mipmap)
	//		{
	//			int block_size = 16;
	//			if (DXT_family == 1)
	//			{
	//				block_size = 8;
	//			}
	//			for (i = 1; i < (int)header.dwMipMapCount; ++i)
	//			{
	//				int mx = s->img_x >> (i + 2);
	//				int my = s->img_y >> (i + 2);
	//				if (mx < 1)
	//				{
	//					mx = 1;
	//				}
	//				if (my < 1)
	//				{
	//					my = 1;
	//				}
	//				skip(s, mx * my * block_size);
	//			}
	//		}
	//	}/* per cubemap face */
	//}
	//else
	//{
	//	/*	uncompressed	*/
	//	DXT_family = 0;
	//	/*s->img_n = 3;
	//	if (has_alpha)
	//	{
	//		s->img_n = 4;
	//	}*/						
	//	*comp = s->img_n;

	//	sz = s->img_x * s->img_y * s->img_n * cubemap_faces;
	//	dds_data = (unsigned char*)malloc(sz);
	//	/*	do this once for each face	*/
	//	for (cf = 0; cf < cubemap_faces; ++cf)
	//	{
	//		/*	read the main image for this face	*/
	//		getn(s, &dds_data[cf * s->img_x * s->img_y * s->img_n], s->img_x * s->img_y * s->img_n);
	//		/*	done reading and decoding the main image...
	//			skip MIPmaps if present	*/
	//		if (has_mipmap)
	//		{
	//			for (i = 1; i < (int)header.dwMipMapCount; ++i)
	//			{
	//				int mx = s->img_x >> i;
	//				int my = s->img_y >> i;
	//				if (mx < 1)
	//				{
	//					mx = 1;
	//				}
	//				if (my < 1)
	//				{
	//					my = 1;
	//				}
	//				skip(s, mx * my * s->img_n);
	//			}
	//		}
	//	}
	//	/*	data was BGR, I need it RGB	*/

	//	if (s->img_n > 1)
	//	{
	//		for (i = 0; i < sz; i += s->img_n)
	//		{
	//			unsigned char temp = dds_data[i];
	//			dds_data[i] = dds_data[i + 2];
	//			dds_data[i + 2] = temp;
	//		}
	//	}
	//}
	///*	finished decompressing into RGBA,
	//	adjust the y size if we have a cubemap
	//	note: sz is already up to date	*/
	//	//s->img_y *= cubemap_faces;
	//	//*y = s->img_y;
	//	//	did the user want something else, or
	//	//	see if all the alpha values are 255 (i.e. no transparency)
	//
	///*has_alpha = 0;
	//if (s->img_n == 4)
	//{
	//	for (i = 3; (i < sz) && (has_alpha == 0); i += 4)
	//	{
	//		has_alpha |= (dds_data[i] < 255);
	//	}
	//}

	//if (has_alpha)
	//{
	//	assert(*comp == 4);
	//}
	//else
	//{
	//	assert(*comp == 3);
	//}*/


	////if ((req_comp <= 4) && (req_comp >= 1))
	////{
	////	//	user has some requirements, meet them
	////	if (req_comp != s->img_n)
	////	{
	////		//dds_data = convert_format(dds_data, s->img_n, req_comp, s->img_x, s->img_y);
	////		*comp = s->img_n;
	////	}
	////}
	////else
	////{
	////	//	user had no requirements, only drop to RGB is no alpha
	////	if ((has_alpha == 0) && (s->img_n == 4))
	////	{
	////		dds_data = convert_format(dds_data, 4, 3, s->img_x, s->img_y);
	////		*comp = 3;
	////	}
	////}
	////	OK, done
	//free(dds_data);
}

static stbi_uc* dds_load(stbi* s, int* x, int* y, int* comp, int req_comp)
{
	//	all variables go up front
	stbi_uc* dds_data = NULL;
	stbi_uc block[16 * 4];
	stbi_uc compressed[8];
	unsigned flags;
	int DXT_family;
	int has_alpha, has_mipmap;
	int is_compressed, cubemap_faces;
	int block_pitch, num_blocks;
	DDS_header header;
	int i, sz, cf;
	//	load the header
	if (sizeof(DDS_header) != 128)
	{
		return NULL;
	}
	getn(s, (stbi_uc*)(&header), 128);
	//	and do some checking
	if (header.dwMagic != (('D' << 0) | ('D' << 8) | ('S' << 16) | (' ' << 24))) return NULL;
	if (header.dwSize != 124) return NULL;
	flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	if ((header.dwFlags & flags) != flags) return NULL;
	/*	According to the MSDN spec, the dwFlags should contain
		DDSD_LINEARSIZE if it's compressed, or DDSD_PITCH if
		uncompressed.  Some DDS writers do not conform to the
		spec, so I need to make my reader more tolerant	*/
	if (header.sPixelFormat.dwSize != 32) return NULL;
	flags = DDPF_FOURCC | DDPF_RGB;
	if ((header.sPixelFormat.dwFlags & flags) == 0) return NULL;
	if ((header.sCaps.dwCaps1 & DDSCAPS_TEXTURE) == 0) return NULL;
	//	get the image data
	s->img_x = header.dwWidth;
	s->img_y = header.dwHeight;
	//s->img_n = 4;
	int hasAlpha = 0;
	_DXGI_FORMAT format = GetDXGIFormat(header.sPixelFormat);
	GetDDSFormatInfo(format, hasAlpha, s->img_n);
	is_compressed = (header.sPixelFormat.dwFlags & DDPF_FOURCC) / DDPF_FOURCC;
	has_alpha = (header.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) / DDPF_ALPHAPIXELS;
	has_mipmap = (header.sCaps.dwCaps1 & DDSCAPS_MIPMAP) && (header.dwMipMapCount > 1);
	cubemap_faces = (header.sCaps.dwCaps2 & DDSCAPS2_CUBEMAP) / DDSCAPS2_CUBEMAP;
	/*	I need cubemaps to have square faces	*/
	cubemap_faces &= (s->img_x == s->img_y);
	cubemap_faces *= 5;
	cubemap_faces += 1;
	block_pitch = (s->img_x + 3) >> 2;
	num_blocks = block_pitch * ((s->img_y + 3) >> 2);
	/*	let the user know what's going on	*/
	*x = s->img_x;
	*y = s->img_y;
	*comp = s->img_n;
	/*	is this uncompressed?	*/
	if (is_compressed)
	{
		/*	compressed	*/
		//	note: header.sPixelFormat.dwFourCC is something like (('D'<<0)|('X'<<8)|('T'<<16)|('1'<<24))
		DXT_family = 1 + (header.sPixelFormat.dwFourCC >> 24) - '1';
		if ((DXT_family < 1) || (DXT_family > 5)) return NULL;
		/*	check the expected size...oops, nevermind...
			those non-compliant writers leave
			dwPitchOrLinearSize == 0	*/
			//	passed all the tests, get the RAM for decoding
		sz = (s->img_x) * (s->img_y) * 4 * cubemap_faces;
		dds_data = (unsigned char*)malloc(sz);
		/*	do this once for each face	*/
		for (cf = 0; cf < cubemap_faces; ++cf)
		{
			//	now read and decode all the blocks
			for (i = 0; i < num_blocks; ++i)
			{
				//	where are we?
				//int bx = 0, by = 0;
				int bw = 4, bh = 4;
				int ref_x = 4 * (i % block_pitch);
				int ref_y = 4 * (i / block_pitch);
				//	get the next block's worth of compressed data, and decompress it
				if (DXT_family == 1)
				{
					//	DXT1
					getn(s, compressed, 8);
					stbi_decode_DXT1_block(block, compressed);
				}
				else if (DXT_family < 4)
				{
					//	DXT2/3
					getn(s, compressed, 8);
					stbi_decode_DXT23_alpha_block(block, compressed);
					getn(s, compressed, 8);
					stbi_decode_DXT_color_block(block, compressed);
				}
				else
				{
					//	DXT4/5
					getn(s, compressed, 8);
					stbi_decode_DXT45_alpha_block(block, compressed);
					getn(s, compressed, 8);
					stbi_decode_DXT_color_block(block, compressed);
				}
				//	is this a partial block?
				if (ref_x + 4 > (int)s->img_x)
				{
					bw = s->img_x - ref_x;
				}
				if (ref_y + 4 > (int)s->img_y)
				{
					bh = s->img_y - ref_y;
				}
				//	now drop our decompressed data into the buffer
				//for( by = 0; by < bh; ++by )
				//{
				//	int idx = 4*((ref_y+by+cf*s->img_x)*s->img_x + ref_x);
				//	for( bx = 0; bx < bw*4; ++bx )
				//	{
				//		dds_data[idx+bx] = block[by*16+bx];
				//	}
				//}
				//去除循环结构的优化
				int idx = 4 * ((ref_y + 0 + cf * s->img_x) * s->img_x + ref_x);
				memcpy(&dds_data[idx], &block[0 * 16], bw * 4);
				idx = 4 * ((ref_y + 1 + cf * s->img_x) * s->img_x + ref_x);
				memcpy(&dds_data[idx], &block[1 * 16], bw * 4);
				idx = 4 * ((ref_y + 2 + cf * s->img_x) * s->img_x + ref_x);
				memcpy(&dds_data[idx], &block[2 * 16], bw * 4);
				idx = 4 * ((ref_y + 3 + cf * s->img_x) * s->img_x + ref_x);
				memcpy(&dds_data[idx], &block[3 * 16], bw * 4);
			}
			/*	done reading and decoding the main image...
				skip MIPmaps if present	*/
			if (has_mipmap)
			{
				int block_size = 16;
				if (DXT_family == 1)
				{
					block_size = 8;
				}
				for (i = 1; i < (int)header.dwMipMapCount; ++i)
				{
					int mx = s->img_x >> (i + 2);
					int my = s->img_y >> (i + 2);
					if (mx < 1)
					{
						mx = 1;
					}
					if (my < 1)
					{
						my = 1;
					}
					skip(s, mx * my * block_size);
				}
			}
		}/* per cubemap face */
	}
	else
	{
		/*	uncompressed	*/
		DXT_family = 0;
		/*s->img_n = 3;
        if (has_alpha)
        {
            assert(s->img_n == 4);
        }*/
		*comp = s->img_n;
		sz = s->img_x * s->img_y * s->img_n * cubemap_faces;
		dds_data = (unsigned char*)malloc(sz);
		/*	do this once for each face	*/
		for (cf = 0; cf < cubemap_faces; ++cf)
		{
			/*	read the main image for this face	*/
			getn(s, &dds_data[cf * s->img_x * s->img_y * s->img_n], s->img_x * s->img_y * s->img_n);
			/*	done reading and decoding the main image...
				skip MIPmaps if present	*/
			if (has_mipmap)
			{
				for (i = 1; i < (int)header.dwMipMapCount; ++i)
				{
					int mx = s->img_x >> i;
					int my = s->img_y >> i;
					if (mx < 1)
					{
						mx = 1;
					}
					if (my < 1)
					{
						my = 1;
					}
					skip(s, mx * my * s->img_n);
				}
			}
		}
		/*	data was BGR, I need it RGB	*/
		//for (i = 0; i < sz; i += s->img_n)
		//{
		//	unsigned char temp = dds_data[i];
		//	dds_data[i] = dds_data[i + 2];
		//	dds_data[i + 2] = temp;
		//}
	}
	/*	finished decompressing into RGBA,
		adjust the y size if we have a cubemap
		note: sz is already up to date	*/
	s->img_y *= cubemap_faces;
	*y = s->img_y;
	//	did the user want something else, or
	//	see if all the alpha values are 255 (i.e. no transparency)
	has_alpha = 0;
	if (s->img_n == 4)
	{
		for (i = 3; (i < sz) && (has_alpha == 0); i += 4)
		{
			has_alpha |= (dds_data[i] < 255);
		}
	}
	if ((req_comp <= 4) && (req_comp >= 1))
	{
		//	user has some requirements, meet them
		if (req_comp != s->img_n)
		{
			dds_data = convert_format(dds_data, s->img_n, req_comp, s->img_x, s->img_y);
			*comp = s->img_n;
		}
	}
	else
	{
		//	user had no requirements, only drop to RGB is no alpha
		if ((has_alpha == 0) && (s->img_n == 4))
		{
			dds_data = convert_format(dds_data, 4, 3, s->img_x, s->img_y);
			*comp = 3;
		}
	}
	//	OK, done
	return dds_data;
}

#ifndef STBI_NO_STDIO
stbi_uc* stbi_dds_load_from_file(FILE* f, int* x, int* y, int* comp, int req_comp)
{
	stbi s;
	start_file(&s, f);
	return dds_load(&s, x, y, comp, req_comp);
}

stbi_uc* stbi_dds_load(char* filename, int* x, int* y, int* comp, int req_comp)
{
	stbi_uc* data;
	FILE* f = fopen(filename, "rb");
	if (!f) return NULL;
	data = stbi_dds_load_from_file(f, x, y, comp, req_comp);
	fclose(f);
	return data;
}
#endif

stbi_uc* stbi_dds_load_from_memory(stbi_uc const* buffer, int len, int* x, int* y, int* comp, int req_comp)
{
	stbi s;
	start_mem(&s, buffer, len);
	return dds_load(&s, x, y, comp, req_comp);

}






void GetDDS_Info(stbi_uc const* buffer, int len, int* x, int* y, int* comp, int* faces, int* bCompressed, _DXGI_FORMAT* format)
{
	GetDDSInfo(buffer, len, x, y, comp, faces, bCompressed, format);
}
