/*
    filename:       matrix2x2.h
    author:         Ming Dong
    date:           2016-Feb-23
    description:    matrix2x2 template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "vector4.h"

DOME_NAMESPACE_BEGIN

/*
    if DOME_MATRIX_ROWMAJOR is defined
        | m0  m1 |
        | m2  m3 |
    else
        | m0  m2 |
        | m1  m3 |
*/
template<class TYPE>
class TMatrix2x2
{
public:
    const static Int k_NumRow = 2;
    const static Int k_NumColumn = 2;
    const static Int k_NumElement = k_NumRow * k_NumColumn;
    static const TMatrix2x2& Zero()
    {
        static TYPE s_Zero[] = {
            (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)0
        };
        return *((TMatrix2x2*)s_Zero);
    };
    static const TMatrix2x2& Identity()
    {
        static TYPE s_Identity[] = {
            (TYPE)1, (TYPE)0, 
            (TYPE)0, (TYPE)1
        };
        return *((TMatrix2x2*)s_Identity);
    };
protected:
    inline const TYPE& _M(Int row, Int column) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return m[row * k_NumColumn + column];
#else
        return m[column * k_NumRow + row];
#endif
    }
    inline TYPE& _M(Int row, Int column)
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return m[row * k_NumColumn + column];
#else
        return m[column * k_NumRow + row];
#endif
    }
    inline const TYPE& _M(Int index) const
    {
        return m[index];
    }
    inline TYPE& _M(Int index)
    {
        return m[index];
    }
public:
    inline const TYPE& M(Int row, Int column) const
    {
        DOME_ASSERT2(row >= 0 && row < k_NumRow && column >= 0 && column < k_NumColumn, 
            "row & column are out of range!");
        return _M(row, column);
    }
    inline TYPE& M(Int row, Int column)
    {
        DOME_ASSERT2(row >= 0 && row < k_NumRow && column >= 0 && column < k_NumColumn, 
            "row & column are out of range!");
        return _M(row, column);
    }
    inline const TYPE& M(Int index) const
    {
        DOME_ASSERT2(index >= 0 && index < k_NumElement, "index is out of range!");
        return _M(index);
    }
    inline TYPE& M(Int index)
    {
        DOME_ASSERT2(index >= 0 && index < k_NumElement, "index is out of range!");
        return _M(index);
    }
    inline TVector2<TYPE>& axisX()
    {
        return *((TVector2<TYPE>*)&_M(0));
    }
    inline const TVector2<TYPE>& axisX() const
    {
        return *((TVector2<TYPE>*)&_M(0));
    }
    inline TVector2<TYPE>& axisY()
    {
        return *((TVector2<TYPE>*)&_M(2));
    }
    inline const TVector2<TYPE>& axisY() const
    {
        return *((TVector2<TYPE>*)&_M(2));
    }

public:
    TMatrix2x2()
    {
        // do nothing here for performance optimization
    }

    TMatrix2x2(const TMatrix2x2& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) = i_Other._M(i);
    }

    ~TMatrix2x2()
    {

    }

    TMatrix2x2& copy(const TMatrix2x2& i_Other)
    {
        for (Int i = 0; i < k_NumElement; ++i)
        {
            _M(i) = i_Other._M(i);
        }
        return *this;
    }

    TMatrix2x2& add(const TMatrix2x2& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) += i_Other._M(i);
        return *this;
    }

    TMatrix2x2 addTo(const TMatrix2x2& i_Other) const
    {
        TMatrix2x2 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TMatrix2x2& addTo(const TMatrix2x2& i_Other, TMatrix2x2* o_pResult) const
    {
        for(Int i = 0; i < k_NumElement; ++i)
            o_pResult->_M(i) = _M(i) + i_Other._M(i);
        return *o_pResult;
    }

    TMatrix2x2& sub(const TMatrix2x2& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) -= i_Other._M(i);
        return *this;
    }

    TMatrix2x2 subTo(const TMatrix2x2& i_Other) const
    {
        TMatrix2x2 l_Result(*this);
        return l_Result.sub(i_Other);
    }

    TMatrix2x2& subTo(const TMatrix2x2& i_Other, TMatrix2x2* o_pResult) const
    {
        for(Int i = 0; i < k_NumElement; ++i)
            o_pResult->_M(i) = _M(i) - i_Other._M(i);
        return *o_pResult;
    }

    TMatrix2x2& mulTo(const TMatrix2x2& i_Other, TMatrix2x2* o_pResult) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        o_pResult->m[0] = m[0] * i_Other.m[0] + m[1] * i_Other.m[2];
        o_pResult->m[1] = m[0] * i_Other.m[1] + m[1] * i_Other.m[3];
        o_pResult->m[2] = m[2] * i_Other.m[0] + m[3] * i_Other.m[2];
        o_pResult->m[0] = m[2] * i_Other.m[1] + m[3] * i_Other.m[3];
