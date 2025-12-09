/*
    filename:       rcrenderer.h
    author:         Ming Dong
    date:           2016-MAR-22
    description:    
*/
#pragma once

#include "rcconfigure.h"

RC_NAMESPACE_BEGIN

// OS dependent data pointer
typedef void*       RCOSRendererDataPtr;

/*
    
*/
enum RCOSRENDERRESOURCETYPE           // continue with OS_Manager::OSRT_RENDERRESOURCE
{
    RCOSRRT_TEXTURE2D = OS_Manager::OSRT_RENDERRESOURCE,
    RCOSRRT_TEXTURE3D,
    RCOSRRT_TEXTURECUBE,
    RCOSRRT_VERTEXSHADER,
    RCOSRRT_PIXELSHADER,
    RCOSRRT_VERTEXLAYOUT,
    RCOSRRT_VERTEXBUFFER,
    RCOSRRT_INDEXBUFFER,
    RCOSRRT_CONSTBUFFER,
    RCOSRRT_RENDEROPERATION,

    RCOSRRT_MAX
};

class RCRenderer;

#define DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(RSNAME, RSTYPE, DLL_API)           \
class DLL_API RSNAME                                                                \
{                                                                                   \
public:                                                                             \
    OSHandle                m_OSHandle;                                             \
    RCRenderer*             m_pRCRenderer;                                          \
    RSNAME():m_OSHandle(),m_pRCRenderer(DM_NULL){}                                  \
    RSNAME(S16 i_Handle, RCRenderer* i_pRCRenderer)                                 \
    :m_OSHandle(RSTYPE,i_Handle)                                                    \
    ,m_pRCRenderer(i_pRCRenderer){}                                                 \
    RSNAME(const RSNAME& i_Other)                                                   \
    :m_OSHandle(i_Other.m_OSHandle)                                                 \
    ,m_pRCRenderer(i_Other.m_pRCRenderer){}                                         \
    ~RSNAME(){}                                                                     \
    void set(S16 i_Handle, RCRenderer* i_pRCRenderer)                               \
    {m_OSHandle.set(RSTYPE, i_Handle);m_pRCRenderer=i_pRCRenderer;}                 \
    RSNAME& operator=(const RSNAME& i_Other)                                        \
    {m_OSHandle = i_Other.m_OSHandle;                                               \
        m_pRCRenderer=i_Other.m_pRCRenderer;return *this;}                          \
    S16 getType() const{return m_OSHandle.getType();}                               \
    S16 getHandle() const{return m_OSHandle.getHandle();}                           \
    RCRenderer* getRenderer() const{return m_pRCRenderer;}                          \
    Bool isValid() const{return m_OSHandle.isValid()&&m_pRCRenderer;}               \
    Bool operator==(const RSNAME& i_Other) const                                    \
    {return m_OSHandle==i_Other.m_OSHandle&&m_pRCRenderer==i_Other.m_pRCRenderer;}  \
    Bool operator!=(const RSNAME& i_Other) const                                    \
    {return m_OSHandle != i_Other.m_OSHandle;}                                      \
};


DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSTexture2D, RCOSRRT_TEXTURE2D, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSTexture3D, RCOSRRT_TEXTURE3D, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSTextureCube, RCOSRRT_TEXTURECUBE, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSVertexShader, RCOSRRT_VERTEXSHADER, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSPixelShader, RCOSRRT_PIXELSHADER, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSVertexLayout, RCOSRRT_VERTEXLAYOUT, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSVertexBuffer, RCOSRRT_VERTEXBUFFER, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSIndexBuffer, RCOSRRT_INDEXBUFFER, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSConstBuffer, RCOSRRT_CONSTBUFFER, RC_API);
DOME_MAKE_UNIQUE_RCRENDERER_RESOURCETYPE(OSRenderOperation, RCOSRRT_RENDEROPERATION, RC_API);

// buffer usage
enum RCBUFFUSAGE : S32
{
    RBU_DEFAULT,
    RBU_IMMUTABLE,
    RBU_DYNAMIC,
    RBU_STAGE
};

/*
    texture format
    1) each gpu data can have one or more following component
        R: red component
        G: green component
        B: blue component
        A: alpha component
        D: depth component
        S: stencil component

    2) each data component or data can have one or more following postfix
        U: the data is unsigned data (default)
        S: the data is signed data

        N: the data is normalized (default)
        I: the data is integer
        F: the data is float
*/
enum RCGPUDATAFORMAT : S32
{
    RGDF_UNKNOWN,
    RGDF_RGBA8,
    RGDF_BGRA8,
    RGDF_RGBA16F,
    RGDF_RG32F,
    RGDF_D24S8,
    RGDF_D32F,
    RGDF_RGBA32F,
    RGDF_R32F
};

