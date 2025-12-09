/*
    filename:       bezier.h
    author:         Ming Dong
    date:           2016-Feb-23
    description:    bezier related functions
*/
#pragma once

#include "vector4.h"

DOME_NAMESPACE_BEGIN

/*
    INTERPOLATETYPE can be (F32,F64,TVector2,TVector3,TVector4)
    ELEMENTTYPE can be F32 or F64
*/
template <class INTERPOLATETYPE, class ELEMENTTYPE> 
INTERPOLATETYPE TBezier1(const INTERPOLATETYPE &P0, const INTERPOLATETYPE &P1, ELEMENTTYPE t)
{
	return P0 * (1 - t) + P1 * t;
}

/*
    INTERPOLATETYPE can be (F32,F64,TVector2,TVector3,TVector4)
    ELEMENTTYPE can be F32 or F64
*/
template <class INTERPOLATETYPE, class ELEMENTTYPE> 
INTERPOLATETYPE TBezier2(const INTERPOLATETYPE &P0, const INTERPOLATETYPE &P1, const INTERPOLATETYPE &P2, ELEMENTTYPE t)
{
	INTERPOLATETYPE P00 = P0, P10 = P1, P20 = P2;
	// first iteration
	INTERPOLATETYPE P11 = P00 * (1 - t) + P10 * t;
	INTERPOLATETYPE P21 = P10 * (1 - t) + P20 * t;

	// second iteration
	INTERPOLATETYPE P22 = P11 * (1 - t) + P21 * t;

	return P22;
}

/*
    INTERPOLATETYPE can be (F32,F64,TVector2,TVector3,TVector4)
    ELEMENTTYPE can be F32 or F64
*/
template <class INTERPOLATETYPE, class ELEMENTTYPE> 
INTERPOLATETYPE TBezier3(const INTERPOLATETYPE &P0, const INTERPOLATETYPE &P1, const INTERPOLATETYPE &P2, const INTERPOLATETYPE &P3, ELEMENTTYPE t)
{
	INTERPOLATETYPE P00 = P0, P10 = P1, P20 = P2, P30 = P3;
	// first iteration
	INTERPOLATETYPE P11 = P00 * (1 - t) + P10 * t;
	INTERPOLATETYPE P21 = P10 * (1 - t) + P20 * t;
	INTERPOLATETYPE P31 = P20 * (1 - t) + P30 * t;

	// second iteration
	INTERPOLATETYPE P22 = P11 * (1 - t) + P21 * t;
	INTERPOLATETYPE P32 = P21 * (1 - t) + P31 * t;

	// third iteration
	INTERPOLATETYPE P33 = P22 * (1 - t) + P32 * t;
	return P33;
}

/*
    Get bezier curve t from its value fX
    To call this function, the curve should be a increase only curve.
    that means, when t [0,1] increase, the fX should increase only.
    the curve is a increase only bezier curve when the following condition satisfied:
    1) fX0 < fX3
    2) fX0 < fX1 < fX3
    3) fX0 < fX2 < fX3
    and also, the fX should satisfy the condition  fX0 <= fX <= fX3
*/
template <class TYPE>
TYPE TGetBezier3Time(TYPE fX0, TYPE fX1, TYPE fX2, TYPE fX3, TYPE fX, TYPE Tolerance)
{
	TYPE fTMax = (TYPE)1;
	TYPE fTMin = (TYPE)0;
	TYPE fRetT = (TYPE)0;
	TYPE fRetX = (TYPE)0;

	while (fTMax > fTMin)
	{
		fRetT = (fTMax + fTMin) / (TYPE)2;
		fRetX = TBezier3(fX0, fX1, fX2, fX3, fRetT);
		if (fabs(fRetX - fX) <= Tolerance)
		{
			break;
		}
		else if (fRetX > fX)
			fTMax = fRetT;
		else
			fTMin = fRetT;
	}
	return fRetT;
}




DOME_NAMESPACE_END