#else
        o_pResult->m[0] = m[0] * i_Other.m[0] + m[2] * i_Other.m[1];
        o_pResult->m[1] = m[0] * i_Other.m[2] + m[2] * i_Other.m[3];
        o_pResult->m[2] = m[1] * i_Other.m[0] + m[3] * i_Other.m[1];
        o_pResult->m[0] = m[1] * i_Other.m[2] + m[3] * i_Other.m[3];
#endif
        return *o_pResult;
    }

    TMatrix2x2 mulTo(const TMatrix2x2& i_Other) const
    {
        TMatrix2x2 l_Result;
        return mulTo(i_Other, &l_Result);
    }

    TMatrix2x2& mul(const TMatrix2x2& i_Other)
    {
        TMatrix2x2 l_Result;
        mulTo(i_Other, &l_Result);
        copy(l_Result);
        return *this;
    }

    TMatrix2x2& mul(TYPE i_Scale)
    {
        for (Int i = 0; i < k_NumElement; ++i)
        {
            _M(i) *= i_Scale;
        }
        return *this;
    }

    TYPE deterninant() const
    {
        return _M(0) * _M(3) - _M(1) * _M(2);
    }

    TMatrix2x2&  transposeTo(TMatrix2x2* o_pResult) const
    {
        for (Int row = 0; row < k_NumRow; ++row)
        {
            for (Int column = 0; column < k_NumColumn; ++column)
            {
                o_pResult->_M(row, column) = _M(column, row);
            }
        }
        return *o_pResult;
    }

    TMatrix2x2 transposeTo() const
    {
        TMatrix2x2 l_Result;
        transposeTo(&l_Result);
        return l_Result;
    }

    TMatrix2x2& transpose()
    {
        for (Int row = 1; row < k_NumRow; ++row)
        {
            for (Int column = 0; column < row; ++column)
            {
                Math::Swap(_M(row, column), _M(column, row));
            }
        }
        return *this;
    }

    TYPE minor(Int row, Int column) const
    {
        Int r = row == 0 ? 1 : 0;
        Int c = column == 0 ? 1 : 0;
        return _M(r, c);
    }

    TYPE cofactor(Int row, Int column) const
    {
        return Math::IsOdd(row + column) ? (-minor(row, column)) : minor(row, column);
    }

    TMatrix2x2& inverseTo(TMatrix2x2* o_pResult) const
    {
        for (Int row = 0; row < k_NumRow; ++row)
        {
            for (Int column = 0; column < k_NumColumn; ++column)
            {
                o_pResult->_M(row, column) = cofactor(row, column);
            }
        }
        o_pResult->transpose();
        o_pResult->mul((TYPE)1 / deterninant());
        return *o_pResult;
    }

    TMatrix2x2 inverseTo() const
    {
        TMatrix2x2 l_Result;
        inverseTo(&l_Result);
        return l_Result;
    }

    TMatrix2x2& inverse()
    {
        TMatrix2x2 l_Result;
        inverseTo(&l_Result);
        return copy(l_Result);
    }

    TMatrix2x2& buildScale(TYPE i_Scale)
    {
        for (Int row = 0; row < k_NumRow; ++row)
        {
            for (Int column = 0; column < k_NumColumn; ++column)
            {
                if(row == column)
                    _M(row, column) = i_Scale;
                else
                    _M(row, column) = (TYPE)0;
            }
        }
        return *this;
    }

    TMatrix2x2& buildRotate(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX().set(l_Cos, l_Sin);
        axisY().set(-l_Sin, l_Cos);
        return *this;
    }

    TVector2<TYPE>& mulVector(const TVector2<TYPE>& i_Vector, TVector2<TYPE>* o_pResult) const
    {
        o_pResult->x = _M(0,0) * i_Vector.x + _M(0,1) * i_Vector.y;
        o_pResult->y = _M(1,0) * i_Vector.x + _M(1,1) * i_Vector.y;
        return *o_pResult;
    }

    TVector2<TYPE> mulVector(const TVector2<TYPE>& i_Vector) const
    {
        TVector2<TYPE> l_Result;
        mulVector(i_Vector, &l_Result);
        return l_Result;
    }

    TVector2<TYPE>& vectorMul(const TVector2<TYPE>& i_Vector, TVector2<TYPE>* o_pResult) const
    {
        o_pResult->x = i_Vector.x * _M(0,0) + i_Vector.y * _M(1,0);
        o_pResult->y = i_Vector.x * _M(0,1) + i_Vector.y * _M(1,1);
        return * o_pResult;
    }

    TVector2<TYPE> vectorMul(const TVector2<TYPE>& i_Vector) const
    {
        TVector2<TYPE> l_Result;
        vectorMul(i_Vector, &l_Result);
        return l_Result;
    }

    TVector2<TYPE>& transformVector(const TVector2<TYPE>& i_Vector, TVector2<TYPE>* o_pResult) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return vectorMul(i_Vector, o_pResult);