/*
    texture lock data, used in lockTexture function
*/
struct RCTexLockedRect
{
    Int     m_Pitch;
    void*   m_pBits;
};

/*
    texture lock style, 
*/
enum RCBUFFLOCKSTYLE
{
    RTLS_READONLY,
    RTLS_WRITEONLY,
    RTLS_READWRITE
};

/*
    Vertex element semantic
*/
enum RCVERTEXSEMANTIC
{
    RVS_POSITION,
    RVS_TEXTURE0,
    RVS_TEXTURE1,
    RVS_TEXTURE2,
    RVS_TEXTURE3,
    RVS_TEXTURE4,
    RVS_TEXTURE5,
    RVS_TEXTURE6,
    RVS_TEXTURE7,
    RVS_TEXTURE8,
    RVS_TEXTURE9,
    RVS_TEXTURE10,
    RVS_TEXTURE11,
    RVS_TEXTURE12,
    RVS_TEXTURE13,
    RVS_TEXTURE14,
    RVS_TEXTURE15
};

struct RCVertexElementDesc
{
    RCVERTEXSEMANTIC        m_Semantic;
    DStringHash             m_TypeID;       // m_TypeID should support U8, U16, U32, S8, S16, S32, F32, DVector2f, DVector3f, DVector4f
};

struct RCViewportInfo
{
    F32                     m_TopLeftX;
    F32                     m_TopLeftY;
    F32                     m_Width;
    F32                     m_Height;
    F32                     m_MinDepth;
    F32                     m_MaxDepth;
};

enum RCBLENDMODE
{
    RBM_UNKNOWN,
    RBM_COPY,
    RBM_ALPHABLEND,
    RBM_ADD,
    RBM_COUNT
};

enum RCCULLMODE
{
    RCM_CULL_NONE,
    RCM_CULL_FRONT,
    RCM_CULL_BACK
};

enum RCDEPTHTESTFUNC
{
    RC_DEPTHTEST_NEVER,
    RC_DEPTHTEST_LESS,
    RC_DEPTHTEST_EQUAL,
    RC_DEPTHTEST_LESS_EQUAL,
    RC_DEPTHTEST_GREATER,
    RC_DEPTHTEST_NOT_EQUAL,
    RC_DEPTHTEST_GREATER_EQUAL,
    RC_DEPTHTEST_ALWAYS
};

class RC_API RCRenderer
{
public:

public:
    virtual ~RCRenderer(){}

    virtual RCOSRendererDataPtr getOSRendererData() = 0;

    // Texture related functions, i_Mipmap=1 to disable multi mipmap, 0 to generate all mipmap levels, if i_Mipmap < 0, then the abs value is the mipmap number, and this texture should have feature to generate mipmaps
    virtual DResult             createTexture2D(OSTexture2D& o_Result, Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, Bool i_bTemp, void* i_pInitData = DM_NULL, int WriteFlag=0) = 0;
    // Generate mipmaps from a 2D texture
    virtual DResult             generateMipmapsFromTexture2D(OSTexture2D& o_Result, OSTexture2D i_Tex) = 0;
    // 1) Render target:NO  2) Texture:YES 3) ReadLocked:NO  4) WriteLock:NO  5) GpuCopyTo:NO  6) GpuCopyFrom:NO
    virtual DResult             createTexture2DFromFile(OSTexture2D& o_Result, const DString& i_TexFile) = 0;
    virtual void                refreshLoaded2DTexture(OSTexture2D i_Tex) = 0;
    // 1) Render target:NO  2) Texture:YES 3) ReadLocked:NO  4) WriteLock:NO  5) GpuCopyTo:NO  6) GpuCopyFrom:NO
    virtual DResult             createTexture2DExternal(OSTexture2D& o_Result, const void* i_Param) = 0;
    // destroy created texture
    virtual DResult             destroyTexture2D(OSTexture2D i_Tex) = 0;
    virtual DResult             clearTexturePool() = 0;

