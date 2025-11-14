/*
    filename:       quaternion.h
    author:         Ming Dong
    date:           2016-Feb-24
    description:    quaternion template
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "matrix4x4.h"

DOME_NAMESPACE_BEGIN

template<class TYPE>
class TQuaternion
{
public:
    static const TQuaternion& Identity()
    {
        static TYPE s_Identity[] = {(TYPE)1, (TYPE)0, (TYPE)0, (TYPE)0};
        return *((TQuaternion*)s_Identity);
    }

    inline TYPE& angle()
    {
        return w;
    }

    inline const TYPE& angle() const
    {
        return w;
    }

    inline TVector3<TYPE>& axis()
    {
        return *((TVector3<TYPE>*)&x);
    }

    inline const TVector3<TYPE>& axis() const
    {
        return *((TVector3<TYPE>*)&x);
    }

    inline TVector4<TYPE>& vector4()
    {
        return *((TVector4<TYPE>*)&x);
    }

    inline const TVector4<TYPE>& vector4() const
    {
        return *((TVector4<TYPE>*)&x);
    }

public:
    TQuaternion()
    {
        w = (TYPE)1;
        x = y = z = (TYPE)0;
    }

    TQuaternion(TYPE i_w, TYPE i_x, TYPE i_y, TYPE i_z)
    {
        w = i_w;
        x = i_x;
        y = i_y;
        z = i_z;
    }

    TQuaternion(TYPE i_Rad, const TVector3<TYPE>& i_Axis)
    {
        setByAxisAngle(i_Rad, i_Axis);
    }

    ~TQuaternion()
    {

    }

    TQuaternion& copy(const TQuaternion& i_Other)
    {
        w = i_Other.w;
        x = i_Other.x;
        y = i_Other.y;
        z = i_Other.z;
        return *this;
    }

    void setByAxisAngle(TYPE i_Rad, TYPE nx, TYPE ny, TYPE nz)
    {
        TYPE l_Sin = Math::Sin(i_Rad / (TYPE)2);
        TYPE l_Cos = Math::Cos(i_Rad / (TYPE)2);
        w = l_Cos;
        x = l_Sin * nx;
        y = l_Sin * ny;
        z = l_Sin * nz;
    }

    void setByAxisAngle(TYPE i_Rad, const TVector3<TYPE>& i_Axis)
    {
        setByAxisAngle(i_Rad, i_Axis.x, i_Axis.y, i_Axis.z);
    }

    TQuaternion& negate()
    {
        w = -w; x = -x; y = -y; z = -z;
        return *this;
    }

    TQuaternion& negateTo(TQuaternion* o_pResult) const
    {
        o_pResult->x = -x;
        o_pResult->y = -y;
        o_pResult->z = -z;
        o_pResult->w = -w;
        return *o_pResult;
    }

    TQuaternion negateTo() const
    {
        TQuaternion l_Result;
        return negateTo(&l_Result);
    }

    TQuaternion& mul(TYPE i_Scale)
    {
        x *= i_Scale;
        y *= i_Scale;
        z *= i_Scale;
        w *= i_Scale;
        return *this;
    }

    TYPE magnitude() const
    {
        return Math::SquareRoot(w * w + x * x + y * y + z * z);
    }

    TYPE magnitudeSQ() const
    {
        return w * w + x * x + y * y + z * z;
    }

    TQuaternion& conjugate()
    {
        x = -x; y = -y; z = -z;
        return *this;
    }

    TQuaternion& conjugateTo(TQuaternion* o_pResult) const
    {
        o_pResult->x = -x;
        o_pResult->y = -y;
        o_pResult->z = -z;
        o_pResult->w = w;
        return *o_pResult;
    }

    TQuaternion conjugateTo() const
    {
        TQuaternion l_Result;
        return conjugateTo(&l_Result);
    }

    TQuaternion& inverseTo(TQuaternion* o_pResult) const
    {
        conjugateTo(o_pResult);
        o_pResult->mul((TYPE)1 / magnitude());
        return *o_pResult;
    }

    TQuaternion inverseTo() const
    {
        TQuaternion l_Result;
        inverseTo(&l_Result);
        return l_Result;
    }

    TQuaternion& inverse()
    {
        TQuaternion l_Result;
        copy(inverseTo(&l_Result));
        return *this;
    }

    TQuaternion& mulTo(const TQuaternion& i_Other, TQuaternion* o_pResult) const
    {
        o_pResult->angle() = angle() * i_Other.angle() - axis().dotProduct(i_Other.axis());
        o_pResult->axis() = i_Other.axis() * angle() + axis() * i_Other.angle() + axis().crossTo(i_Other.axis());
        return *o_pResult;
    }

    TQuaternion mulTo(const TQuaternion& i_Other) const
    {
        TQuaternion l_Result;
        mulTo(i_Other, &l_Result);
        return l_Result;
    }

    TQuaternion& mul(const TQuaternion& i_Other)
    {
        TQuaternion l_Result;
        mulTo(i_Other, &l_Result);
        copy(l_Result);
        return *this;
    }

    TYPE dotProduct(const TQuaternion& i_Other) const
    {
        return vector4().dotProduct(i_Other.vector4());
    }

    TQuaternion& exponentTo(TYPE i_Exponent, TQuaternion* o_pResult) const
    {
        // the quaternion should be a unit quaternion, aka, magnitude() == 1, 
        // when this function is called
        // if w == 1, means cos(a/2) == 1, means the rotate angle is 0, in 
        // this case, exponent of this quaternion is always itself.
        if (w < (TYPE)0.9999)
        {
            // Extract the half angle alpha (alpha = theta / 2)
            TYPE l_Alpha = Math::ArcCos(w);

            // Compute new alpha value
            TYPE l_NewAlpha = l_Alpha * i_Exponent;

            // compute new w value
            o_pResult->w = Math::Cos(l_NewAlpha);

            // compute new xyz values
            TYPE l_Mult = Math::Sin(l_NewAlpha) / Math::Sin(l_Alpha);
            o_pResult->x *= l_Mult;
            o_pResult->y *= l_Mult;
            o_pResult->z *= l_Mult;
        }
        return *o_pResult;
    }

    TQuaternion exponentTo(TYPE i_Exponent) const
    {
        TQuaternion l_Result;
        exponentTo(i_Exponent, &l_Result);
        return l_Result;
    }

    TQuaternion& exponent(TYPE i_Exponent)
    {
        TQuaternion l_Result;
        exponentTo(i_Exponent, &l_Result);
        copy(l_Result);
        return *this;
    }

    TQuaternion& lerpTo(const TQuaternion& i_Other, TYPE i_Ratio, TQuaternion* o_pResult) const
    {
        // this function assume me and other quaternion are all unit quaternion
        TQuaternion q0(*this);
        TQuaternion q1(i_Other);
        TYPE t = i_Ratio;

        // Compute the ¡± cos ine of the angle ¡± between the
        // quaternions , us ing the dot product
        TYPE cosOmega = dotProduct(i_Other);

        // if negative dot, negate one of the input
        // quaternion, to take the shorter 4D "arc"
        if (cosOmega < (TYPE)0)
        {
            q1.negate();
            cosOmega = -cosOmega;
        }

        // check if they are very close together, to protect 
        // against divide-by-zero
        TYPE k0, k1;
        if (cosOmega > (TYPE)0.9999)
        {
            // very close - just use linear interpolation
            k0 = (TYPE)1 - t;
            k1 = t;
        }
        else
        {
            // compute the sin of the angle using the
            // trig identity    sin^2(omega) + cos^2(omega) = 1
            TYPE sinOmega = Math::SquareRoot((TYPE)1 - cosOmega * cosOmega);

            // compute the angle from its sin and cosine
            TYPE omega = Math::ArcTan2(sinOmega, cosOmega);

            // compute inverse of denominator, so we only have
            // to divide once
            TYPE onOverSinOmega = (TYPE)1 / sinOmega;

            // compute interpolation parameters
            k0 = Math::Sin( ((TYPE)1 - t) * omega ) * onOverSinOmega;
            k1 = Math::Sin( t * omega ) * onOverSinOmega;
        }

        // Interpolate
        o_pResult->w = q0.w * k0 + q1.w * k1;
        o_pResult->x = q0.x * k0 + q1.x * k1;
        o_pResult->y = q0.y * k0 + q1.y * k1;
        o_pResult->z = q0.z * k0 + q1.z * k1;

        return *o_pResult;
    }

    TQuaternion lerpTo(const TQuaternion& i_Other, TYPE i_Ratio) const
    {
        TQuaternion l_Result;
        lerpTo(i_Other, i_Ratio, &l_Result);
        return l_Result;
    }

    TQuaternion& lerp(const TQuaternion& i_Other, TYPE i_Ratio)
    {
        TQuaternion l_Result;
        lerpTo(i_Other, i_Ratio, &l_Result);
        copy(l_Result);
        return *this;
    }

    TMatrix3x3<TYPE>& toMatrix3x3(TMatrix3x3<TYPE>* o_pResult) const
    {
        o_pResult->axisX().set(
            (TYPE)1 - (TYPE)2 * y * y - (TYPE)2 * z * z,
            (TYPE)2 * x * y + (TYPE)2 * w * z,
            (TYPE)2 * x * z - (TYPE)2 * w * y
            );
        o_pResult->axisY().set(
            (TYPE)2 * x * y - (TYPE)2 * w * z,
            (TYPE)1 - (TYPE)2 * x * x - (TYPE)2 * z * z,
            (TYPE)2 * y * z + (TYPE)2 * w * x
            );
        o_pResult->axisZ().set(
            (TYPE)2 * x * z + (TYPE)2 * w * y,
            (TYPE)2 * y * z - (TYPE)2 * w * x,
            (TYPE)1 - (TYPE)2 * x * x - (TYPE)2 * y * y
            );
        return *o_pResult;
    }

    TMatrix4x4<TYPE>& toMatrix4x4(TMatrix4x4<TYPE>* o_pResult) const
    {
        o_pResult->axisX4().set(
            (TYPE)1 - (TYPE)2 * y * y - (TYPE)2 * z * z,
            (TYPE)2 * x * y + (TYPE)2 * w * z,
            (TYPE)2 * x * z - (TYPE)2 * w * y,
            (TYPE)0
            );
        o_pResult->axisY4().set(
            (TYPE)2 * x * y - (TYPE)2 * w * z,
            (TYPE)1 - (TYPE)2 * x * x - (TYPE)2 * z * z,
            (TYPE)2 * y * z + (TYPE)2 * w * x,
            (TYPE)0
            );
        o_pResult->axisZ4().set(
            (TYPE)2 * x * z + (TYPE)2 * w * y,
            (TYPE)2 * y * z - (TYPE)2 * w * x,
            (TYPE)1 - (TYPE)2 * x * x - (TYPE)2 * y * y,
            (TYPE)0
            );
        o_pResult->translation4().set((TYPE)0, (TYPE)0, (TYPE)0, (TYPE)1);
        return *o_pResult;
    }

    TMatrix3x3<TYPE> getMatrix3x3() const
    {
        TMatrix3x3<TYPE> l_Result;
        toMatrix3x3(&l_Result);
        return l_Result;
    }

    TMatrix4x4<TYPE> getMatrix4x4() const
    {
        TMatrix4x4<TYPE> l_Result;
        toMatrix4x4(&l_Result);
        return l_Result;
    }

    TQuaternion& fromMatrix3x3(const TMatrix3x3<TYPE>& i_Matrix)
    {
        const TVector3<TYPE>& axisX = i_Matrix.axisX();
        const TVector3<TYPE>& axisY = i_Matrix.axisY();
        const TVector3<TYPE>& axisZ = i_Matrix.axisZ();
        TYPE m11 = axisX.x, m12 = axisX.y, m13 = axisX.z; 
        TYPE m21 = axisY.x, m22 = axisY.y, m23 = axisY.z; 
        TYPE m31 = axisZ.x, m32 = axisZ.y, m33 = axisZ.z;

        // Determine which of w, x , y , or z has the largest absolute value 
        TYPE fourWSquaredMinus1 = m11 + m22 + m33;
        TYPE fourXSquaredMinus1 = m11 - m22 - m33;
        TYPE fourYSquaredMinus1 = m22 - m11 - m33;
        TYPE fourZSquaredMinus1 = m33 - m11 - m22;

        Int biggestIndex = 0; 
        TYPE fourBiggestSquaredMinus1 = fourWSquaredMinus1 ;
        if ( fourXSquaredMinus1 > fourBiggestSquaredMinus1 ) 
        { 
            fourBiggestSquaredMinus1 = fourXSquaredMinus1 ; 
            biggestIndex = 1; 
        } 
        if ( fourYSquaredMinus1 > fourBiggestSquaredMinus1 ) 
        { 
            fourBiggestSquaredMinus1 = fourYSquaredMinus1 ; 
            biggestIndex = 2; 
        } 
        if ( fourZSquaredMinus1 > fourBiggestSquaredMinus1 ) 
        { 
            fourBiggestSquaredMinus1 = fourZSquaredMinus1 ; 
            biggestIndex = 3; 
        } 

        // Perform square root and division 
        TYPE biggestVal = Math::SquareRoot(fourBiggestSquaredMinus1 + (TYPE)1) * (TYPE)0.5;
        TYPE mult = (TYPE)0.25 / biggestVal ;

        // Apply table to compute quaternion values 
        switch ( biggestIndex ) 
        { 
        case 0: 
            w = biggestVal ; 
            x = (m23 - m32) * mult ;
            y = (m31 - m13) * mult ; 
            z = (m12 - m21) * mult ; 
            break ;
        case 1: 
            x = biggestVal ; 
            w = (m23 - m32) * mult ; 
            y = (m12 + m21) * mult ; 
            z = (m31 + m13) * mult ; 
            break ;
        case 2: 
            y = biggestVal ; 
            w = (m31 - m13) * mult ; 
            x = (m12 + m21) * mult ; 
            z = (m23 + m32) * mult ; 
            break ;
        case 3: 
            z = biggestVal ; 
            w = (m12 - m21) * mult ; 
            x = (m31 + m13) * mult ; 
            y = (m23 + m32) * mult ; 
            break ;
        }
        return *this;
    }

    TQuaternion& fromMatrix4x4(const TMatrix4x4<TYPE>& i_Matrix)
    {
        const TVector3<TYPE>& axisX = i_Matrix.axisX();
        const TVector3<TYPE>& axisY = i_Matrix.axisY();
        const TVector3<TYPE>& axisZ = i_Matrix.axisZ();
        TYPE m11 = axisX.x, m12 = axisX.y, m13 = axisX.z; 
        TYPE m21 = axisY.x, m22 = axisY.y, m23 = axisY.z; 
        TYPE m31 = axisZ.x, m32 = axisZ.y, m33 = axisZ.z;

        // Determine which of w, x , y , or z has the largest absolute value 
        TYPE fourWSquaredMinus1 = m11 + m22 + m33;
        TYPE fourXSquaredMinus1 = m11 - m22 - m33;
        TYPE fourYSquaredMinus1 = m22 - m11 - m33;
        TYPE fourZSquaredMinus1 = m33 - m11 - m22;

        Int biggestIndex = 0; 
        TYPE fourBiggestSquaredMinus1 = fourWSquaredMinus1 ;
        if ( fourXSquaredMinus1 > fourBiggestSquaredMinus1 ) 
        { 
            fourBiggestSquaredMinus1 = fourXSquaredMinus1 ; 
            biggestIndex = 1; 
        } 
        if ( fourYSquaredMinus1 > fourBiggestSquaredMinus1 ) 
        { 
            fourBiggestSquaredMinus1 = fourYSquaredMinus1 ; 
            biggestIndex = 2; 
        } 
        if ( fourZSquaredMinus1 > fourBiggestSquaredMinus1 ) 
        { 
            fourBiggestSquaredMinus1 = fourZSquaredMinus1 ; 
            biggestIndex = 3; 
        } 

        // Perform square root and division 
        TYPE biggestVal = Math::SquareRoot(fourBiggestSquaredMinus1 + (TYPE)1) * (TYPE)0.5;
        TYPE mult = (TYPE)0.25 / biggestVal ;

        // Apply table to compute quaternion values 
        switch ( biggestIndex ) 
        { 
        case 0: 
            w = biggestVal ; 
            x = (m23 - m32) * mult ;
            y = (m31 - m13) * mult ; 
            z = (m12 - m21) * mult ; 
            break ;
        case 1: 
            x = biggestVal ; 
            w = (m23 - m32) * mult ; 
            y = (m12 + m21) * mult ; 
            z = (m31 + m13) * mult ; 
            break ;
        case 2: 
            y = biggestVal ; 
            w = (m31 - m13) * mult ; 
            x = (m12 + m21) * mult ; 
            z = (m23 + m32) * mult ; 
            break ;
        case 3: 
            z = biggestVal ; 
            w = (m12 - m21) * mult ; 
            x = (m31 + m13) * mult ; 
            y = (m23 + m32) * mult ; 
            break ;
        }
        return *this;
    }


public:
    TYPE    x;
    TYPE    y;
    TYPE    z;
    TYPE    w;
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TQuaternion<F32>;
//////template class DOME_CORE_API TQuaternion<F64>;
//////template class DOME_CORE_API TQuaternion<Int>;

typedef TQuaternion<F32>    DQuaternionf;
typedef TQuaternion<F64>    DQuaterniond;
typedef TQuaternion<Int>    DQuaternioni;

DOME_NAMESPACE_END