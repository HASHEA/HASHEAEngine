/*
    filename:       iexecuter.h
    author:         Ming Dong
    date:           2016-JUN-29
    description:    
*/
#pragma once

#include "rcrenderer.h"

RC_NAMESPACE_BEGIN

class IExecuter
{
public:
    virtual Int     getNumTextureParam() const = 0;
    virtual Int     getNumTexture3DParam() const = 0;
    virtual Int     getNumTextureCubeParam() const = 0;
    virtual Int     getNumMatrix4x4Param() const = 0;
    virtual Int     getNumMatrix3x3Param() const = 0;
    virtual Int     getNumMatrix2x2Param() const = 0;
    virtual Int     getNumFloat4Param() const = 0;
    virtual Int     getNumFloat3Param() const = 0;
    virtual Int     getNumFloat2Param() const = 0;
    virtual Int     getNumFloat1Param() const = 0;

    virtual DResult setRenderTarget(OSTexture2D i_Rt) = 0;
    virtual DResult setRenderTargetViewport(Int x, Int y, Int width, Int height) = 0;
    virtual DResult setBlendMode(RCBLENDMODE i_Mode) = 0;
    virtual DResult setUVCoef(const DVector4f& i_UVCoef) = 0;

    virtual DResult setTextureParam(Int i_Index, OSTexture2D i_Texture) = 0;
    virtual DResult setTexture3DParam(Int i_Index, OSTexture3D i_Texture) = 0;
    virtual DResult setTextureCubeParam(Int i_Index, OSTextureCube i_Texture) = 0;
    virtual DResult setMatrix4x4Param(Int i_Index, const DMatrix4x4f& i_Mat) = 0;
    virtual DResult setMatrix3x3Param(Int i_Index, const DMatrix3x3f& i_Mat) = 0;
    virtual DResult setMatrix2x2Param(Int i_Index, const DMatrix2x2f& i_Mat) = 0;
    virtual DResult setFloat4Param(Int i_Index, const DVector4f& i_Val) = 0;
    virtual DResult setFloat3Param(Int i_Index, const DVector3f& i_Val) = 0;
    virtual DResult setFloat2Param(Int i_Index, const DVector2f& i_Val) = 0;
    virtual DResult setFloat1Param(Int i_Index, F32 i_Val) = 0;

    virtual DResult execute() = 0;

};


RC_NAMESPACE_END