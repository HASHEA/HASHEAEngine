/*
    filename:       matrix3x3.h
    author:         Ming Dong
    date:           2016-Feb-24
    description:    matrix3x3 template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "matrix2x2.h"

DOME_NAMESPACE_BEGIN

/*
    if DOME_MATRIX_ROWMAJOR is defined
        | m0  m1  m2 |
        | m3  m4  m5 |
        | m6  m7  m8 |
    else
        | m0  m3  m6 |
        | m1  m4  m7 |
        | m2  m5  m8 |
*/
template<class TYPE>
class TMatrix3x3
{
public:
    const static Int k_NumRow = 3;
    const static Int k_NumColumn = 3;
    const static Int k_NumElement = k_NumRow * k_NumColumn;
    static const TMatrix3x3& Zero()
    {
        static TYPE s_Zero[] = {
            (TYPE)0, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)0, (TYPE)0
        };
        return *((TMatrix3x3*)s_Zero);
    };
    static const TMatrix3x3& Identity()
    {
        static TYPE s_Identity[] = {
            (TYPE)1, (TYPE)0, (TYPE)0, 
            (TYPE)0, (TYPE)1, (TYPE)0,
            (TYPE)0, (TYPE)0, (TYPE)1
        };
        return *((TMatrix3x3*)s_Identity);
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
        return *((TVector3<TYPE>*)&_M(3));
    }
    inline const TVector3<TYPE>& axisY() const
    {
        return *((TVector3<TYPE>*)&_M(3));
    }
    inline TVector3<TYPE>& axisZ()
    {
        return *((TVector3<TYPE>*)&_M(6));
    }
    inline const TVector3<TYPE>& axisZ() const
    {
        return *((TVector3<TYPE>*)&_M(6));
    }

public:
    TMatrix3x3()
    {
        // do nothing here for performance optimization
    }

    TMatrix3x3(const TMatrix3x3& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) = i_Other._M(i);
    }

    ~TMatrix3x3()
    {

    }

    TMatrix3x3& copy(const TMatrix3x3& i_Other)
    {
        for (Int i = 0; i < k_NumElement; ++i)
        {
            _M(i) = i_Other._M(i);
        }
        return *this;
    }

    TMatrix3x3& add(const TMatrix3x3& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) += i_Other._M(i);
        return *this;
    }

    TMatrix3x3 addTo(const TMatrix3x3& i_Other) const
    {
        TMatrix3x3 l_Result(*this);
        return l_Result.add(i_Other);
    }

    TMatrix3x3& addTo(const TMatrix3x3& i_Other, TMatrix3x3* o_pResult) const
    {
        for(Int i = 0; i < k_NumElement; ++i)
            o_pResult->_M(i) = _M(i) + i_Other._M(i);
        return *o_pResult;
    }

    TMatrix3x3& sub(const TMatrix3x3& i_Other)
    {
        for(Int i = 0; i < k_NumElement; ++i)
            _M(i) -= i_Other._M(i);
        return *this;
    }

    TMatrix3x3 subTo(const TMatrix3x3& i_Other) const
    {
        TMatrix3x3 l_Result(*this);
        return l_Result.sub(i_Other);
    }

    TMatrix3x3& subTo(const TMatrix3x3& i_Other, TMatrix3x3* o_pResult) const
    {
        for(Int i = 0; i < k_NumElement; ++i)
            o_pResult->_M(i) = _M(i) - i_Other._M(i);
        return *o_pResult;
    }

    TMatrix3x3& mulTo(const TMatrix3x3& i_Other, TMatrix3x3* o_pResult) const
    {
        for (Int row = 0; row < k_NumRow; ++row)
        {
            for (Int col = 0; col < k_NumColumn; ++col)
            {
                o_pResult->_M(row, col) = 
                    _M(row, 0) * i_Other._M(0, col) + 
                    _M(row, 1) * i_Other._M(1, col) + 
                    _M(row, 2) * i_Other._M(2, col);
            }
        }

        return *o_pResult;
    }

    TMatrix3x3 mulTo(const TMatrix3x3& i_Other) const
    {
        TMatrix3x3 l_Result;
        return mulTo(i_Other, &l_Result);
    }

    TMatrix3x3& mul(const TMatrix3x3& i_Other)
    {
        TMatrix3x3 l_Result;
        mulTo(i_Other, &l_Result);
        copy(l_Result);
        return *this;
    }

    TMatrix3x3& mul(TYPE i_Scale)
    {
        for (Int i = 0; i < k_NumElement; ++i)
        {
            _M(i) *= i_Scale;
        }
        return *this;
    }

    TMatrix2x2<TYPE>& buildMinorMatrix(Int row, Int column, TMatrix2x2<TYPE>* o_pResult) const
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
        return _M(0,0) * _M(1,1) * _M(2,2) + 
               _M(0,1) * _M(1,2) * _M(2,0) + 
               _M(0,2) * _M(1,0) * _M(2,1) -
               _M(0,2) * _M(1,1) * _M(2,0) -
               _M(0,1) * _M(1,0) * _M(2,2) -
               _M(0,0) * _M(1,2) * _M(2,1);
    }

    TMatrix3x3&  transposeTo(TMatrix3x3* o_pResult) const
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

    TMatrix3x3 transposeTo() const
    {
        TMatrix3x3 l_Result;
        transposeTo(&l_Result);
        return l_Result;
    }

    TMatrix3x3& transpose()
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
        TMatrix2x2<TYPE> l_MinorMtx;
        buildMinorMatrix(row, column, &l_MinorMtx);
        return l_MinorMtx.deterninant();
    }

    TYPE cofactor(Int row, Int column) const
    {
        return Math::IsOdd(row + column) ? (-minor(row, column)) : minor(row, column);
    }

    TMatrix3x3& inverseTo(TMatrix3x3* o_pResult) const
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

    TMatrix3x3 inverseTo() const
    {
        TMatrix3x3 l_Result;
        inverseTo(&l_Result);
        return l_Result;
    }

    TMatrix3x3& inverse()
    {
        TMatrix3x3 l_Result;
        inverseTo(&l_Result);
        return copy(l_Result);
    }

    TMatrix3x3& buildScale(TYPE i_Scale)
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

    TMatrix3x3& buildRotateX(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX().set((TYPE)1, (TYPE)0, (TYPE)0);
        axisY().set((TYPE)0, l_Cos,   l_Sin  );
        axisZ().set((TYPE)0, -l_Sin,  l_Cos  );
        return *this;
    }

    TMatrix3x3& buildRotateY(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX().set(l_Cos,   (TYPE)0, -l_Sin );
        axisY().set((TYPE)0, (TYPE)1, (TYPE)0);
        axisZ().set(l_Sin,   (TYPE)0, l_Cos  );
        return *this;
    }

    TMatrix3x3& buildRotateZ(TYPE i_Rad)
    {
        TYPE l_Sin = Math::Sin(i_Rad);
        TYPE l_Cos = Math::Cos(i_Rad);
        axisX().set(l_Cos,   l_Sin,   (TYPE)0);
        axisY().set(-l_Sin,  l_Cos,   (TYPE)0);
        axisZ().set((TYPE)0, (TYPE)0, (TYPE)1);
        return *this;
    }

    TVector3<TYPE>& mulVector(const TVector3<TYPE>& i_Vector, TVector3<TYPE>* o_pResult) const
    {
        o_pResult->x = _M(0,0) * i_Vector.x + _M(0,1) * i_Vector.y + _M(0,2) * i_Vector.z;
        o_pResult->y = _M(1,0) * i_Vector.x + _M(1,1) * i_Vector.y + _M(1,2) * i_Vector.z;
        o_pResult->z = _M(2,0) * i_Vector.x + _M(2,1) * i_Vector.y + _M(2,2) * i_Vector.z;
        return *o_pResult;
    }

    TVector3<TYPE> mulVector(const TVector3<TYPE>& i_Vector) const
    {
        TVector3<TYPE> l_Result;
        mulVector(i_Vector, &l_Result);
        return l_Result;
    }

    TVector3<TYPE>& vectorMul(const TVector3<TYPE>& i_Vector, TVector3<TYPE>* o_pResult) const
    {
        o_pResult->x = i_Vector.x * _M(0,0) + i_Vector.y * _M(1,0) + i_Vector.z * _M(2,0);
        o_pResult->y = i_Vector.x * _M(0,1) + i_Vector.y * _M(1,1) + i_Vector.z * _M(2,1);
        o_pResult->z = i_Vector.x * _M(0,2) + i_Vector.y * _M(1,2) + i_Vector.z * _M(2,2);
        return * o_pResult;
    }

    TVector3<TYPE> vectorMul(const TVector3<TYPE>& i_Vector) const
    {
        TVector3<TYPE> l_Result;
        vectorMul(i_Vector, &l_Result);
        return l_Result;
    }

    TVector3<TYPE>& transformVector(const TVector3<TYPE>& i_Vector, TVector3<TYPE>* o_pResult) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return vectorMul(i_Vector, o_pResult);
