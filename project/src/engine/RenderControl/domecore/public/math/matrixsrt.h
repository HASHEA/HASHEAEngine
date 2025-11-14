/*
    filename:       matrixsrt.h
    author:         Ming Dong
    date:           2016-Feb-28
    description:    matrixsrt template (Matrix which has scale, 
                    rotation and translation information)
*/
#pragma once
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "matrix4x4.h"

DOME_NAMESPACE_BEGIN

template <class TYPE>
class TMatrixSRT
{
public:
    TMatrixSRT()
        :m_AxisX((TYPE)1, (TYPE)0, (TYPE)0)
        ,m_AxisY((TYPE)0, (TYPE)1, (TYPE)0)
        ,m_AxisZ((TYPE)0, (TYPE)0, (TYPE)1)
        ,m_Translation((TYPE)0, (TYPE)0, (TYPE)0)
    {

    }

    TMatrixSRT(const TVector3<TYPE>& i_AxisX, const TVector3<TYPE>& i_AxisY,
        const TVector3<TYPE>& i_AxisZ, const TVector3<TYPE>& i_Translation)
        :m_AxisX(i_AxisX)
        ,m_AxisY(i_AxisY)
        ,m_AxisZ(i_AxisZ)
        ,m_Translation(i_Translation)
    {

    }

    TMatrixSRT(const TMatrixSRT& i_Other)
    {
        copy(i_Other);
    }

    ~TMatrixSRT()
    {

    }

    TMatrixSRT& copy(const TMatrixSRT& i_Other)
    {
        m_AxisX = i_Other.m_AxisX;
        m_AxisY = i_Other.m_AxisY;
        m_AxisZ = i_Other.m_AxisZ;
        m_Translation = i_Other.m_Translation;
        return *this;
    }


public:
    TVector3<TYPE>      m_AxisX;
    TVector3<TYPE>      m_AxisY;
    TVector3<TYPE>      m_AxisZ;
    TVector3<TYPE>      m_Translation;
};


DOME_NAMESPACE_END