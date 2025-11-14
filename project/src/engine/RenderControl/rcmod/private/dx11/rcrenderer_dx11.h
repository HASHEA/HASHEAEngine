/*
    filename:       rcrenderer_dx11.h
    author:         Ming Dong
    date:           2016-MAY-13
    description:    
*/
#pragma once

#include "../../public/rcmod.h"
#include <rc/public/rc.h>
#include "rctexture2dpool_dx11.h"

RC_NAMESPACE_BEGIN

DXGI_FORMAT getPlatformFormat(RCGPUDATAFORMAT i_Format);

class RCRenderer_DX11 : public RCRenderer
{
public:
    RCRenderer_DX11(const RCOSRendererData& i_RendererData);
    virtual ~RCRenderer_DX11();
    virtual RCOSRendererDataPtr getOSRendererData();


public: // FUNCTIONS FROM RCRenderer
    // Texture related functions
    virtual DResult             createTexture2D(OSTexture2D& o_Result, Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, Bool i_bTemp, void* i_pInitData = DM_NULL, int i_WriteFlag = 0);
    virtual DResult             generateMipmapsFromTexture2D(OSTexture2D& o_Result, OSTexture2D i_Tex);
    virtual DResult             createTexture2DFromFile(OSTexture2D& o_Result, const DString& i_TexFile);
    virtual void                refreshLoaded2DTexture(OSTexture2D i_Tex);
    virtual DResult             createTexture2DExternal(OSTexture2D& o_Result, const void* i_Param);
    virtual DResult             destroyTexture2D(OSTexture2D i_Tex);
    virtual DResult             clearTexturePool();
    virtual RCBUFFUSAGE         getTexture2DUsage(OSTexture2D i_Tex);
    virtual RCGPUDATAFORMAT     getTexture2DFormat(OSTexture2D i_Tex);
    virtual DResult             getTexture2DSize(OSTexture2D i_Tex, Int& o_Width, Int& o_Height);
    virtual DResult             getTexture2DSize(OSTexture2D i_Tex, Int i_Mipmap, Int& o_Width, Int& o_Height);
    virtual DResult             getTexture2DMipmaps(OSTexture2D i_Tex, Int& o_Mipmap);
    virtual Bool                isTexture2DTemp(OSTexture2D i_Tex);
    virtual Bool                isTexture2DRT(OSTexture2D i_Tex);
    virtual DResult             lockTexture2D(OSTexture2D i_Tex, Int i_Mipmap, RCBUFFLOCKSTYLE i_LockStyle, RCTexLockedRect& o_LockResult);
    virtual DResult             unlockTexture2D(OSTexture2D i_Tex, Int i_Mipmap);
    virtual DResult             copyTexture2D(OSTexture2D i_DestTex, OSTexture2D i_SrcTex);
    virtual DResult             copyTexture2DMipmap(OSTexture2D i_DestTex, Int i_DestMipmap, OSTexture2D i_SrcTex, Int i_SrcMipmap);

    // 3D Texture functions
    virtual DResult             createTexture3DFromFile(OSTexture3D& o_Result, const DString& i_TexFile);
    virtual DResult             destroyTexture3D(OSTexture3D i_Tex);

    // Cube Texture functions
    virtual DResult             createTextureCubeFromFile(OSTextureCube& o_Result, const DString& i_TexFile);
    virtual DResult             destroyTextureCube(OSTextureCube i_Tex);

    // vertex shader resource
    virtual DResult             createVertexShader(OSVertexShader& o_Result, const DString& i_Code, const DString& i_Entry, const DString& i_CompiledFile, const RCVertexElementDesc* i_ElementArray, Int i_NumElement);
    virtual DResult             destroyVertexShader(OSVertexShader i_VS);
    virtual OSVertexShader      getFullscreenVS();

    // pixel shader resource
    virtual DResult             createPixelShader(OSPixelShader& o_Result, const DString& i_Signature, const DString& i_Code, const DString& i_Entry, const DString& i_CompiledFile, DString& o_StringResult);
    virtual DResult             destroyPixelShader(OSPixelShader i_PS);

