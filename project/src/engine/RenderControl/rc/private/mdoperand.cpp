/*
    filename:       mdoperand.cpp
    author:         Ming Dong
    date:           2016-APR-11
    description:    
*/

#include "../public/mdoperand.h"
#include "../public/mdoperation.h"
#include "../public/rcrenderer.h"
#include "../public/rcglobal.h"

DOME_NAMESPACE_BEGIN

Bool                                MDOperand::isTexture() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_OSTexture2D;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getTextureSize(DVector2i& o_Size) const
{
    Bool l_bIsTex = isTexture();
    if(!l_bIsTex)
        return R_FAILED;

    o_Size = getTexDataSize(0);
    return R_SUCCESS;
}

DResult                             MDOperand::getTextureFormat(RCGPUDATAFORMAT& o_Format) const
{
    Bool l_bIsTex = isTexture();
    if(!l_bIsTex)
        return R_FAILED;

    o_Format = getTexDataFmt(0);
    return R_SUCCESS;
}

DResult                             MDOperand::getTexture(OSTexture2D& o_Texture) const
{
    Bool l_bIsTex = isTexture();
    if(!l_bIsTex)
        return R_FAILED;

    if (getDataPtr())
    {
        o_Texture = getDataPtr()->getValue<OSTexture2D>();
        return R_SUCCESS;
    }
    else
        return R_FAILED;
}

const OSTexture2D*                  MDOperand::getTexturePtr() const
{
    if (isTexture())
    {
        if(getDataPtr())
            return getDataPtr()->getValuePtr<OSTexture2D>();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isTexture3D() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_OSTexture3D;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getTexture3D(OSTexture3D& o_Value) const
{
    if (isTexture3D())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getValue<OSTexture3D>();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const OSTexture3D*                  MDOperand::getTexture3DPtr() const
{
    if (isTexture3D())
    {
        if (getDataPtr())
            return getDataPtr()->getValuePtr<OSTexture3D>();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isTextureCube() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_OSTextureCube;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getTextureCube(OSTextureCube& o_Value) const
{
    if (isTextureCube())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getValue<OSTextureCube>();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const OSTextureCube* MDOperand::getTextureCubePtr() const
{
    if (isTextureCube())
    {
        if (getDataPtr())
            return getDataPtr()->getValuePtr<OSTextureCube>();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isFloat() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_F32;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getFloat(F32& o_Value) const
{
    if (isFloat())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getF32();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const F32*                          MDOperand::getFloatPtr() const
{
    if (isFloat())
    {
        if(getDataPtr())
            return getDataPtr()->getF32Ptr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isFloat2() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_DVector2f;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getFloat2(DVector2f& o_Value) const
{
    if (isFloat2())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getDVector2f();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const DVector2f*                    MDOperand::getFloat2Ptr() const
{
    if (isFloat2())
    {
        if(getDataPtr())
            return getDataPtr()->getDVector2fPtr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isFloat3() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_DVector3f;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getFloat3(DVector3f& o_Value) const
{
    if (isFloat3())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getDVector3f();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const DVector3f*                    MDOperand::getFloat3Ptr() const
{
    if (isFloat3())
    {
        if(getDataPtr())
            return getDataPtr()->getDVector3fPtr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isFloat4() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_DVector4f;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getFloat4(DVector4f& o_Value) const
{
    if (isFloat4())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getDVector4f();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const DVector4f*                    MDOperand::getFloat4Ptr() const
{
    if (isFloat4())
    {
        if(getDataPtr())
            return getDataPtr()->getDVector4fPtr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isMatrix4x4() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_DMatrix4x4f;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getMatrix4x4(DMatrix4x4f& o_Value) const
{
    if (isMatrix4x4())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getDMatrix4x4f();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const DMatrix4x4f*                  MDOperand::getMatrix4x4Ptr() const
{
    if (isMatrix4x4())
    {
        if(getDataPtr())
            return getDataPtr()->getDMatrix4x4fPtr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isMatrix3x3() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_DMatrix3x3f;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getMatrix3x3(DMatrix3x3f& o_Value) const
{
    if (isMatrix3x3())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getDMatrix3x3f();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const DMatrix3x3f*                  MDOperand::getMatrix3x3Ptr() const
{
    if (isMatrix3x3())
    {
        if(getDataPtr())
            return getDataPtr()->getDMatrix3x3fPtr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

Bool                                MDOperand::isMatrix2x2() const
{
    if (getDataCount() == 1)
    {
        return getDataType(0) == RCGlobal::k_SimpleTypeID_DMatrix2x2f;
    }
    return DM_FALSE;
}

DResult                             MDOperand::getMatrix2x2(DMatrix2x2f& o_Value) const
{
    if (isMatrix2x2())
    {
        if (getDataPtr())
        {
            o_Value = getDataPtr()->getDMatrix2x2f();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }
    else
        return R_TYPEMISMATCH;
}

const DMatrix2x2f*                  MDOperand::getMatrix2x2Ptr() const
{
    if (isMatrix2x2())
    {
        if(getDataPtr())
            return getDataPtr()->getDMatrix2x2fPtr();
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}


DOME_NAMESPACE_END