#else
        return mulVector(i_Vector, o_pResult);
#endif
    }

    TVector3<TYPE> transformVector(const TVector3<TYPE>& i_Vector) const
    {
#ifdef DOME_MATRIX_ROWMAJOR
        return vectorMul(i_Vector);
#else
        return mulVector(i_Vector);
#endif
    }


    // operator overload
    TMatrix3x3& operator= (const TMatrix3x3& i_Other)
    {
        copy(i_Other);
        return *this;
    }

    TMatrix3x3& operator+= (const TMatrix3x3& i_Other)
    {
        add(i_Other);
        return *this;
    }

    TMatrix3x3 operator+ (const TMatrix3x3& i_Other) const
    {
        TMatrix3x3 l_Result(*this);
        l_Result.add(i_Other);
        return l_Result;
    }

    TMatrix3x3& operator-= (const TMatrix3x3& i_Other)
    {
        sub(i_Other);
        return *this;
    }

    TMatrix3x3 operator- (const TMatrix3x3& i_Other) const
    {
        TMatrix3x3 l_Result(*this);
        l_Result.sub(i_Other);
        return l_Result;
    }

    TMatrix3x3& operator*= (const TMatrix3x3& i_Other)
    {
        mul(i_Other);
        return *this;
    }

    TMatrix3x3 operator* (const TMatrix3x3& i_Other) const
    {
        TMatrix3x3 l_Result(*this);
        l_Result.mul(i_Other);
        return l_Result;
    }

public:
    TYPE        m[k_NumElement];
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TMatrix3x3<F32>;
//////template class DOME_CORE_API TMatrix3x3<F64>;
//////template class DOME_CORE_API TMatrix3x3<Int>;

typedef TMatrix3x3<F32>     DMatrix3x3f;
typedef TMatrix3x3<F64>     DMatrix3x3d;
typedef TMatrix3x3<Int>     DMatrix3x3i;

DOME_NAMESPACE_END