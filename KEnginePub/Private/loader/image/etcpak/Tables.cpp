#include "Tables.h"

const int32_t ETC_PAK::g_table[8][4] = {
    {2,  8,   -2,  -8  },
    {5,  17,  -5,  -17 },
    {9,  29,  -9,  -29 },
    {13, 42,  -13, -42 },
    {18, 60,  -18, -60 },
    {24, 80,  -24, -80 },
    {33, 106, -33, -106},
    {47, 183, -47, -183}
};

const int64_t ETC_PAK::g_table256[8][4] = {
    {2 * 256,  8 * 256,   -2 * 256,  -8 * 256  },
    {5 * 256,  17 * 256,  -5 * 256,  -17 * 256 },
    {9 * 256,  29 * 256,  -9 * 256,  -29 * 256 },
    {13 * 256, 42 * 256,  -13 * 256, -42 * 256 },
    {18 * 256, 60 * 256,  -18 * 256, -60 * 256 },
    {24 * 256, 80 * 256,  -24 * 256, -80 * 256 },
    {33 * 256, 106 * 256, -33 * 256, -106 * 256},
    {47 * 256, 183 * 256, -47 * 256, -183 * 256}
};

const uint32_t ETC_PAK::g_id[4][16] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2},
    {5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4},
    {7, 7, 6, 6, 7, 7, 6, 6, 7, 7, 6, 6, 7, 7, 6, 6}
};

const uint32_t ETC_PAK::g_avg2[16] = {
    0x00,
    0x11,
    0x22,
    0x33,
    0x44,
    0x55,
    0x66,
    0x77,
    0x88,
    0x99,
    0xAA,
    0xBB,
    0xCC,
    0xDD,
    0xEE,
    0xFF
};

const uint32_t ETC_PAK::g_flags[64] = {
    0x80800402, 0x80800402, 0x80800402, 0x80800402,
    0x80800402, 0x80800402, 0x80800402, 0x8080E002,
    0x80800402, 0x80800402, 0x8080E002, 0x8080E002,
    0x80800402, 0x8080E002, 0x8080E002, 0x8080E002,
    0x80000402, 0x80000402, 0x80000402, 0x80000402,
    0x80000402, 0x80000402, 0x80000402, 0x8000E002,
    0x80000402, 0x80000402, 0x8000E002, 0x8000E002,
    0x80000402, 0x8000E002, 0x8000E002, 0x8000E002,
    0x00800402, 0x00800402, 0x00800402, 0x00800402,
    0x00800402, 0x00800402, 0x00800402, 0x0080E002,
    0x00800402, 0x00800402, 0x0080E002, 0x0080E002,
    0x00800402, 0x0080E002, 0x0080E002, 0x0080E002,
    0x00000402, 0x00000402, 0x00000402, 0x00000402,
    0x00000402, 0x00000402, 0x00000402, 0x0000E002,
    0x00000402, 0x00000402, 0x0000E002, 0x0000E002,
    0x00000402, 0x0000E002, 0x0000E002, 0x0000E002
};

const int32_t ETC_PAK::g_alpha[16][8] = {
    {-3, -6, -9,  -15, 2, 5, 8, 14},
    {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8,  -13, 1, 4, 7, 12},
    {-2, -4, -6,  -13, 1, 3, 5, 12},
    {-3, -6, -8,  -12, 2, 5, 7, 11},
    {-3, -7, -9,  -11, 2, 6, 8, 10},
    {-4, -7, -8,  -11, 3, 6, 7, 10},
    {-3, -5, -8,  -11, 2, 4, 7, 10},
    {-2, -6, -8,  -10, 1, 5, 7, 9 },
    {-2, -5, -8,  -10, 1, 4, 7, 9 },
    {-2, -4, -8,  -10, 1, 3, 7, 9 },
    {-2, -5, -7,  -10, 1, 4, 6, 9 },
    {-3, -4, -7,  -10, 2, 3, 6, 9 },
    {-1, -2, -3,  -10, 0, 1, 2, 9 },
    {-4, -6, -8,  -9,  3, 5, 7, 8 },
    {-3, -5, -7,  -9,  2, 4, 6, 8 }
};

