#pragma once
#include "../typedefs.h"
#include "../error.h"
#include <float.h>
#include <math.h>


DOME_NAMESPACE_BEGIN
namespace Math
{
    static const F32 k_F32Epsilon = FLT_EPSILON;
    static const F64 k_F64Epsilon = DBL_EPSILON;
    static const F32 k_F32Pi = 3.14159265359f;
    static const F64 k_F64Pi = 3.14159265359;

    inline F32 SquareRoot(F32 i_Val)
    {
        return sqrt(i_Val);
    }

    inline F64 SquareRoot(F64 i_Val)
    {
        return sqrt(i_Val);
    }

    inline Int SquareRoot(Int i_Val)
    {
        return (Int)sqrt(i_Val);
    }

    inline F32 Ceil(F32 i_Val)
    {
        return ceil(i_Val);
    }

    inline F64 Ceil(F64 i_Val)
    {
        return ceil(i_Val);
    }

    inline F32 Floor(F32 i_Val)
    {
        return floor(i_Val);
    }

    inline F64 Floor(F64 i_Val)
    {
        return floor(i_Val);
    }

    inline F32 Round(F32 i_Val)
    {
        //return round(i_Val);
        return i_Val < 0.0f ? Ceil(i_Val - 0.5f) : Floor(i_Val + 0.5f);
    }

    inline F64 Round(F64 i_Val)
    {
        //return round(i_Val);
        return i_Val < 0.0 ? Ceil(i_Val - 0.5) : Floor(i_Val + 0.5);
    }

    inline F32 Sin(F32 i_Rad)
    {
        return sinf(i_Rad);
    }

    inline F64 Sin(F64 i_Rad)
    {
        return sin(i_Rad);
    }

    inline Int Sin(Int i_Rad)
    {
        return (Int)sin(i_Rad);
    }

    inline F32 ArcSin(F32 i_Val)
    {
        return asinf(i_Val);
    }

    inline F64 ArcSin(F64 i_Val)
    {
        return asin(i_Val);
    }

    inline F32 Cos(F32 i_Rad)
    {
        return cosf(i_Rad);
    }

    inline F64 Cos(F64 i_Rad)
    {
        return cos(i_Rad);
    }

    inline Int Cos(Int i_Rad)
    {
        return (Int)cos(i_Rad);
    }

    inline F32 ArcCos(F32 i_Val)
    {
        return acosf(i_Val);
    }

    inline F64 ArcCos(F64 i_Val)
    {
        return acos(i_Val);
    }

    inline Int ArcCos(Int i_Val)
    {
        return (Int)acos(i_Val);
    }

    inline F32 Tan(F32 i_Val)
    {
        return tanf(i_Val);
    }

    inline F64 Tan(F64 i_Val)
    {
        return tan(i_Val);
    }

    inline F32 ArcTan(F32 i_Val)
    {
        return atanf(i_Val);
    }

    inline F64 ArcTan(F64 i_Val)
    {
        return atan(i_Val);
    }

    inline F32 ArcTan2(F32 y, F32 x)
    {
        return atan2f(y, x);
    }

    inline F64 ArcTan2(F64 y, F64 x)
    {
        return atan2(y, x);
    }

    inline Int ArcTan2(Int y, Int x)
    {
        return (Int)atan2(y, x);
    }

    inline F32 Power(F32 i_Base, F32 i_Exp)
    {
        return powf(i_Base, i_Exp);
    }

    inline F64 Power(F64 i_Base, F64 i_Exp)
    {
        return pow(i_Base, i_Exp);
    }

    inline F32 Log10(F32 i_Val)
    {
        return log10f(i_Val);
    }

    inline F64 Log10(F64 i_Val)
    {
        return log10(i_Val);
    }

    inline F32 Log(F32 i_Base, F32 i_Val)
    {
        return Log10(i_Val) / Log10(i_Base);
    }

    inline F64 Log(F64 i_Base, F64 i_Val)
    {
        return Log10(i_Val) / Log10(i_Base);
    }

    inline F32 Log2(F32 i_Val)
    {
        return Log(2.0f, i_Val);
    }

    inline F64 Log2(F64 i_Val)
    {
        return Log(2.0, i_Val);
    }

