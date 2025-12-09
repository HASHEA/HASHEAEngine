/*
    filename:       rcglobal.h
    author:         Ming Dong
    date:           2016-MAY-25
    description:    
*/
#pragma once

#include "rcconfigure.h"

RC_NAMESPACE_BEGIN

#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( push )
#  pragma warning( disable: 4251 )
#endif

class RC_API RCGlobal : public TSingleton<RCGlobal>
{
public:
    RCGlobal();

    DString                         m_RCDataRootPath;

    static const Char*              k_SimpleTypeName_Unknown;
    static DSimpleTypeID            k_SimpleTypeID_Unknown;
    DString                         m_SimpleTypeName_Unknown;

    static const Char*              k_SimpleTypeName_OSTexture2D;
    static DSimpleTypeID            k_SimpleTypeID_OSTexture2D;
    DString                         m_SimpleTypeName_OSTexture2D;

    static const Char*              k_SimpleTypeName_OSTexture3D;
    static DSimpleTypeID            k_SimpleTypeID_OSTexture3D;
    DString                         m_SimpleTypeName_OSTexture3D;

    static const Char*              k_SimpleTypeName_OSTextureCube;
    static DSimpleTypeID            k_SimpleTypeID_OSTextureCube;
    DString                         m_SimpleTypeName_OSTextureCube;

    static const Char*              k_SimpleTypeName_DMatrix4x4f;
    static DSimpleTypeID            k_SimpleTypeID_DMatrix4x4f;
    DString                         m_SimpleTypeName_DMatrix4x4f;

    static const Char*              k_SimpleTypeName_DMatrix3x3f;
    static DSimpleTypeID            k_SimpleTypeID_DMatrix3x3f;
    DString                         m_SimpleTypeName_DMatrix3x3f;

    static const Char*              k_SimpleTypeName_DMatrix2x2f;
    static DSimpleTypeID            k_SimpleTypeID_DMatrix2x2f;
    DString                         m_SimpleTypeName_DMatrix2x2f;

    static const Char*              k_SimpleTypeName_F32;
    static DSimpleTypeID            k_SimpleTypeID_F32;
    DString                         m_SimpleTypeName_F32;

    static const Char*              k_SimpleTypeName_DVector2f;
    static DSimpleTypeID            k_SimpleTypeID_DVector2f;
    DString                         m_SimpleTypeName_DVector2f;

    static const Char*              k_SimpleTypeName_DVector3f;
    static DSimpleTypeID            k_SimpleTypeID_DVector3f;
    DString                         m_SimpleTypeName_DVector3f;

    static const Char*              k_SimpleTypeName_DVector4f;
    static DSimpleTypeID            k_SimpleTypeID_DVector4f;
    DString                         m_SimpleTypeName_DVector4f;

    static const Char*              k_SimpleTypeName_DVectorLut1f;
    static DSimpleTypeID            k_SimpleTypeID_DVectorLut1f;
    DString                         m_SimpleTypeName_DVectorLut1f;

    static const Char*              k_SimpleTypeName_DVectorLut2f;
    static DSimpleTypeID            k_SimpleTypeID_DVectorLut2f;
    DString                         m_SimpleTypeName_DVectorLut2f;

    static const Char*              k_SimpleTypeName_DVectorLut3f;
    static DSimpleTypeID            k_SimpleTypeID_DVectorLut3f;
    DString                         m_SimpleTypeName_DVectorLut3f;

    static const Char*              k_SimpleTypeName_DVectorLut4f;
    static DSimpleTypeID            k_SimpleTypeID_DVectorLut4f;
    DString                         m_SimpleTypeName_DVectorLut4f;

    static const Char*              k_SimpleTypeName_DString;
    static DSimpleTypeID            k_SimpleTypeID_DString;
    DString                         m_SimpleTypeName_DString;

    static const Char*              k_SimpleTypeName_Int;
    static DSimpleTypeID            k_SimpleTypeID_Int;
    DString                         m_SimpleTypeName_Int;

    static const Char*              k_SimpleTypeName_DVector2i;
    static DSimpleTypeID            k_SimpleTypeID_DVector2i;
    DString                         m_SimpleTypeName_DVector2i;

    static const Char*              k_SimpleTypeName_DVector3i;
    static DSimpleTypeID            k_SimpleTypeID_DVector3i;
    DString                         m_SimpleTypeName_DVector3i;

    static const Char*              k_SimpleTypeName_DVector4i;
    static DSimpleTypeID            k_SimpleTypeID_DVector4i;
    DString                         m_SimpleTypeName_DVector4i;

};

#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( pop )
#endif

RC_NAMESPACE_END