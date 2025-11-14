/*
    filename:       mdexecuter.h
    author:         Ming Dong
    date:           2016-JUN-04
    description:    
*/
#pragma once

#include "../public/iexecuter.h"

RC_NAMESPACE_BEGIN

class RCEffectManager;
class MDExecuter : public IExecuter
{
public:
    MDExecuter(RCEffectManager* i_pEffectManager, const DHashString& i_Signature);
    ~MDExecuter();

    virtual Int     getNumTextureParam() const DOME_OVERRIDE;
    virtual Int     getNumTexture3DParam() const DOME_OVERRIDE;
    virtual Int     getNumTextureCubeParam() const DOME_OVERRIDE;
    virtual Int     getNumMatrix4x4Param() const DOME_OVERRIDE;
    virtual Int     getNumMatrix3x3Param() const DOME_OVERRIDE;
    virtual Int     getNumMatrix2x2Param() const DOME_OVERRIDE;
    virtual Int     getNumFloat4Param() const DOME_OVERRIDE;
    virtual Int     getNumFloat3Param() const DOME_OVERRIDE;
    virtual Int     getNumFloat2Param() const DOME_OVERRIDE;
    virtual Int     getNumFloat1Param() const DOME_OVERRIDE;

    virtual DResult setRenderTarget(OSTexture2D i_Rt) DOME_OVERRIDE;
    virtual DResult setRenderTargetViewport(Int x, Int y, Int width, Int height) DOME_OVERRIDE;
    virtual DResult setBlendMode(RCBLENDMODE i_Mode) DOME_OVERRIDE;
    virtual DResult setUVCoef(const DVector4f& i_UVCoef) DOME_OVERRIDE;

    virtual DResult setTextureParam(Int i_Index, OSTexture2D i_Texture) DOME_OVERRIDE;
    virtual DResult setTexture3DParam(Int i_Index, OSTexture3D i_Texture) DOME_OVERRIDE;
    virtual DResult setTextureCubeParam(Int i_Index, OSTextureCube i_Texture) DOME_OVERRIDE;
    virtual DResult setMatrix4x4Param(Int i_Index, const DMatrix4x4f& i_Mat) DOME_OVERRIDE;
    virtual DResult setMatrix3x3Param(Int i_Index, const DMatrix3x3f& i_Mat) DOME_OVERRIDE;
    virtual DResult setMatrix2x2Param(Int i_Index, const DMatrix2x2f& i_Mat) DOME_OVERRIDE;
    virtual DResult setFloat4Param(Int i_Index, const DVector4f& i_Val) DOME_OVERRIDE;
    virtual DResult setFloat3Param(Int i_Index, const DVector3f& i_Val) DOME_OVERRIDE;
    virtual DResult setFloat2Param(Int i_Index, const DVector2f& i_Val) DOME_OVERRIDE;
    virtual DResult setFloat1Param(Int i_Index, F32 i_Val) DOME_OVERRIDE;

    virtual DResult execute() DOME_OVERRIDE;

    static DResult compile(const DString& i_Signature, DString& o_PSShader, Int& o_NumTex, Int& o_NumTex3D, Int& o_NumTexCube, Int& o_NumMat4x4, Int& o_NumMat3x3, Int& o_Mat2x2, Int& o_NumFloat4, Int& o_NumFloat3, Int& o_NumFloat2, Int& o_NumFloat);
private:
    DResult init();
    DResult uninit();


    RCEffectManager*            m_pEffectManager;
    DHashString                 m_Signature;

    // resource which need to be released
    OSConstBuffer               m_VSParams;
    OSPixelShader               m_PixelShader;
    OSConstBuffer               m_PSParams;
    OSRenderOperation           m_RO;

    DString                     m_PSShaderCode;
    OSTexture2D                 m_RenderTarget;
    DVector4i                   m_Viewport2D;
    RCBLENDMODE                 m_BlendMode;
    DVector4f                   m_UVCoef;
    TArray<OSTexture2D>         m_TextureParams;
    TArray<OSTexture3D>         m_Texture3DParams;
    TArray<OSTextureCube>       m_TextureCubeParams;
    TArray<DMatrix4x4f>         m_Matrix4x4Params;
    TArray<DMatrix3x3f>         m_Matrix3x3Params;
    TArray<DMatrix2x2f>         m_Matrix2x2Params;
    TArray<DVector4f>           m_Float4Params;
    TArray<DVector3f>           m_Float3Params;
    TArray<DVector2f>           m_Float2Params;
    TArray<F32>                 m_Float1Params;
};


RC_NAMESPACE_END