const int32_t ETC_PAK::g_alphaRange[16] = {
    0x100FF / (1 + ETC_PAK::g_alpha[0][7] - ETC_PAK::g_alpha[0][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[1][7] - ETC_PAK::g_alpha[1][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[2][7] - ETC_PAK::g_alpha[2][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[3][7] - ETC_PAK::g_alpha[3][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[4][7] - ETC_PAK::g_alpha[4][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[5][7] - ETC_PAK::g_alpha[5][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[6][7] - ETC_PAK::g_alpha[6][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[7][7] - ETC_PAK::g_alpha[7][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[8][7] - ETC_PAK::g_alpha[8][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[9][7] - ETC_PAK::g_alpha[9][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[10][7] - ETC_PAK::g_alpha[10][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[11][7] - ETC_PAK::g_alpha[11][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[12][7] - ETC_PAK::g_alpha[12][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[13][7] - ETC_PAK::g_alpha[13][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[14][7] - ETC_PAK::g_alpha[14][3]),
    0x100FF / (1 + ETC_PAK::g_alpha[15][7] - ETC_PAK::g_alpha[15][3]),
};

#ifdef __SSE4_1__
const __m128i ETC_PAK::g_table_SIMD[2] =
    {
        _mm_setr_epi16(2, 5, 9, 13, 18, 24, 33, 47),
        _mm_setr_epi16(8, 17, 29, 42, 60, 80, 106, 183)
};
const __m128i ETC_PAK::g_table128_SIMD[2] =
    {
        _mm_setr_epi16(2 * 128, 5 * 128, 9 * 128, 13 * 128, 18 * 128, 24 * 128, 33 * 128, 47 * 128),
        _mm_setr_epi16(8 * 128, 17 * 128, 29 * 128, 42 * 128, 60 * 128, 80 * 128, 106 * 128, 183 * 128)
};
const __m128i ETC_PAK::g_table256_SIMD[4] =
    {
        _mm_setr_epi32(2 * 256, 5 * 256, 9 * 256, 13 * 256),
        _mm_setr_epi32(8 * 256, 17 * 256, 29 * 256, 42 * 256),
        _mm_setr_epi32(18 * 256, 24 * 256, 33 * 256, 47 * 256),
        _mm_setr_epi32(60 * 256, 80 * 256, 106 * 256, 183 * 256)
};

const __m128i ETC_PAK::g_alpha_SIMD[16] = {
    _mm_setr_epi16(ETC_PAK::g_alpha[0][0], ETC_PAK::g_alpha[0][1], ETC_PAK::g_alpha[0][2], ETC_PAK::g_alpha[0][3], ETC_PAK::g_alpha[0][4], ETC_PAK::g_alpha[0][5], ETC_PAK::g_alpha[0][6], ETC_PAK::g_alpha[0][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[1][0], ETC_PAK::g_alpha[1][1], ETC_PAK::g_alpha[1][2], ETC_PAK::g_alpha[1][3], ETC_PAK::g_alpha[1][4], ETC_PAK::g_alpha[1][5], ETC_PAK::g_alpha[1][6], ETC_PAK::g_alpha[1][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[2][0], ETC_PAK::g_alpha[2][1], ETC_PAK::g_alpha[2][2], ETC_PAK::g_alpha[2][3], ETC_PAK::g_alpha[2][4], ETC_PAK::g_alpha[2][5], ETC_PAK::g_alpha[2][6], ETC_PAK::g_alpha[2][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[3][0], ETC_PAK::g_alpha[3][1], ETC_PAK::g_alpha[3][2], ETC_PAK::g_alpha[3][3], ETC_PAK::g_alpha[3][4], ETC_PAK::g_alpha[3][5], ETC_PAK::g_alpha[3][6], ETC_PAK::g_alpha[3][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[4][0], ETC_PAK::g_alpha[4][1], ETC_PAK::g_alpha[4][2], ETC_PAK::g_alpha[4][3], ETC_PAK::g_alpha[4][4], ETC_PAK::g_alpha[4][5], ETC_PAK::g_alpha[4][6], ETC_PAK::g_alpha[4][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[5][0], ETC_PAK::g_alpha[5][1], ETC_PAK::g_alpha[5][2], ETC_PAK::g_alpha[5][3], ETC_PAK::g_alpha[5][4], ETC_PAK::g_alpha[5][5], ETC_PAK::g_alpha[5][6], ETC_PAK::g_alpha[5][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[6][0], ETC_PAK::g_alpha[6][1], ETC_PAK::g_alpha[6][2], ETC_PAK::g_alpha[6][3], ETC_PAK::g_alpha[6][4], ETC_PAK::g_alpha[6][5], ETC_PAK::g_alpha[6][6], ETC_PAK::g_alpha[6][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[7][0], ETC_PAK::g_alpha[7][1], ETC_PAK::g_alpha[7][2], ETC_PAK::g_alpha[7][3], ETC_PAK::g_alpha[7][4], ETC_PAK::g_alpha[7][5], ETC_PAK::g_alpha[7][6], ETC_PAK::g_alpha[7][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[8][0], ETC_PAK::g_alpha[8][1], ETC_PAK::g_alpha[8][2], ETC_PAK::g_alpha[8][3], ETC_PAK::g_alpha[8][4], ETC_PAK::g_alpha[8][5], ETC_PAK::g_alpha[8][6], ETC_PAK::g_alpha[8][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[9][0], ETC_PAK::g_alpha[9][1], ETC_PAK::g_alpha[9][2], ETC_PAK::g_alpha[9][3], ETC_PAK::g_alpha[9][4], ETC_PAK::g_alpha[9][5], ETC_PAK::g_alpha[9][6], ETC_PAK::g_alpha[9][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[10][0], ETC_PAK::g_alpha[10][1], ETC_PAK::g_alpha[10][2], ETC_PAK::g_alpha[10][3], ETC_PAK::g_alpha[10][4], ETC_PAK::g_alpha[10][5], ETC_PAK::g_alpha[10][6], ETC_PAK::g_alpha[10][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[11][0], ETC_PAK::g_alpha[11][1], ETC_PAK::g_alpha[11][2], ETC_PAK::g_alpha[11][3], ETC_PAK::g_alpha[11][4], ETC_PAK::g_alpha[11][5], ETC_PAK::g_alpha[11][6], ETC_PAK::g_alpha[11][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[12][0], ETC_PAK::g_alpha[12][1], ETC_PAK::g_alpha[12][2], ETC_PAK::g_alpha[12][3], ETC_PAK::g_alpha[12][4], ETC_PAK::g_alpha[12][5], ETC_PAK::g_alpha[12][6], ETC_PAK::g_alpha[12][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[13][0], ETC_PAK::g_alpha[13][1], ETC_PAK::g_alpha[13][2], ETC_PAK::g_alpha[13][3], ETC_PAK::g_alpha[13][4], ETC_PAK::g_alpha[13][5], ETC_PAK::g_alpha[13][6], ETC_PAK::g_alpha[13][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[14][0], ETC_PAK::g_alpha[14][1], ETC_PAK::g_alpha[14][2], ETC_PAK::g_alpha[14][3], ETC_PAK::g_alpha[14][4], ETC_PAK::g_alpha[14][5], ETC_PAK::g_alpha[14][6], ETC_PAK::g_alpha[14][7]),
    _mm_setr_epi16(ETC_PAK::g_alpha[15][0], ETC_PAK::g_alpha[15][1], ETC_PAK::g_alpha[15][2], ETC_PAK::g_alpha[15][3], ETC_PAK::g_alpha[15][4], ETC_PAK::g_alpha[15][5], ETC_PAK::g_alpha[15][6], ETC_PAK::g_alpha[15][7]),
};

const __m128i ETC_PAK::g_alphaRange_SIMD = _mm_setr_epi16(
    ETC_PAK::g_alphaRange[0],
    ETC_PAK::g_alphaRange[1],
    ETC_PAK::g_alphaRange[4],
    ETC_PAK::g_alphaRange[5],
    ETC_PAK::g_alphaRange[8],
    ETC_PAK::g_alphaRange[14],
    0,
    0
);
#endif

#ifdef __AVX2__
const __m256i ETC_PAK::g_alpha_AVX[8] = {
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][0], ETC_PAK::g_alpha[1][0], ETC_PAK::g_alpha[2][0], ETC_PAK::g_alpha[3][0], ETC_PAK::g_alpha[4][0], ETC_PAK::g_alpha[5][0], ETC_PAK::g_alpha[6][0], ETC_PAK::g_alpha[7][0], ETC_PAK::g_alpha[8][0], ETC_PAK::g_alpha[9][0], ETC_PAK::g_alpha[10][0], ETC_PAK::g_alpha[11][0], ETC_PAK::g_alpha[12][0], ETC_PAK::g_alpha[13][0], ETC_PAK::g_alpha[14][0], ETC_PAK::g_alpha[15][0]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][1], ETC_PAK::g_alpha[1][1], ETC_PAK::g_alpha[2][1], ETC_PAK::g_alpha[3][1], ETC_PAK::g_alpha[4][1], ETC_PAK::g_alpha[5][1], ETC_PAK::g_alpha[6][1], ETC_PAK::g_alpha[7][1], ETC_PAK::g_alpha[8][1], ETC_PAK::g_alpha[9][1], ETC_PAK::g_alpha[10][1], ETC_PAK::g_alpha[11][1], ETC_PAK::g_alpha[12][1], ETC_PAK::g_alpha[13][1], ETC_PAK::g_alpha[14][1], ETC_PAK::g_alpha[15][1]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][2], ETC_PAK::g_alpha[1][2], ETC_PAK::g_alpha[2][2], ETC_PAK::g_alpha[3][2], ETC_PAK::g_alpha[4][2], ETC_PAK::g_alpha[5][2], ETC_PAK::g_alpha[6][2], ETC_PAK::g_alpha[7][2], ETC_PAK::g_alpha[8][2], ETC_PAK::g_alpha[9][2], ETC_PAK::g_alpha[10][2], ETC_PAK::g_alpha[11][2], ETC_PAK::g_alpha[12][2], ETC_PAK::g_alpha[13][2], ETC_PAK::g_alpha[14][2], ETC_PAK::g_alpha[15][2]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][3], ETC_PAK::g_alpha[1][3], ETC_PAK::g_alpha[2][3], ETC_PAK::g_alpha[3][3], ETC_PAK::g_alpha[4][3], ETC_PAK::g_alpha[5][3], ETC_PAK::g_alpha[6][3], ETC_PAK::g_alpha[7][3], ETC_PAK::g_alpha[8][3], ETC_PAK::g_alpha[9][3], ETC_PAK::g_alpha[10][3], ETC_PAK::g_alpha[11][3], ETC_PAK::g_alpha[12][3], ETC_PAK::g_alpha[13][3], ETC_PAK::g_alpha[14][3], ETC_PAK::g_alpha[15][3]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][4], ETC_PAK::g_alpha[1][4], ETC_PAK::g_alpha[2][4], ETC_PAK::g_alpha[3][4], ETC_PAK::g_alpha[4][4], ETC_PAK::g_alpha[5][4], ETC_PAK::g_alpha[6][4], ETC_PAK::g_alpha[7][4], ETC_PAK::g_alpha[8][4], ETC_PAK::g_alpha[9][4], ETC_PAK::g_alpha[10][4], ETC_PAK::g_alpha[11][4], ETC_PAK::g_alpha[12][4], ETC_PAK::g_alpha[13][4], ETC_PAK::g_alpha[14][4], ETC_PAK::g_alpha[15][4]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][5], ETC_PAK::g_alpha[1][5], ETC_PAK::g_alpha[2][5], ETC_PAK::g_alpha[3][5], ETC_PAK::g_alpha[4][5], ETC_PAK::g_alpha[5][5], ETC_PAK::g_alpha[6][5], ETC_PAK::g_alpha[7][5], ETC_PAK::g_alpha[8][5], ETC_PAK::g_alpha[9][5], ETC_PAK::g_alpha[10][5], ETC_PAK::g_alpha[11][5], ETC_PAK::g_alpha[12][5], ETC_PAK::g_alpha[13][5], ETC_PAK::g_alpha[14][5], ETC_PAK::g_alpha[15][5]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][6], ETC_PAK::g_alpha[1][6], ETC_PAK::g_alpha[2][6], ETC_PAK::g_alpha[3][6], ETC_PAK::g_alpha[4][6], ETC_PAK::g_alpha[5][6], ETC_PAK::g_alpha[6][6], ETC_PAK::g_alpha[7][6], ETC_PAK::g_alpha[8][6], ETC_PAK::g_alpha[9][6], ETC_PAK::g_alpha[10][6], ETC_PAK::g_alpha[11][6], ETC_PAK::g_alpha[12][6], ETC_PAK::g_alpha[13][6], ETC_PAK::g_alpha[14][6], ETC_PAK::g_alpha[15][6]),
    _mm256_setr_epi16(ETC_PAK::g_alpha[0][7], ETC_PAK::g_alpha[1][7], ETC_PAK::g_alpha[2][7], ETC_PAK::g_alpha[3][7], ETC_PAK::g_alpha[4][7], ETC_PAK::g_alpha[5][7], ETC_PAK::g_alpha[6][7], ETC_PAK::g_alpha[7][7], ETC_PAK::g_alpha[8][7], ETC_PAK::g_alpha[9][7], ETC_PAK::g_alpha[10][7], ETC_PAK::g_alpha[11][7], ETC_PAK::g_alpha[12][7], ETC_PAK::g_alpha[13][7], ETC_PAK::g_alpha[14][7], ETC_PAK::g_alpha[15][7]),
};

const __m256i ETC_PAK::g_alphaRange_AVX = _mm256_setr_epi16(
    ETC_PAK::g_alphaRange[0], ETC_PAK::g_alphaRange[1], ETC_PAK::g_alphaRange[2], ETC_PAK::g_alphaRange[3], ETC_PAK::g_alphaRange[4], ETC_PAK::g_alphaRange[5], ETC_PAK::g_alphaRange[6], ETC_PAK::g_alphaRange[7],
    ETC_PAK::g_alphaRange[8], ETC_PAK::g_alphaRange[9], ETC_PAK::g_alphaRange[10], ETC_PAK::g_alphaRange[11], ETC_PAK::g_alphaRange[12], ETC_PAK::g_alphaRange[13], ETC_PAK::g_alphaRange[14], ETC_PAK::g_alphaRange[15]
);
#endif

#ifdef __ARM_NEON
const int16x8_t ETC_PAK::g_table128_NEON[2] =
    {
        {2 * 128, 5 * 128,  9 * 128,  13 * 128, 18 * 128, 24 * 128, 33 * 128,  47 * 128 },
        {8 * 128, 17 * 128, 29 * 128, 42 * 128, 60 * 128, 80 * 128, 106 * 128, 183 * 128}
};

const int32x4_t ETC_PAK::g_table256_NEON[4] =
    {
        {2 * 256,  5 * 256,  9 * 256,   13 * 256 },
        {8 * 256,  17 * 256, 29 * 256,  42 * 256 },
        {18 * 256, 24 * 256, 33 * 256,  47 * 256 },
        {60 * 256, 80 * 256, 106 * 256, 183 * 256}
};

const int16x8_t ETC_PAK::g_alpha_NEON[16] =
    {
        {-3, -6, -9,  -15, 2, 5, 8, 14},
        {-3, -7, -10, -13, 2, 6, 9, 12},
        {-2, -5, -8,  -13, 1, 4, 7, 12},
        {-2, -4, -6,  -13, 1, 3, 5, 12},
        {-3, -6, -8,  -12, 2, 5, 7, 11},
        {-3, -7, -9,  -11, 2, 6, 8, 10},
        {-4, -7, -8,  -11, 3, 6, 7, 10},
        {-3, -5, -8,  -11, 2, 4, 7, 10},
        {-2, -6, -8,  -10, 1, 5, 7, 9 },
        {-2, -5, -8,  -10, 1, 4, 7, 9 },
        {-2, -4, -8,  -10, 1, 3, 7, 9 },
        {-2, -5, -7,  -10, 1, 4, 6, 9 },
        {-3, -4, -7,  -10, 2, 3, 6, 9 },
        {-1, -2, -3,  -10, 0, 1, 2, 9 },
        {-4, -6, -8,  -9,  3, 5, 7, 8 },
        {-3, -5, -7,  -9,  2, 4, 6, 8 }
};

const int16x8_t ETC_PAK::g_alphaRange_NEON =
    {
        (int16_t)ETC_PAK::g_alphaRange[0],
        (int16_t)ETC_PAK::g_alphaRange[1],
        (int16_t)ETC_PAK::g_alphaRange[4],
        (int16_t)ETC_PAK::g_alphaRange[5],
        (int16_t)ETC_PAK::g_alphaRange[8],
        (int16_t)ETC_PAK::g_alphaRange[14],
        0,
        0
};

#endif