    // vertex buffer resource
    virtual DResult             createVertexBuffer(OSVertexBuffer& o_Result, Int i_VertexSize, Int i_NumVertex, RCBUFFUSAGE i_Usage);
    virtual DResult             destroyVertexBuffer(OSVertexBuffer i_VBuffer);
    virtual void*               lockVertexBuffer(OSVertexBuffer i_VBuffer, RCBUFFLOCKSTYLE i_LockStyle);
    virtual DResult             unlockVertexBuffer(OSVertexBuffer i_VBuffer);
    virtual OSVertexBuffer      getFullscreenVB();

    // index buffer resource
    virtual DResult             createIndexBuffer(OSIndexBuffer& o_Result, Int i_IndexNum, Bool i_b32Bit, RCBUFFUSAGE i_Usage);
    virtual DResult             destroyIndexBuffer(OSIndexBuffer i_IBuffer);
    virtual void*               lockIndexBuffer(OSIndexBuffer i_IBuffer, RCBUFFLOCKSTYLE i_LockStyle);
    virtual DResult             unlockIndexBuffer(OSIndexBuffer i_IBuffer);

    // const buffer resource
    virtual DResult             createConstBuffer(OSConstBuffer& o_Result, Int i_NumFloat4, RCBUFFUSAGE i_Usage);
    virtual DResult             destroyConstBuffer(OSConstBuffer i_CBuffer);
    virtual void*               lockConstBuffer(OSConstBuffer i_CBuffer, RCBUFFLOCKSTYLE i_LockStyle);
    virtual DResult             unlockConstBuffer(OSConstBuffer i_CBuffer);

    // common render functions
    virtual DResult             clearRenderTarget           (OSTexture2D i_Rt, const DVector4f& i_ClearColor);
    virtual DResult             setRenderTargets            (Int i_NumRt, const OSTexture2D* i_pRtArray, OSTexture2D i_DepthTex);
    virtual DResult             setViewports                (Int i_NumViewport, const RCViewportInfo* i_ViewportArray);

    virtual DResult             modifyBlendMode             (RCBLENDMODE i_Mode);
    virtual DResult             modifyDepthState            (Bool i_bDepthTest, Bool i_bDepthWrite, RCDEPTHTESTFUNC i_DepthFunc);
    virtual DResult             modifyCullMode              (RCCULLMODE i_CullMode, Bool i_bCCWFront);
    virtual DResult             modifyPipelineStateToDefault();
    virtual DResult             commitPipelineState         (Bool i_bForce);

    // render operation resource
    virtual DResult             createRenderOperation       (OSRenderOperation& o_Result);
    virtual DResult             destroyRenderOperation      (OSRenderOperation i_RO);
    virtual DResult             ro_SetRenderTarget          (OSRenderOperation i_RO, Int i_Index, OSTexture2D i_RT);
    virtual DResult             ro_GetRenderTarget          (OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_RT);
    virtual DResult             ro_SetDepthBuffer           (OSRenderOperation i_RO, OSTexture2D i_Depth);
    virtual DResult             ro_GetDepthBuffer           (OSRenderOperation i_RO, OSTexture2D& o_Depth);
    virtual DResult             ro_SetVertexShader          (OSRenderOperation i_RO, OSVertexShader i_VertexShader);
    virtual DResult             ro_GetVertexShader          (OSRenderOperation i_RO, OSVertexShader& o_VertexShader);
    virtual DResult             ro_SetPixelShader           (OSRenderOperation i_RO, OSPixelShader i_PixelShader);
    virtual DResult             ro_GetPixelShader           (OSRenderOperation i_RO, OSPixelShader& o_PixelShader);
    virtual DResult             ro_SetVertexBuffer          (OSRenderOperation i_RO, OSVertexBuffer i_VBuffer);
    virtual DResult             ro_GetVertexBuffer          (OSRenderOperation i_RO, OSVertexBuffer& o_VBuffer);
    virtual DResult             ro_SetIndexBuffer           (OSRenderOperation i_RO, OSIndexBuffer i_IBuffer);
    virtual DResult             ro_GetIndexBuffer           (OSRenderOperation i_RO, OSIndexBuffer& o_IBuffer);
    virtual DResult             ro_SetVS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer i_CBuffer);
    virtual DResult             ro_GetVS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer& o_CBuffer);
    virtual DResult             ro_SetVS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D i_Texture);
    virtual DResult             ro_GetVS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_Texture);
    virtual DResult             ro_SetVS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D i_Texture);
    virtual DResult             ro_GetVS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D& o_Texture);
    virtual DResult             ro_SetVS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube i_Texture);
    virtual DResult             ro_GetVS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube& o_Texture);
    virtual DResult             ro_SetPS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer i_CBuffer);
    virtual DResult             ro_GetPS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer& o_CBuffer);
    virtual DResult             ro_SetPS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D i_Texture);
    virtual DResult             ro_GetPS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_Texture);
    virtual DResult             ro_SetPS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D i_Texture);
    virtual DResult             ro_GetPS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D& o_Texture);
    virtual DResult             ro_SetPS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube i_Texture);
    virtual DResult             ro_GetPS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube& o_Texture);

    virtual DResult             ro_SetRS_EnableDepthTest    (OSRenderOperation i_RO, Bool i_bEnable = DM_FALSE);
    virtual DResult             ro_SetRS_EnableDepthWrite   (OSRenderOperation i_RO, Bool i_bEnable = DM_FALSE);
    virtual DResult				ro_SetRS_BlendMode          (OSRenderOperation i_RO, RCBLENDMODE i_Mode);
    virtual DResult             ro_SetRS_CullMode           (OSRenderOperation i_RO, RCCULLMODE i_Mode, Bool i_bCCWFront);
    virtual DResult             ro_SetViewport              (OSRenderOperation i_RO, F32 x, F32 y, F32 width, F32 height, F32 i_MinDepth, F32 i_MaxDepth);

    virtual DResult             ro_Execute                  (OSRenderOperation i_RO);

    // Call back functions
    virtual DResult             beginFrame(){return R_SUCCESS;};
    virtual DResult             endFrame(){return R_SUCCESS;};

    // Helper functions
    virtual DResult             getDataPath(DString& o_Result) const
    {
        o_Result = RCGlobal::Instance().m_RCDataRootPath;
        return R_SUCCESS;
    }