    virtual RCBUFFUSAGE         getTexture2DUsage(OSTexture2D i_Tex) = 0;
    virtual RCGPUDATAFORMAT     getTexture2DFormat(OSTexture2D i_Tex) = 0;
    virtual DResult             getTexture2DSize(OSTexture2D i_Tex, Int& o_Width, Int& o_Height) = 0;
    virtual DResult             getTexture2DSize(OSTexture2D i_Tex, Int i_Mipmap, Int& o_Width, Int& o_Height) = 0;
    virtual DResult             getTexture2DMipmaps(OSTexture2D i_Tex, Int& o_Mipmap) = 0;
    virtual Bool                isTexture2DTemp(OSTexture2D i_Tex) = 0;
    virtual Bool                isTexture2DRT(OSTexture2D i_Tex) = 0;
    virtual DResult             lockTexture2D(OSTexture2D i_Tex, Int i_Mipmap, RCBUFFLOCKSTYLE i_LockStyle, RCTexLockedRect& o_LockResult) = 0;
    virtual DResult             unlockTexture2D(OSTexture2D i_Tex, Int i_Mipmap) = 0;
    virtual DResult             copyTexture2D(OSTexture2D i_DestTex, OSTexture2D i_SrcTex) = 0;
    virtual DResult             copyTexture2DMipmap(OSTexture2D i_DestTex, Int i_DestMipmap, OSTexture2D i_SrcTex, Int i_SrcMipmap) = 0;

    // 3D Texture functions
    virtual DResult             createTexture3DFromFile(OSTexture3D& o_Result, const DString& i_TexFile) = 0;
    virtual DResult             destroyTexture3D(OSTexture3D i_Tex) = 0;

    // Cube Texture functions
    virtual DResult             createTextureCubeFromFile(OSTextureCube& o_Result, const DString& i_TexFile) = 0;
    virtual DResult             destroyTextureCube(OSTextureCube i_Tex) = 0;

    // vertex shader resource
    virtual DResult             createVertexShader(OSVertexShader& o_Result, const DString& i_Code, const DString& i_Entry, const DString& i_CompiledFile, const RCVertexElementDesc* i_ElementArray, Int i_NumElement) = 0;
    virtual DResult             destroyVertexShader(OSVertexShader i_VS) = 0;
    virtual OSVertexShader      getFullscreenVS() = 0;

    // pixel shader resource
    virtual DResult             createPixelShader(OSPixelShader& o_Result, const DString& i_Signature, const DString& i_Code, const DString& i_Entry, const DString& i_CompiledFile, DString& o_StringResult) = 0;
    virtual DResult             destroyPixelShader(OSPixelShader i_PS) = 0;

    // vertex buffer resource
    virtual DResult             createVertexBuffer(OSVertexBuffer& o_Result, Int i_VertexSize, Int i_NumVertex, RCBUFFUSAGE i_Usage) = 0;
    virtual DResult             destroyVertexBuffer(OSVertexBuffer i_VBuffer) = 0;
    virtual void*               lockVertexBuffer(OSVertexBuffer i_VBuffer, RCBUFFLOCKSTYLE i_LockStyle) = 0;
    virtual DResult             unlockVertexBuffer(OSVertexBuffer i_VBuffer) = 0;
    virtual OSVertexBuffer      getFullscreenVB() = 0;

    // index buffer resource
    virtual DResult             createIndexBuffer(OSIndexBuffer& o_Result, Int i_IndexNum, Bool i_b32Bit, RCBUFFUSAGE i_Usage) = 0;
    virtual DResult             destroyIndexBuffer(OSIndexBuffer i_IBuffer) = 0;
    virtual void*               lockIndexBuffer(OSIndexBuffer i_IBuffer, RCBUFFLOCKSTYLE i_LockStyle) = 0;
    virtual DResult             unlockIndexBuffer(OSIndexBuffer i_IBuffer) = 0;

    // const buffer resource
    virtual DResult             createConstBuffer(OSConstBuffer& o_Result, Int i_NumFloat4, RCBUFFUSAGE i_Usage) = 0;
    virtual DResult             destroyConstBuffer(OSConstBuffer i_CBuffer) = 0;
    virtual void*               lockConstBuffer(OSConstBuffer i_CBuffer, RCBUFFLOCKSTYLE i_LockStyle) = 0;
    virtual DResult             unlockConstBuffer(OSConstBuffer i_CBuffer) = 0;

    // common render functions
    virtual DResult             clearRenderTarget           (OSTexture2D i_Rt, const DVector4f& i_ClearColor) = 0;
    virtual DResult             setRenderTargets            (Int i_NumRt, const OSTexture2D* i_pRtArray, OSTexture2D i_DepthTex) = 0;
    virtual DResult             setViewports                (Int i_NumViewport, const RCViewportInfo* i_ViewportArray) = 0;

    virtual DResult             modifyBlendMode             (RCBLENDMODE i_Mode) = 0;
    virtual DResult             modifyDepthState            (Bool i_bDepthTest, Bool i_bDepthWrite, RCDEPTHTESTFUNC i_DepthFunc) = 0;
    virtual DResult             modifyCullMode              (RCCULLMODE i_CullMode, Bool i_bCCWFront) = 0;
    virtual DResult             modifyPipelineStateToDefault() = 0;
    virtual DResult             commitPipelineState         (Bool i_bForce) = 0;

