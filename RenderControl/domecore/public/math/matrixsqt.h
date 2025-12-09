/*
    filename:       matrixsqt.h
    author:         Ming Dong
    date:           2016-Feb-28
    description:    matrixsqt template (Matrix which has scale, 
                    quaternion and translation information)
*/
#pragma once
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "mathutils.h"
#include "quaternion.h"

DOME_NAMESPACE_BEGIN

template <class TYPE>
class TMatrixSQT
{
public:


public:
    TVector3<TYPE>      m_Scale;
    TQuaternion<TYPE>   m_Quaternion;
    TVector3<TYPE>      m_Translation;
};


DOME_NAMESPACE_END