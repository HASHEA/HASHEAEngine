/*
    filename:       vector4.h
    author:         Ming Dong
    date:           2016-Feb-22
    description:    vector4 template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "vector2.h"
#include "vector3.h"


DOME_NAMESPACE_BEGIN

template<class TYPE>
class TVector4
{
public:
    TVector4()
    {
        x = y = z = w = (TYPE)0;
    }

    TVector4(TYPE i_Val)
    {
        x = y = z = w = i_Val;
    }

    TVector4(TYPE i_ValX, TYPE i_ValY, TYPE i_ValZ, TYPE i_ValW)
    {
        x = i_ValX;
        y = i_ValY;
        z = i_ValZ;
        w = i_ValW;
    }

    TVector4(const TVector4& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
        w = i_Other.w;
    }

    TVector4(const TVector3<TYPE>& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
        w = (TYPE)0;
    }

    TVector4(const TVector2<TYPE>& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = (TYPE)0;
        w = (TYPE)0;
    }

    ~TVector4()
    {

    }

    void set(TYPE i_ValX, TYPE i_ValY, TYPE i_ValZ, TYPE i_ValW)
    {
        x = i_ValX;
        y = i_ValY;
        z = i_ValZ;
        w = i_ValW;
    }

    void set(const TVector4& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
        w = i_Other.w;
    }

    void set(const TVector3<TYPE>& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
        w = (TYPE)0;
    }

    void set(const TVector2<TYPE>& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = (TYPE)0;
        w = (TYPE)0;
    }

    TVector4& copy(const TVector4& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector4& add(const TVector4& i_Other)
    {
        x += i_Other.x;
        y += i_Other.y;
        z += i_Other.z;
        w += i_Other.w;
        return *this;
    }

    TVector4 addTo(const TVector4& i_Other) const
    {
        TVector4 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TVector4& addTo(const TVector4& i_Other, TVector4* o_pResult) const
    {
        o_pResult->x = x + i_Other.x;
        o_pResult->y = y + i_Other.y;
        o_pResult->z = z + i_Other.z;
        o_pResult->w = w + i_Other.w;
        return *o_pResult;
    }

    TVector4& sub(const TVector4& i_Other)
    {
        x -= i_Other.x;
        y -= i_Other.y;
        z -= i_Other.z;
        w -= i_Other.w;
        return *this;
    }

    TVector4 subTo(const TVector4& i_Other) const
    {
        TVector4 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TVector4& subTo(const TVector4& i_Other, TVector4* o_pResult) const
    {
        o_pResult->x = x - i_Other.x;
        o_pResult->y = y - i_Other.y;
        o_pResult->z = z - i_Other.z;
        o_pResult->w = w - i_Other.w;
        return *o_pResult;
    }

    TYPE dotProduct(const TVector4& i_Other) const
    {
        return x * i_Other.x + y * i_Other.y + z * i_Other.z + w * i_Other.w;
    }

    TYPE length() const
    {
        return Math::SquareRoot(x * x + y * y + z * z + w * w);
    }

    TYPE lengthSq() const
    {
        return x * x + y * y + z * z + w * w;
    }

    TVector4& scale(TYPE i_Scale)
    {
        x *= i_Scale;
        y *= i_Scale;
        z *= i_Scale;
        w *= i_Scale;
        return *this;
    }

    void normalize()
    {
        scale((TYPE)1 / length());
    }

    void getMax(const TVector4& i_Other, TVector4* o_pResult) const
    {
        o_pResult->x = x > i_Other.x ? x : i_Other.x;
        o_pResult->y = y > i_Other.y ? y : i_Other.y;
        o_pResult->z = z > i_Other.z ? z : i_Other.z;
        o_pResult->w = w > i_Other.w ? w : i_Other.w;
    }

    TVector4 getMax(const TVector4& i_Other) const
    {
        TVector4 l_Result;
        getMax(i_Other, &l_Result);
        return l_Result;
    }

    void getMin(const TVector4& i_Other, TVector4* o_pResult) const
    {
        o_pResult->x = x < i_Other.x ? x : i_Other.x;
        o_pResult->y = y < i_Other.y ? y : i_Other.y;
        o_pResult->z = z < i_Other.z ? z : i_Other.z;
        o_pResult->w = w < i_Other.w ? w : i_Other.w;
    }

    TVector4 getMin(const TVector4& i_Other) const
    {
        TVector4 l_Result;
        getMin(i_Other, &l_Result);
        return l_Result;
    }

    const TYPE* getBuffer() const
    {
        return &x;
    }

    TYPE* getBuffer()
    {
        return &x;
    }

    // operator overload
    Bool operator== (const TVector4& i_Other) const
    {
        return (x == i_Other.x) && (y == i_Other.y) && (z == i_Other.z) && (w == i_Other.w);
    }

    Bool operator!= (const TVector4& i_Other) const
    {
        return !(*this == i_Other);
    }

    TVector4& operator= (const TVector4& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector4& operator= (const TVector3<TYPE>& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector4& operator= (const TVector2<TYPE>& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector4& operator+= (const TVector4& i_Other)
    {
        add(i_Other);
        return *this;
    }

    TVector4& operator-= (const TVector4& i_Other)
    {
        sub(i_Other);
        return *this;
    }

    TVector4 operator+ (const TVector4& i_Other) const
    {
        TVector4 l_Result(*this);
        l_Result.add(i_Other);
        return l_Result;
    }

    TVector4 operator- (const TVector4& i_Other) const
    {
        TVector4 l_Result(*this);
        l_Result.sub(i_Other);
        return l_Result;
    }

    TVector4& operator*= (TYPE i_Scale)
    {
        scale(i_Scale);
        return *this;
    }

    TVector4& operator*= (const TVector4& i_Other)
    {
        x *= i_Other.x;
        y *= i_Other.y;
        z *= i_Other.z;
        w *= i_Other.w;
        return *this;
    }

    TVector4 operator* (TYPE i_Scale) const
    {
        TVector4 l_Result(*this);
        l_Result.scale(i_Scale);
        return l_Result;
    }

    TVector4 operator* (const TVector4& i_Other) const
    {
        TVector4 l_Result(*this);
        l_Result *= i_Other;
        return l_Result;
    }

    const TYPE& operator[] (Int i_Index) const
    {
        DOME_ASSERT2(i_Index >= 0 && i_Index <4, "Index out of range.");
        return (&x)[i_Index];
    }

    TYPE& operator[] (Int i_Index)
    {
        DOME_ASSERT2(i_Index >= 0 && i_Index <4, "Index out of range.");
        return (&x)[i_Index];
    }

    const TVector2<TYPE>& vector2() const
    {
        return *((TVector2<TYPE>*)&x);
    }

    TVector2<TYPE>& vector2()
    {
        return *((TVector2<TYPE>*)&x);
    }

    const TVector3<TYPE>& vector3() const
    {
        return *((TVector3<TYPE>*)&x);
    }

    TVector3<TYPE>& vector3()
    {
        return *((TVector3<TYPE>*)&x);
    }

public:
    TYPE        x;
    TYPE        y;
    TYPE        z;
    TYPE        w;
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TVector4<F32>;
//////template class DOME_CORE_API TVector4<F64>;
//////template class DOME_CORE_API TVector4<Int>;

typedef TVector4<F32>   DVector4f;
typedef TVector4<F64>   DVector4d;
typedef TVector4<Int>   DVector4i;

DOME_NAMESPACE_END