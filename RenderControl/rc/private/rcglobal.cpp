/*
    filename:       rcglobal.cpp
    author:         Ming Dong
    date:           2016-MAY-25
    description:    
*/

#include "../public/rcglobal.h"

RC_NAMESPACE_BEGIN

const Char*         RCGlobal::k_SimpleTypeName_Unknown = "Unknown";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_Unknown("Unknown");

const Char*         RCGlobal::k_SimpleTypeName_OSTexture2D = "OSTexture2D";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_OSTexture2D("OSTexture2D");

const Char*         RCGlobal::k_SimpleTypeName_OSTexture3D = "OSTexture3D";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_OSTexture3D("OSTexture3D");

const Char*         RCGlobal::k_SimpleTypeName_OSTextureCube = "OSTextureCube";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_OSTextureCube("OSTextureCube");

const Char*         RCGlobal::k_SimpleTypeName_DMatrix4x4f = "DMatrix4x4f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DMatrix4x4f("DMatrix4x4f");

const Char*         RCGlobal::k_SimpleTypeName_DMatrix3x3f = "DMatrix3x3f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DMatrix3x3f("DMatrix3x3f");

const Char*         RCGlobal::k_SimpleTypeName_DMatrix2x2f = "DMatrix2x2f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DMatrix2x2f("DMatrix2x2f");

const Char*         RCGlobal::k_SimpleTypeName_F32 = "F32";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_F32("F32");

const Char*         RCGlobal::k_SimpleTypeName_DVector2f = "DVector2f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVector2f("DVector2f");

const Char*         RCGlobal::k_SimpleTypeName_DVector3f = "DVector3f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVector3f("DVector3f");

const Char*         RCGlobal::k_SimpleTypeName_DVector4f = "DVector4f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVector4f("DVector4f");

const Char*         RCGlobal::k_SimpleTypeName_DVectorLut1f = "DVectorLut1f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVectorLut1f("DVectorLut1f");

const Char*         RCGlobal::k_SimpleTypeName_DVectorLut2f = "DVectorLut2f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVectorLut2f("DVectorLut2f");

const Char*         RCGlobal::k_SimpleTypeName_DVectorLut3f = "DVectorLut3f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVectorLut3f("DVectorLut3f");

const Char*         RCGlobal::k_SimpleTypeName_DVectorLut4f = "DVectorLut4f";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVectorLut4f("DVectorLut4f");

const Char*         RCGlobal::k_SimpleTypeName_DString = "DString";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DString("DString");

const Char*         RCGlobal::k_SimpleTypeName_Int = "Int";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_Int("Int");

const Char*         RCGlobal::k_SimpleTypeName_DVector2i = "DVector2i";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVector2i("DVector2i");

const Char*         RCGlobal::k_SimpleTypeName_DVector3i = "DVector3i";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVector3i("DVector3i");

const Char*         RCGlobal::k_SimpleTypeName_DVector4i = "DVector4i";
DSimpleTypeID       RCGlobal::k_SimpleTypeID_DVector4i("DVector4i");

RCGlobal::RCGlobal()
{
    m_SimpleTypeName_Unknown = k_SimpleTypeName_Unknown;

    m_SimpleTypeName_OSTexture2D = k_SimpleTypeName_OSTexture2D;
    m_SimpleTypeName_DMatrix4x4f = k_SimpleTypeName_DMatrix4x4f;
    m_SimpleTypeName_DMatrix3x3f = k_SimpleTypeName_DMatrix3x3f;
    m_SimpleTypeName_DMatrix2x2f = k_SimpleTypeName_DMatrix2x2f;

    m_SimpleTypeName_F32 = k_SimpleTypeName_F32;
    m_SimpleTypeName_DVector2f = k_SimpleTypeName_DVector2f;
    m_SimpleTypeName_DVector3f = k_SimpleTypeName_DVector3f;
    m_SimpleTypeName_DVector4f = k_SimpleTypeName_DVector4f;

    m_SimpleTypeName_DVectorLut1f = k_SimpleTypeName_DVectorLut1f;
    m_SimpleTypeName_DVectorLut2f = k_SimpleTypeName_DVectorLut2f;
    m_SimpleTypeName_DVectorLut3f = k_SimpleTypeName_DVectorLut3f;
    m_SimpleTypeName_DVectorLut4f = k_SimpleTypeName_DVectorLut4f;

    m_SimpleTypeName_DString = k_SimpleTypeName_DString;

    m_SimpleTypeName_Int = k_SimpleTypeName_Int;
    m_SimpleTypeName_DVector2i = k_SimpleTypeName_DVector2i;
    m_SimpleTypeName_DVector3i = k_SimpleTypeName_DVector3i;
    m_SimpleTypeName_DVector4i = k_SimpleTypeName_DVector4i;
}


RC_NAMESPACE_END