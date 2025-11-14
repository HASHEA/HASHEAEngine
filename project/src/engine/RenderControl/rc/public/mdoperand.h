/*
    filename:       mdoperand.h
    author:         Ming Dong
    date:           2016-APR-08
    description:    
*/
#pragma once

#include "rcrenderer.h"
#include "microdata.h"

DOME_NAMESPACE_BEGIN

class RC_API MDOperand : public MicroData
{
public:
    MDOperand()
        : MicroData(MDT_OPERAND)
    {
    }
    virtual ~MDOperand(){}

    // functions need to be implemented in child class
    virtual Bool                        isOperation() const = 0;
    virtual Int                         getDataCount() const = 0;
    virtual DSimpleTypeID               getDataType(Int i_Index) const = 0;
    // if getDataCount() > 1, call the following functions to get data
    virtual const MDOperand*            getSubOperand(Int i_Index) const = 0;
    virtual MDOperand*                  getSubOperand(Int i_Index) = 0;
    // if getDataCount() == 1, call the following functions to get data
    virtual const DSimpleTypedValue*    getDataPtr() const = 0;
    virtual DSimpleTypedValue*          getDataPtr() = 0;
    // special functions for texture data
    virtual RCGPUDATAFORMAT             getTexDataFmt(Int i_Index) const = 0;
    virtual DVector2i                   getTexDataSize(Int i_Index) const = 0;

    // helper functions
    Bool                                isTexture() const;
    DResult                             getTextureSize(DVector2i& o_Size) const;
    DResult                             getTextureFormat(RCGPUDATAFORMAT& o_Format) const;
    DResult                             getTexture(OSTexture2D& o_Texture) const;
    const OSTexture2D*                  getTexturePtr() const;
    Bool                                isTexture3D() const;
    DResult                             getTexture3D(OSTexture3D& o_Value) const;
    const OSTexture3D*                  getTexture3DPtr() const;
    Bool                                isTextureCube() const;
    DResult                             getTextureCube(OSTextureCube& o_Value) const;
    const OSTextureCube*                getTextureCubePtr() const;
    Bool                                isFloat() const;
    DResult                             getFloat(F32& o_Value) const;
    const F32*                          getFloatPtr() const;
    Bool                                isFloat2() const;
    DResult                             getFloat2(DVector2f& o_Value) const;
    const DVector2f*                    getFloat2Ptr() const;
    Bool                                isFloat3() const;
    DResult                             getFloat3(DVector3f& o_Value) const;
    const DVector3f*                    getFloat3Ptr() const;
    Bool                                isFloat4() const;
    DResult                             getFloat4(DVector4f& o_Value) const;
    const DVector4f*                    getFloat4Ptr() const;

    Bool                                isMatrix4x4() const;
    DResult                             getMatrix4x4(DMatrix4x4f& o_Value) const;
    const DMatrix4x4f*                  getMatrix4x4Ptr() const;

    Bool                                isMatrix3x3() const;
    DResult                             getMatrix3x3(DMatrix3x3f& o_Value) const;
    const DMatrix3x3f*                  getMatrix3x3Ptr() const;

    Bool                                isMatrix2x2() const;
    DResult                             getMatrix2x2(DMatrix2x2f& o_Value) const;
    const DMatrix2x2f*                  getMatrix2x2Ptr() const;
};

typedef const MDOperand*                MDOperandCPtr;
typedef MDOperand*                      MDOperandPtr;

DOME_NAMESPACE_END