#else
        return mulVector(i_Vector, o_pResult);
#endif
    }

    TVector2<TYPE> transformVector(const TVector2<TYPE>& i_Vector) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return vectorMul(i_Vector);
#else
        return mulVector(i_Vector);
#endif
    }

    // operator overload
    TMatrix2x2& operator= (const TMatrix2x2& i_Other)
    {
        copy(i_Other);
        return *this;
    }

    TMatrix2x2& operator+= (const TMatrix2x2& i_Other)
    {
        add(i_Other);
        return *this;
    }

    TMatrix2x2 operator+ (const TMatrix2x2& i_Other) const
    {
        TMatrix2x2 l_Result(*this);
        l_Result.add(i_Other);
        return l_Result;
    }

    TMatrix2x2& operator-= (const TMatrix2x2& i_Other)
    {
        sub(i_Other);
        return *this;
    }

    TMatrix2x2 operator- (const TMatrix2x2& i_Other) const
    {
        TMatrix2x2 l_Result(*this);
        l_Result.sub(i_Other);
        return l_Result;
    }

    TMatrix2x2& operator*= (const TMatrix2x2& i_Other)
    {
        mul(i_Other);
        return *this;
    }

    TMatrix2x2 operator* (const TMatrix2x2& i_Other) const
    {
        TMatrix2x2 l_Result(*this);
        l_Result.mul(i_Other);
        return l_Result;
    }

public:
    TYPE        m[k_NumElement];
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TMatrix2x2<F32>;
//////template class DOME_CORE_API TMatrix2x2<F64>;
//////template class DOME_CORE_API TMatrix2x2<Int>;

typedef TMatrix2x2<F32>     DMatrix2x2f;
typedef TMatrix2x2<F64>     DMatrix2x2d;
typedef TMatrix2x2<Int>     DMatrix2x2i;

DOME_NAMESPACE_END