    // render operation resource
    virtual DResult             createRenderOperation       (OSRenderOperation& o_Result) = 0;
    virtual DResult             destroyRenderOperation      (OSRenderOperation i_RO) = 0;
    virtual DResult             ro_SetRenderTarget          (OSRenderOperation i_RO, Int i_Index, OSTexture2D i_RT) = 0;
    virtual DResult             ro_GetRenderTarget          (OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_RT) = 0;
    virtual DResult             ro_SetDepthBuffer           (OSRenderOperation i_RO, OSTexture2D i_Depth) = 0;
    virtual DResult             ro_GetDepthBuffer           (OSRenderOperation i_RO, OSTexture2D& o_Depth) = 0;
    virtual DResult             ro_SetVertexShader          (OSRenderOperation i_RO, OSVertexShader i_VertexShader) = 0;
    virtual DResult             ro_GetVertexShader          (OSRenderOperation i_RO, OSVertexShader& o_VertexShader) = 0;
    virtual DResult             ro_SetPixelShader           (OSRenderOperation i_RO, OSPixelShader i_PixelShader) = 0;
    virtual DResult             ro_GetPixelShader           (OSRenderOperation i_RO, OSPixelShader& o_PixelShader) = 0;
    virtual DResult             ro_SetVertexBuffer          (OSRenderOperation i_RO, OSVertexBuffer i_VBuffer) = 0;
    virtual DResult             ro_GetVertexBuffer          (OSRenderOperation i_RO, OSVertexBuffer& o_VBuffer) = 0;
    virtual DResult             ro_SetIndexBuffer           (OSRenderOperation i_RO, OSIndexBuffer i_IBuffer) = 0;
    virtual DResult             ro_GetIndexBuffer           (OSRenderOperation i_RO, OSIndexBuffer& o_IBuffer) = 0;
    virtual DResult             ro_SetVS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer i_CBuffer) = 0;
    virtual DResult             ro_GetVS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer& o_CBuffer) = 0;
    virtual DResult             ro_SetVS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D i_Texture) = 0;
    virtual DResult             ro_GetVS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_Texture) = 0;
    virtual DResult             ro_SetVS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D i_Texture) = 0;
    virtual DResult             ro_GetVS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D& o_Texture) = 0;
    virtual DResult             ro_SetVS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube i_Texture) = 0;
    virtual DResult             ro_GetVS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube& o_Texture) = 0;
    virtual DResult             ro_SetPS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer i_CBuffer) = 0;
    virtual DResult             ro_GetPS_ConstBuffer        (OSRenderOperation i_RO, Int i_Index, OSConstBuffer& o_CBuffer) = 0;
    virtual DResult             ro_SetPS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D i_Texture) = 0;
    virtual DResult             ro_GetPS_Texture            (OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_Texture) = 0;
    virtual DResult             ro_SetPS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D i_Texture) = 0;
    virtual DResult             ro_GetPS_Texture3D          (OSRenderOperation i_RO, Int i_Index, OSTexture3D& o_Texture) = 0;
    virtual DResult             ro_SetPS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube i_Texture) = 0;
    virtual DResult             ro_GetPS_TextureCube        (OSRenderOperation i_RO, Int i_Index, OSTextureCube& o_Texture) = 0;

    virtual DResult             ro_SetRS_EnableDepthTest    (OSRenderOperation i_RO, Bool i_bEnable = DM_FALSE) = 0;
    virtual DResult             ro_SetRS_EnableDepthWrite   (OSRenderOperation i_RO, Bool i_bEnable = DM_FALSE) = 0;
    virtual DResult				ro_SetRS_BlendMode          (OSRenderOperation i_RO, RCBLENDMODE i_Mode) = 0;
    virtual DResult             ro_SetRS_CullMode           (OSRenderOperation i_RO, RCCULLMODE i_Mode, Bool i_bCCWFront) = 0;
    virtual DResult             ro_SetViewport              (OSRenderOperation i_RO, F32 x, F32 y, F32 width, F32 height, F32 i_MinDepth, F32 i_MaxDepth) = 0;

    virtual DResult             ro_Execute                  (OSRenderOperation i_RO) = 0;

    // Call back functions
    virtual DResult             beginFrame() = 0;
    virtual DResult             endFrame() = 0;

    // Helper functions
    virtual DResult             getDataPath(DString& o_Result) const = 0;
};

RC_NAMESPACE_END