/*
    filename:       matrix4x4.h
    author:         Ming Dong
    date:           2016-Feb-24
    description:    matrix4x4 template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "matrix3x3.h"

DOME_NAMESPACE_BEGIN

/*
    if DOME_MATRIX_ROWMAJOR is defined
        | m0  m1  m2  m3 |
        | m4  m5  m6  m7 |
        | m8  m9  m10 m11|
        | m12 m13 m14 m15|
    else
        | m0  m4  m8  m12|
        | m1  m5  m9  m13|
        | m2  m6  m10 m14|
        | m3  m7  m11 m15|
*/
template<class TYPE>
class TMatrix4x4
{
public:
    const static Int k_NumRow = 4;
    const static Int k_NumColumn = 4;
    const static Int k_NumElement = k_NumRow * k_NumColumn;
    static const TMatrix4x4& Zero()
    {
        static TYPE s_Zero[] = {
            (TYPE)0, (TYPE)0, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)0, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)0, (TYPE)0, (TYPE)0,
            (TYPE)0, (TYPE)0, (TYPE)0, (TYPE)0
        };
        return *((TMatrix4x4*)s_Zero);
    };
    static const TMatrix4x4& Identity()
    {
        static TYPE s_Identity[] = {
            (TYPE)1, (TYPE)0, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)1, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)0, (TYPE)1, (TYPE)0,
            (TYPE)0, (TYPE)0, (TYPE)0, (TYPE)1
        };
        return *((TMatrix4x4*)s_Identity);
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
    inline TVector3<TYPE>& axisX()
    {
        return *((TVector3<TYPE>*)&_M(0));
    }
    inline const TVector3<TYPE>& axisX() const
    {
        return *((TVector3<TYPE>*)&_M(0));
    }
    inline TVector3<TYPE>& axisY()
    {
        return *((TVector3<TYPE>*)&_M(4));
    }
    inline const TVector3<TYPE>& axisY() const
    {
        return *((TVector3<TYPE>*)&_M(4));
    }
    inline TVector3<TYPE>& axisZ()
    {
        return *((TVector3<TYPE>*)&_M(8));
    }
    inline const TVector3<TYPE>& axisZ() const
    {
        return *((TVector3<TYPE>*)&_M(8));
    }
    inline const TVector3<TYPE>& translation() const
    {
        return *((TVector3<TYPE>*)&_M(12));
    }
    inline TVector3<TYPE>& translation()
    {
        return *((TVector3<TYPE>*)&_M(12));
    }

    inline TVector4<TYPE>& axisX4()
    {
        return *((TVector4<TYPE>*)&_M(0));
    }
    inline const TVector4<TYPE>& axisX4() const
    {
        return *((TVector4<TYPE>*)&_M(0));
    }
    inline TVector4<TYPE>& axisY4()
    {
        return *((TVector4<TYPE>*)&_M(4));
    }
    inline const TVector4<TYPE>& axisY4() const
    {
        return *((TVector4<TYPE>*)&_M(4));
    }
    inline TVector4<TYPE>& axisZ4()
    {
        return *((TVector4<TYPE>*)&_M(8));
    }
    inline const TVector4<TYPE>& axisZ4() const
    {
        return *((TVector4<TYPE>*)&_M(8));
    }
    inline const TVector4<TYPE>& translation4() const
    {
        return *((TVector4<TYPE>*)&_M(12));
    }
    inline TVector4<TYPE>& translation4()
    {
        return *((TVector4<TYPE>*)&_M(12));
    }