    inline Bool IsMultipleOf(Int i_Val, Int i_Divider)
    {
        return (i_Val / i_Divider * i_Divider) == i_Val;
    }

    template<class T>
    T Max(T v0, T v1)
    {
        if(v0 > v1)
            return v0;
        else
            return v1;
    }
    
    template<class T>
    T Min(T v0, T v1)
    {
        if(v0 < v1)
            return v0;
        else
            return v1;
    }
    
    template<class T>
    T Abs(T v)
    {
        if(v < 0)
            return -v;
        else
            return v;
    }
    
    template<class T>
    T Clamp(T v, T min, T max)
    {
        if(v < min)
            return min;
        else if(v > max)
            return max;
        else
            return v;
    }

	template<class T>
	Bool IsSame(T v0, T v1, T Epsilon)
	{
		if (Abs(v0 - v1) <= Epsilon)
			return DM_TRUE;
		else
			return DM_FALSE;
	}

    template<class T>
    void Swap(T& val0, T& val1)
    {
        T tmp = val0;
        val0 = val1;
        val1 = tmp;
    }

    // return max random integer number
    inline Int GetMaxRandom()
    {
        return RAND_MAX;
    }

    inline void SetRandomSeed(Int i_Seed)
    {
        srand((unsigned int) i_Seed);
    }

    // return a random integer number between [0, Random_Max]
    inline Int Random()
    {
        return rand();
    }

    // return a random integer number between [i_min, i_max)
    inline Int RandomInRange(Int i_Min/*inclusive*/, Int i_Max/*exclusive*/)
    {
        DOME_ASSERT(i_Min < i_Max);
        if (i_Min >= i_Max) return i_Min;
        return Random() % (i_Max - i_Min) + i_Min;
    }

    inline Int RandomInRangeInclusive(Int i_Min, Int i_Max)
    {
        DOME_ASSERT(i_Min < i_Max);
        if (i_Min >= i_Max) return i_Min;
        return Random() % (i_Max - i_Min + 1) + i_Min;
    }

    // return a random float number in range [0.0f, 1.0f]
    inline F32 RandomF32Normalize()
    {
        return (F32)Random() / GetMaxRandom();
    }

    // return a random float number in range [i_Min, i_Max]
    inline F32 RandomF32InRange(F32 i_Min, F32 i_Max)
    {
        DOME_ASSERT(i_Min < i_Max);
        return RandomF32Normalize() * (i_Max - i_Min) + i_Min;
    }

    inline Bool IsOdd(Int n)
    {
        return (n & 1) != 0;
    }

    inline Bool IsEven(Int n)
    {
        return (n & 1) == 0;
    }

    template<class FIRST, class SECOND>
    struct TPair
    {
        FIRST       first;
        SECOND      second;

        TPair()
        :first()
        ,second()
        {
        }

        TPair(const FIRST& i_First, const SECOND& i_Second)
        : first(i_First)
        , second(i_Second)
        {
        }

        TPair(const FIRST& i_First)
        : first(i_First)
        , second()
        {
        }

        TPair(const TPair& i_Other)
        :first(i_Other.first)
        ,second(i_Other.second)
        {
        }

        Bool operator > (const TPair& i_Other) const
        {
            return first > i_Other.first;
        }

        Bool operator < (const TPair& i_Other) const
        {
            return first < i_Other.first;
        }

        Bool operator == (const TPair& i_Other) const
        {
            return first == i_Other.first;
        }
    };

    template<class FIRST_T, class SECOND_T = FIRST_T>
    struct TCompare
    {
        static Bool Less(const FIRST_T& i_Val0, const SECOND_T& i_Val1)
        {
            return i_Val0 < i_Val1;
        }
        
        static Bool Greater(const FIRST_T& i_Val0, const SECOND_T& i_Val1)
        {
            return i_Val0 > i_Val1;
        }

        static Bool Equal(const FIRST_T& i_Val0, const SECOND_T& i_Val1)
        {
            return i_Val0 == i_Val1;
        }

        static Int Compare(const FIRST_T& i_Val0, const SECOND_T& i_Val1)
        {
            if(i_Val0 < i_Val1)
                return -1;
            else if(i_Val0 > i_Val1)
                return 1;
            else
                return 0;
        }
    };
}
DOME_NAMESPACE_END
