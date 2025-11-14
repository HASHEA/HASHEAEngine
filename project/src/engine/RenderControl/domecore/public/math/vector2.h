/*
    filename:       vector2.h
    author:         Ming Dong
    date:           2016-Feb-22
    description:    vector2 template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"


DOME_NAMESPACE_BEGIN

template<class TYPE>
class TVector2
{
public:
    TVector2()
    {
        x = y = (TYPE)0;
    }

    TVector2(TYPE i_Val)
    {
        x = y = i_Val;
    }

    TVector2(TYPE i_ValX, TYPE i_ValY)
    {
        x = i_ValX;
        y = i_ValY;
    }

    TVector2(const TVector2& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
    }

    ~TVector2()
    {

    }

    void set(TYPE i_ValX, TYPE i_ValY)
    {
        x = i_ValX;
        y = i_ValY;
    }

    void set(const TVector2& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
    }

    TVector2& copy(const TVector2& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector2& add(const TVector2& i_Other)
    {
        x += i_Other.x;
        y += i_Other.y;
        return *this;
    }

    TVector2 addTo(const TVector2& i_Other) const
    {
        TVector2 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TVector2& addTo(const TVector2& i_Other, TVector2* o_pResult) const
    {
        o_pResult->x = x + i_Other.x;
        o_pResult->y = y + i_Other.y;
        return *o_pResult;
    }

    TVector2& sub(const TVector2& i_Other)
    {
        x -= i_Other.x;
        y -= i_Other.y;
        return *this;
    }

    TVector2 subTo(const TVector2& i_Other) const
    {
        TVector2 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TVector2& subTo(const TVector2& i_Other, TVector2* o_pResult) const
    {
        o_pResult->x = x - i_Other.x;
        o_pResult->y = y - i_Other.y;
        return *o_pResult;
    }

    TYPE dotProduct(const TVector2& i_Other) const
    {
        return x * i_Other.x + y * i_Other.y;
    }

    TYPE length() const
    {
        return Math::SquareRoot(x * x + y * y);
    }

    TYPE lengthSq() const
    {
        return x * x + y * y;
    }

    TVector2& scale(TYPE i_Scale)
    {
        x *= i_Scale;
        y *= i_Scale;
        return *this;
    }

    void normalize()
    {
        scale((TYPE)1 / length());
    }

    void getMax(const TVector2& i_Other, TVector2* o_pResult) const
    {
        o_pResult->x = x > i_Other.x ? x : i_Other.x;
        o_pResult->y = y > i_Other.y ? y : i_Other.y;
    }

    TVector2 getMax(const TVector2& i_Other) const
    {
        TVector2 l_Result;
        getMax(i_Other, &l_Result);
        return l_Result;
    }

    void getMin(const TVector2& i_Other, TVector2* o_pResult) const
    {
        o_pResult->x = x < i_Other.x ? x : i_Other.x;
        o_pResult->y = y < i_Other.y ? y : i_Other.y;
    }

    TVector2 getMin(const TVector2& i_Other) const
    {
        TVector2 l_Result;
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
    Bool operator== (const TVector2& i_Other) const
    {
        return (x == i_Other.x) && (y == i_Other.y);
    }

    Bool operator!= (const TVector2& i_Other) const
    {
        return !(*this == i_Other);
    }

    TVector2& operator= (const TVector2& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector2& operator+= (const TVector2& i_Other)
    {
        add(i_Other);
        return *this;
    }

    TVector2& operator-= (const TVector2& i_Other)
    {
        sub(i_Other);
        return *this;
    }

    TVector2 operator+ (const TVector2& i_Other) const
    {
        TVector2 l_Result(*this);
        l_Result.add(i_Other);
        return l_Result;
    }

    TVector2 operator- (const TVector2& i_Other) const
    {
        TVector2 l_Result(*this);
        l_Result.sub(i_Other);
        return l_Result;
    }

    TVector2& operator*= (TYPE i_Scale)
    {
        scale(i_Scale);
        return *this;
    }

    TVector2& operator*= (const TVector2& i_Other)
    {
        x *= i_Other.x;
        y *= i_Other.y;
        return *this;
    }

    TVector2 operator* (TYPE i_Scale) const
    {
        TVector2 l_Result(*this);
        l_Result.scale(i_Scale);
        return l_Result;
    }

    TVector2 operator* (const TVector2& i_Other) const
    {
        TVector2 l_Result(*this);
        l_Result *= i_Other;
        return l_Result;
    }

    const TYPE& operator[] (Int i_Index) const
    {
        DOME_ASSERT2(i_Index >= 0 && i_Index <2, "Index out of range.");
        return (&x)[i_Index];
    }

    TYPE& operator[] (Int i_Index)
    {
        DOME_ASSERT2(i_Index >= 0 && i_Index <2, "Index out of range.");
        return (&x)[i_Index];
    }

public:
    TYPE        x;
    TYPE        y;
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TVector2<F32>;
//////template class DOME_CORE_API TVector2<F64>;
//////template class DOME_CORE_API TVector2<Int>;

typedef TVector2<F32>   DVector2f;
typedef TVector2<F64>   DVector2d;
typedef TVector2<Int>   DVector2i;

DOME_NAMESPACE_END