public:
    TMatrix4x4()
    {
        // do nothing here for performance optimization
    }

    TMatrix4x4(const TMatrix4x4& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) = i_Other._M(i);
    }

    ~TMatrix4x4()
    {

    }

    TMatrix4x4& copy(const TMatrix4x4& i_Other)
    {
        for (Int i = 0; i < k_NumElement; ++i)
        {
            _M(i) = i_Other._M(i);
        }
        return *this;
    }

    TMatrix4x4& add(const TMatrix4x4& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) += i_Other._M(i);
        return *this;
    }

    TMatrix4x4 addTo(const TMatrix4x4& i_Other) const
    {
        TMatrix4x4 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TMatrix4x4& addTo(const TMatrix4x4& i_Other, TMatrix4x4* o_pResult) const
    {
        for(Int i = 0; i < k_NumElement; ++i)
            o_pResult->_M(i) = _M(i) + i_Other._M(i);
        return *o_pResult;
    }

    TMatrix4x4& sub(const TMatrix4x4& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) -= i_Other._M(i);
        return *this;
    }

    TMatrix4x4 subTo(const TMatrix4x4& i_Other) const
    {
        TMatrix4x4 l_Result(*this);
        return l_Result.sub(i_Other);
    }

    TMatrix4x4& subTo(const TMatrix4x4& i_Other, TMatrix4x4* o_pResult) const
    {
        for(Int i = 0; i < k_NumElement; ++i)
            o_pResult->_M(i) = _M(i) - i_Other._M(i);
        return *o_pResult;
    }

    TMatrix4x4& mulTo(const TMatrix4x4& i_Other, TMatrix4x4* o_pResult) const
    {
        for (Int row = 0; row < k_NumRow; ++row)
        {
            for (Int col = 0; col < k_NumColumn; ++col)
            {
                o_pResult->_M(row, col) = 
                    _M(row, 0) * i_Other._M(0, col) + 
                    _M(row, 1) * i_Other._M(1, col) + 
                    _M(row, 2) * i_Other._M(2, col) +
                    _M(row, 3) * i_Other._M(3, col);
            }
        }

        return *o_pResult;
    }

    TMatrix4x4 mulTo(const TMatrix4x4& i_Other) const
    {
        TMatrix4x4 l_Result;
        return mulTo(i_Other, &l_Result);
    }

    TMatrix4x4& mul(const TMatrix4x4& i_Other)
    {
        TMatrix4x4 l_Result;
        mulTo(i_Other, &l_Result);
        copy(l_Result);
        return *this;
    }

    TMatrix4x4& mul(TYPE i_Scale)
    {
        for (Int i = 0; i < k_NumElement; ++i)
        {
            _M(i) *= i_Scale;
        }
        return *this;
    }

    TMatrix3x3<TYPE>& buildMinorMatrix(Int row, Int column, TMatrix3x3<TYPE>* o_pResult) const
    {
        for (Int r = 0; r < k_NumRow; ++r)
        {
            for (Int c = 0; c < k_NumColumn; ++c)
            {
                if(r == row || c == column)
                    continue;
                Int i = r < row ? r : (r - 1);
                Int j = c < column ? c : (c - 1);
                o_pResult->M(i, j) = _M(r, c);
            }
        }
        return *o_pResult;
    }

    TYPE deterninant() const
    {
        return _M(0,0) * minor(0,0) -
               _M(0,1) * minor(0,1) + 
               _M(0,2) * minor(0,2) -
               _M(0,3) * minor(0,3);
    }

    TMatrix4x4&  transposeTo(TMatrix4x4* o_pResult) const
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

    TMatrix4x4 transposeTo() const
    {
        TMatrix4x4 l_Result;
        transposeTo(&l_Result);
        return l_Result;
    }

    TMatrix4x4& transpose()
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
        TMatrix3x3<TYPE> l_MinorMtx;
        buildMinorMatrix(row, column, &l_MinorMtx);
        return l_MinorMtx.deterninant();
    }

    TYPE cofactor(Int row, Int column) const
    {
        return Math::IsOdd(row + column) ? (-minor(row, column)) : minor(row, column);
    }

    TMatrix4x4& inverseTo(TMatrix4x4* o_pResult) const
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

    TMatrix4x4 inverseTo() const
    {
        TMatrix4x4 l_Result;
        inverseTo(&l_Result);
        return l_Result;
    }

    TMatrix4x4& inverse()
    {
        TMatrix4x4 l_Result;
        inverseTo(&l_Result);
        return copy(l_Result);
    }

    TMatrix4x4& buildScale(TYPE i_Scale)
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

    TMatrix4x4& buildRotateX(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX4().      set((TYPE)1, (TYPE)0, (TYPE)0, (TYPE)0);
        axisY4().      set((TYPE)0, l_Cos,   l_Sin  , (TYPE)0);
        axisZ4().      set((TYPE)0, -l_Sin,  l_Cos  , (TYPE)0);
        translation4().set((TYPE)0, (TYPE)0, (TYPE)0, (TYPE)1);
        return *this;
    }

    TMatrix4x4& buildRotateY(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX4().      set(l_Cos,   (TYPE)0, -l_Sin , (TYPE)0);
        axisY4().      set((TYPE)0, (TYPE)1, (TYPE)0, (TYPE)0);
        axisZ4().      set(l_Sin,   (TYPE)0, l_Cos  , (TYPE)0);
        translation4().set((TYPE)0, (TYPE)0, (TYPE)0, (TYPE)1);
        return *this;
    }

    TMatrix4x4& buildRotateZ(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX4().      set(l_Cos,   l_Sin,   (TYPE)0, (TYPE)0);
        axisY4().      set(-l_Sin,  l_Cos,   (TYPE)0, (TYPE)0);
        axisZ4().      set((TYPE)0, (TYPE)0, (TYPE)1, (TYPE)0);
        translation4().set((TYPE)0, (TYPE)0, (TYPE)0, (TYPE)1);
        return *this;
    }

    TMatrix4x4& buildTranslation(TYPE x, TYPE y, TYPE z)
    {
        axisX4().      set((TYPE)1, (TYPE)0, (TYPE)0, (TYPE)0);
        axisY4().      set((TYPE)0, (TYPE)1, (TYPE)0, (TYPE)0);
        axisZ4().      set((TYPE)0, (TYPE)0, (TYPE)1, (TYPE)0);
        translation4().set((TYPE)x, (TYPE)y, (TYPE)z, (TYPE)1);
        return *this;
    }

    TVector4<TYPE>& mulVector(const TVector4<TYPE>& i_Vector, TVector4<TYPE>* o_pResult) const
    {
        o_pResult->x = _M(0,0) * i_Vector.x + _M(0,1) * i_Vector.y + _M(0,2) * i_Vector.z + _M(0,3) * i_Vector.w;
        o_pResult->y = _M(1,0) * i_Vector.x + _M(1,1) * i_Vector.y + _M(1,2) * i_Vector.z + _M(1,3) * i_Vector.w;
        o_pResult->z = _M(2,0) * i_Vector.x + _M(2,1) * i_Vector.y + _M(2,2) * i_Vector.z + _M(2,3) * i_Vector.w;
        o_pResult->w = _M(3,0) * i_Vector.x + _M(3,1) * i_Vector.y + _M(3,2) * i_Vector.z + _M(3,3) * i_Vector.w;
        return *o_pResult;
    }

    TVector4<TYPE> mulVector(const TVector4<TYPE>& i_Vector) const
    {
        TVector4<TYPE> l_Result;
        mulVector(i_Vector, &l_Result);
        return l_Result;
    }

    TVector4<TYPE>& vectorMul(const TVector4<TYPE>& i_Vector, TVector4<TYPE>* o_pResult) const
    {
        o_pResult->x = i_Vector.x * _M(0,0) + i_Vector.y * _M(1,0) + i_Vector.z * _M(2,0) + i_Vector.w * _M(3,0);
        o_pResult->y = i_Vector.x * _M(0,1) + i_Vector.y * _M(1,1) + i_Vector.z * _M(2,1) + i_Vector.w * _M(3,1);
        o_pResult->z = i_Vector.x * _M(0,2) + i_Vector.y * _M(1,2) + i_Vector.z * _M(2,2) + i_Vector.w * _M(3,2);
        o_pResult->w = i_Vector.x * _M(0,3) + i_Vector.y * _M(1,3) + i_Vector.z * _M(2,3) + i_Vector.w * _M(3,3);
        return * o_pResult;
    }

    TVector4<TYPE> vectorMul(const TVector4<TYPE>& i_Vector) const
    {
        TVector4<TYPE> l_Result;
        vectorMul(i_Vector, &l_Result);
        return l_Result;
    }

    TVector4<TYPE>& transformVector(const TVector4<TYPE>& i_Vector, TVector4<TYPE>* o_pResult) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return vectorMul(i_Vector, o_pResult);