public: // dx11 specific functions
    //i_Mipmap=1 to disable multi mipmap, 0 to generate all mipmap levels
    static DResult              DX11CreateTexture2D(RCOSTextureData& o_TexData, ID3D11Device* i_pD3D11Device, Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, void* i_pInitData = DM_NULL);
    static DResult              DX11CreateDepthTexture2D(RCOSTextureData& o_TexData, ID3D11Device* i_pD3D11Device, Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format);
    DResult                     DX11LoadTexture2D(ID3D11Device* i_pD3D11Device, const DString& i_FileName, RCOSTextureData& o_TexData, Int& o_Width, Int& o_Height, Int& o_Mipmap, RCGPUDATAFORMAT& o_Format, Int& o_VideoMemSize);

    static DResult              DX11LoadTexture3D(ID3D11Device* i_pD3D11Device, const DString& i_FileName, RCOSTextureData& o_TexData, Int& o_Width, Int& o_Height, Int& o_Depth, Int& o_Mipmap, RCGPUDATAFORMAT& o_Format, Int& o_VideoMemSize);
    static DResult              DX11LoadTextureCube(ID3D11Device* i_pD3D11Device, const DString& i_FileName, RCOSTextureData& o_TexData, Int& o_Width, Int& o_Height, Int& o_Depth, Int& o_Mipmap, RCGPUDATAFORMAT& o_Format, Int& o_VideoMemSize);

    ID3D11Texture2D*            getTexResourceDX11(OSTexture2D i_Tex);
    ID3D11ShaderResourceView*   getTexShaderResourceViewDX11(OSTexture2D i_Tex);
    ID3D11ShaderResourceView*   getTex3DShaderResourceViewDX11(OSTexture3D i_Tex);
    ID3D11ShaderResourceView*   getTexCubeShaderResourceViewDX11(OSTextureCube i_Tex);
    ID3D11RenderTargetView*     getTexRenderTargetViewDX11(OSTexture2D i_Tex);
    ID3D11DepthStencilView*     getTexDepthStencilViewDX11(OSTexture2D i_Tex);
    ID3D11UnorderedAccessView*  getTexUAVDX11(OSTexture2D i_Tex);

    DResult                     executeBegin();
    DResult                     executeEnd();
    DResult                     getTextureUsedInfo(Int& o_TexLoaded, Int& o_TexFromPool, Int& o_TexCreated, Int& o_TexImported);

