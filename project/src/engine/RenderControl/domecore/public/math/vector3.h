/*
    filename:       vector3.h
    author:         Ming Dong
    date:           2016-Feb-22
    description:    vector3 template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "vector2.h"


DOME_NAMESPACE_BEGIN

template<class TYPE>
class TVector3
{
public:
    TVector3()
    {
        x = y = z = (TYPE)0;
    }

    TVector3(TYPE i_Val)
    {
        x = y = z = i_Val;
    }

    TVector3(TYPE i_ValX, TYPE i_ValY, TYPE i_ValZ)
    {
        x = i_ValX;
        y = i_ValY;
        z = i_ValZ;
    }

    TVector3(const TVector3& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
    }

    TVector3(const TVector2<TYPE>& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = (TYPE)0;
    }

    ~TVector3()
    {

    }

    void set(TYPE i_ValX, TYPE i_ValY, TYPE i_ValZ)
    {
        x = i_ValX;
        y = i_ValY;
        z = i_ValZ;
    }

    void set(const TVector3& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
    }

    TVector3& copy(const TVector3& i_Other)
    {
        set(i_Other);
        return *this;
    }

    void set(const TVector2<TYPE>& i_Other)
    {
        x = i_Other.x;
        y = i_Other.y;
        z = (TYPE)0;
    }

    TVector3& add(const TVector3& i_Other)
    {
        x += i_Other.x;
        y += i_Other.y;
        z += i_Other.z;
        return *this;
    }

    TVector3 addTo(const TVector3& i_Other) const
    {
        TVector3 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TVector3& addTo(const TVector3& i_Other, TVector3* o_pResult) const
    {
        o_pResult->x = x + i_Other.x;
        o_pResult->y = y + i_Other.y;
        o_pResult->z = z + i_Other.z;
        return *o_pResult;
    }

    TVector3& sub(const TVector3& i_Other)
    {
        x -= i_Other.x;
        y -= i_Other.y;
        z -= i_Other.z;
        return *this;
    }

    TVector3 subTo(const TVector3& i_Other) const
    {
        TVector3 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TVector3& subTo(const TVector3& i_Other, TVector3* o_pResult) const
    {
        o_pResult->x = x - i_Other.x;
        o_pResult->y = y - i_Other.y;
        o_pResult->z = z - i_Other.z;
        return *o_pResult;
    }

    TYPE dotProduct(const TVector3& i_Other) const
    {
        return x * i_Other.x + y * i_Other.y + z * i_Other.z;
    }

    TYPE length() const
    {
        return Math::SquareRoot(x * x + y * y + z * z);
    }

    TYPE lengthSq() const
    {
        return x * x + y * y + z * z;
    }

    TVector3& scale(TYPE i_Scale)
    {
        x *= i_Scale;
        y *= i_Scale;
        z *= i_Scale;
        return *this;
    }

    void normalize()
    {
        scale((TYPE)1 / length());
    }

    void getMax(const TVector3& i_Other, TVector3* o_pResult) const
    {
        o_pResult->x = x > i_Other.x ? x : i_Other.x;
        o_pResult->y = y > i_Other.y ? y : i_Other.y;
        o_pResult->z = z > i_Other.z ? z : i_Other.z;
    }

    TVector3 getMax(const TVector3& i_Other) const
    {
        TVector3 l_Result;
        getMax(i_Other, &l_Result);
        return l_Result;
    }

    void getMin(const TVector3& i_Other, TVector3* o_pResult) const
    {
        o_pResult->x = x < i_Other.x ? x : i_Other.x;
        o_pResult->y = y < i_Other.y ? y : i_Other.y;
        o_pResult->z = z < i_Other.z ? z : i_Other.z;
    }

    TVector3 getMin(const TVector3& i_Other) const
    {
        TVector3 l_Result;
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
    Bool operator== (const TVector3& i_Other) const
    {
        return (x == i_Other.x) && (y == i_Other.y) && (z == i_Other.z);
    }

    Bool operator!= (const TVector3& i_Other) const
    {
        return !(*this == i_Other);
    }

    TVector3& operator= (const TVector3& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector3& operator= (const TVector2<TYPE>& i_Other)
    {
        set(i_Other);
        return *this;
    }

    TVector3& operator+= (const TVector3& i_Other)
    {
        add(i_Other);
        return *this;
    }

    TVector3& operator-= (const TVector3& i_Other)
    {
        sub(i_Other);
        return *this;
    }

    TVector3 operator+ (const TVector3& i_Other) const
    {
        TVector3 l_Result(*this);
        l_Result.add(i_Other);
        return l_Result;
    }

    TVector3 operator- (const TVector3& i_Other) const
    {
        TVector3 l_Result(*this);
        l_Result.sub(i_Other);
        return l_Result;
    }

    TVector3& operator*= (TYPE i_Scale)
    {
        scale(i_Scale);
        return *this;
    }

    TVector3& operator*= (const TVector3& i_Other)
    {
        x *= i_Other.x;
        y *= i_Other.y;
        z *= i_Other.z;
        return *this;
    }

    TVector3 operator* (TYPE i_Scale) const
    {
        TVector3 l_Result(*this);
        l_Result.scale(i_Scale);
        return l_Result;
    }

    TVector3 operator* (const TVector3& i_Other) const
    {
        TVector3 l_Result(*this);
        l_Result *= i_Other;
        return l_Result;
    }

    TVector3& crossTo(const TVector3& i_Other, TVector3* o_pResult) const
    {
        o_pResult->x = y * i_Other.z - z * i_Other.y;
        o_pResult->y = z * i_Other.x - x * i_Other.z;
        o_pResult->z = x * i_Other.y - y * i_Other.x;
        return *o_pResult;
    }

    TVector3 crossTo(const TVector3& i_Other) const
    {
        TVector3 l_Result;
        crossTo(i_Other, &l_Result);
        return l_Result;
    }

    const TYPE& operator[] (Int i_Index) const
    {
        DOME_ASSERT2(i_Index >= 0 && i_Index <3, "Index out of range.");
        return (&x)[i_Index];
    }

    TYPE& operator[] (Int i_Index)
    {
        DOME_ASSERT2(i_Index >= 0 && i_Index <3, "Index out of range.");
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

public:
    TYPE        x;
    TYPE        y;
    TYPE        z;
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TVector3<F32>;
//////template class DOME_CORE_API TVector3<F64>;
//////template class DOME_CORE_API TVector3<Int>;

typedef TVector3<F32>   DVector3f;
typedef TVector3<F64>   DVector3d;
typedef TVector3<Int>   DVector3i;

DOME_NAMESPACE_END