#else
        return mulVector(i_Vector, o_pResult);
#endif
    }

    TVector4<TYPE> transformVector(const TVector4<TYPE>& i_Vector) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return vectorMul(i_Vector);
#else
        return mulVector(i_Vector);
#endif
    }

    // operator overload
    TMatrix4x4& operator= (const TMatrix4x4& i_Other)
    {
        copy(i_Other);
        return *this;
    }

    TMatrix4x4& operator+= (const TMatrix4x4& i_Other)
    {
        add(i_Other);
        return *this;
    }

    TMatrix4x4 operator+ (const TMatrix4x4& i_Other) const
    {
        TMatrix4x4 l_Result(*this);
        l_Result.add(i_Other);
        return l_Result;
    }

    TMatrix4x4& operator-= (const TMatrix4x4& i_Other)
    {
        sub(i_Other);
        return *this;
    }

    TMatrix4x4 operator- (const TMatrix4x4& i_Other) const
    {
        TMatrix4x4 l_Result(*this);
        l_Result.sub(i_Other);
        return l_Result;
    }

    TMatrix4x4& operator*= (const TMatrix4x4& i_Other)
    {
        mul(i_Other);
        return *this;
    }

    TMatrix4x4 operator* (const TMatrix4x4& i_Other) const
    {
        TMatrix4x4 l_Result(*this);
        l_Result.mul(i_Other);
        return l_Result;
    }

public:
    TYPE        m[k_NumElement];
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TMatrix4x4<F32>;
//////template class DOME_CORE_API TMatrix4x4<F64>;
//////template class DOME_CORE_API TMatrix4x4<Int>;

typedef TMatrix4x4<F32>     DMatrix4x4f;
typedef TMatrix4x4<F64>     DMatrix4x4d;
typedef TMatrix4x4<Int>     DMatrix4x4i;

DOME_NAMESPACE_END