public:
    static const Int k_DDSPoolSize = 1024;
    struct _DDSLoadInfoDX11
    {
        Int                 m_RefCount;
        DString             m_Path;
        Int                 m_Width;
        Int                 m_Height;
        Int                 m_Depth;
        Int                 m_MipmapsCount;
        Int                 m_LayerCount;
        Bool                m_bCubemap;
        DXGI_FORMAT         m_Format;
        D3D11_RESOURCE_DIMENSION    m_Dimension;
        ID3D11Resource*     m_pDX11Resource;
        ID3D11ShaderResourceView* m_pDX11SRV;
    };
    static _DDSLoadInfoDX11 m_DDSPool[k_DDSPoolSize];

    static void*                LoadDDSTexture(ID3D11Device* i_pD3D11Device, const DString& i_FileName, ID3D11Resource** o_ppDX11Resource, ID3D11ShaderResourceView** o_ppDX11SRV, D3D11_RESOURCE_DIMENSION& o_Dim, Int& o_Width, Int& o_Height, Int& o_Depth, Int& o_Mipmap, Int& o_LayerCount, DXGI_FORMAT& o_Format);
    static void                 UnloadDDSTexture(void* i_pData);

private:
    // global data
    RCOSRendererData            m_OSRendererData;
    RCTexture2DPool_DX11        m_WorkSurfacePool;
    IShaderCache*               m_pShaderCache;

    // created dx11 resources
    OSVertexShader              m_FullscreenVS;
    OSVertexBuffer              m_FullscreenVB;
    ID3D11BlendState*           m_DX11BlendState[RBM_COUNT];
    struct _DX11RasterizerStateInfo
    {
        RCCULLMODE              m_CullMode;
        Bool                    m_bCCWFront;
        ID3D11RasterizerState*  m_pDX11RasterizerState;
    };
    TArray<_DX11RasterizerStateInfo>    m_DX11RasterizerStateArray;

    // Current Render State
    RCBLENDMODE                 m_CurBlendMode;
    Bool                        m_bCurDepthTest;
    Bool                        m_bCurDepthWrite;
    RCDEPTHTESTFUNC             m_CurDepthFunc;
    RCCULLMODE                  m_CurCullMode;
    Bool                        m_bCurCCWFront;


    // texture resource
    static const Int            k_MaxTextureRes = 256;
    enum _TEXTURECAT : S32
    {
        TEXCAT_NORMAL,
        TEXCAT_LOADFROMFILE,
        TEXCAT_TEMP,
        TEXCAT_EXTERNAL
    };
    struct _TextureRes : public DOSResourceInfo
    {
        Int                     m_Width;
        Int                     m_Height;
        Int                     m_Mipmap;
        RCGPUDATAFORMAT         m_Format;
        RCBUFFUSAGE             m_Usage;
        _TEXTURECAT             m_TexCat;
        Int                     m_TempTexID;
        Int                     m_VideoMemSize;
        RCOSTextureData         m_TextureData;
    };
    Int                         m_FreeTextureRes;
    _TextureRes                 m_TextureResPool[k_MaxTextureRes];

    static const Int            k_MaxTexture3DRes = 256;
    struct _Texture3DRes : public DOSResourceInfo
    {
        Int                     m_Width;
        Int                     m_Height;
        Int                     m_Depth;
        Int                     m_Mipmap;
        RCGPUDATAFORMAT         m_Format;
        RCBUFFUSAGE             m_Usage;
        _TEXTURECAT             m_TexCat;
        Int                     m_TempTexID;
        Int                     m_VideoMemSize;
        RCOSTextureData         m_TextureData;
    };
    Int                         m_FreeTexture3DRes;
    _Texture3DRes               m_Texture3DResPool[k_MaxTexture3DRes];

    static const Int            k_MaxTextureCubeRes = 256;
    struct _TextureCubeRes : public DOSResourceInfo
    {
        Int                     m_Width;
        Int                     m_Height;
        Int                     m_Depth;
        Int                     m_Mipmap;
        RCGPUDATAFORMAT         m_Format;
        RCBUFFUSAGE             m_Usage;
        _TEXTURECAT             m_TexCat;
        Int                     m_TempTexID;
        Int                     m_VideoMemSize;
        RCOSTextureData         m_TextureData;
    };
    Int                         m_FreeTextureCubeRes;
    _TextureCubeRes             m_TextureCubeResPool[k_MaxTextureCubeRes];

    // vertex shader resource
    static const Int            k_MaxVertexShaderRes = 256;
    struct _VertexShaderRes : public DOSResourceInfo
    {
        ID3D11VertexShader*     m_pVertexShader;
        ID3D11InputLayout*      m_pVertexInputLayout;
    };
    Int                         m_FreeVertexShaderRes;
    _VertexShaderRes            m_VertexShaderResPool[k_MaxVertexShaderRes];

    // pixel shader resource
    static const Int            k_MaxPixelShaderRes = 1024;
    struct _PixelShaderRes : public DOSResourceInfo
    {
        ID3D11PixelShader*      m_pPixelShader;
    };
    Int                         m_FreePixelShaderRes;
    _PixelShaderRes             m_PixelShaderResPool[k_MaxPixelShaderRes];

    // vertex buffer resource
    static const Int            k_MaxVertexBufferRes = 256;
    struct _VertexBufferRes : public DOSResourceInfo
    {
        RCBUFFUSAGE             m_Usage;
        Int                     m_VertexSize;
        Int                     m_NumVertex;
        ID3D11Buffer*           m_pVertexBuffer;
    };
    Int                         m_FreeVertexBufferRes;
    _VertexBufferRes            m_VertexBufferResPool[k_MaxVertexBufferRes];


    // index buffer resource
    static const Int            k_MaxIndexBufferRes = 256;
    struct _IndexBufferRes : public DOSResourceInfo
    {
        RCBUFFUSAGE             m_Usage;
        Bool                    m_b32Bit;
        Int                     m_NumIndex;
        ID3D11Buffer*           m_pIndexBuffer;
    };
    Int                         m_FreeIndexBufferRes;
    _IndexBufferRes            m_IndexBufferResPool[k_MaxIndexBufferRes];

    // const buffer resource
    static const Int            k_MaxConstBufferRes = 256;
    struct _ConstBufferRes : public DOSResourceInfo
    {
        RCBUFFUSAGE             m_Usage;
        ID3D11Buffer*           m_pConstBuffer;
    };
    Int                         m_FreeConstBufferRes;
    _ConstBufferRes            m_ConstBufferResPool[k_MaxConstBufferRes];

    // render operation resource
    static const Int            k_MaxRenderOperationRes = 256;
    static const Int            k_MaxRenderTarget = 8;
    static const Int            k_MaxConstBuffer = 8;
    static const Int            k_MaxTexture = 16;
    static const Int            k_MaxTexture3D = 4;
    static const Int            k_MaxTextureCube = 2;
    struct _RenderOperationRes : public DOSResourceInfo
    {
        OSTexture2D             m_RenderTargets[k_MaxRenderTarget];
        OSTexture2D             m_DepthTexture;
        OSVertexShader          m_VertexShader;
        OSPixelShader           m_PixelShader;
        OSVertexBuffer          m_VertexBuffer;
        OSIndexBuffer           m_IndexBuffer;
        OSConstBuffer           m_VSConstBuffer[k_MaxConstBuffer];
        OSTexture2D             m_VSTextures[k_MaxTexture];
        OSTexture3D             m_VSTextures3D[k_MaxTexture3D];
        OSTextureCube           m_VSTexturesCube[k_MaxTextureCube];
        OSConstBuffer           m_PSConstBuffer[k_MaxConstBuffer];
        OSTexture2D             m_PSTextures[k_MaxTexture];
        OSTexture3D             m_PSTextures3D[k_MaxTexture3D];
        OSTextureCube           m_PSTexturesCube[k_MaxTextureCube];
        Bool                    m_bDepthTest;
        Bool                    m_bDepthWrite;
        RCBLENDMODE             m_BlendMode;
        RCCULLMODE              m_CullMode;
        Bool                    m_bCCWFront;
        D3D11_VIEWPORT          m_D3D11Viewport;
        ID3D11DepthStencilState* m_pDepthStencilState; 
    };
    Int                         m_FreeRenderOperationRes;
    _RenderOperationRes         m_RenderOperationResPool[k_MaxRenderOperationRes];

    Int                         m_TextureLoaded;
    Int                         m_TextureFromPool;
    Int                         m_TextureCreated;
    Int                         m_TextureImported;
    Int                         m_CurTextureLoaded;
    Int                         m_CurTextureFromPool;
    Int                         m_CurTextureCreated;
    Int                         m_CurTextureImported;
};


RC_NAMESPACE_END