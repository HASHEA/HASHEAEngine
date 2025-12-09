#include "pch.h"
/*
    filename:       rcrenderer_dx11.cpp
    author:         Ming Dong
    date:           2016-MAY-13
    description:    
*/

#include "rcrenderer_dx11.h"
#include "d3dcompiler.h"

#include "DevEnv/Internal/DirectXTex/D3DStats.h"
#include "KGCommon/Publish/Include/KG_FrameStats.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

RCRenderer_DX11::RCRenderer_DX11(const RCOSRendererData& i_RendererData)
    :m_WorkSurfacePool(i_RendererData.m_pDevice, i_RendererData.m_pTexturePool)
    ,m_pShaderCache(i_RendererData.m_pShaderCache)
{
    m_OSRendererData = i_RendererData;

    m_TextureLoaded = 0;
    m_TextureFromPool = 0;
    m_TextureCreated = 0;
    m_TextureImported = 0;
    m_CurTextureLoaded = 0;
    m_CurTextureFromPool = 0;
    m_CurTextureCreated = 0;
    m_CurTextureImported = 0;
    // texture resource init
    for (Int i = 0; i < k_MaxTextureRes; ++i)
    {
        m_TextureResPool[i].m_bFree = DM_TRUE;
        m_TextureResPool[i].m_Width = 0;
        m_TextureResPool[i].m_Height = 0;
        m_TextureResPool[i].m_Mipmap = 1;
        m_TextureResPool[i].m_Format = RGDF_UNKNOWN;
        m_TextureResPool[i].m_TexCat = TEXCAT_NORMAL;
        m_TextureResPool[i].m_TempTexID = -1;
        m_TextureResPool[i].m_VideoMemSize = 0;
        m_TextureResPool[i].m_TextureData.m_pRenderTargetView = DM_NULL;
        m_TextureResPool[i].m_TextureData.m_pShaderResourceView = DM_NULL;
        m_TextureResPool[i].m_TextureData.m_pTexture = DM_NULL;
    }
    m_FreeTextureRes = k_MaxTextureRes;

	// texture 3d resource init
	for (Int i = 0; i < k_MaxTexture3DRes; ++i)
	{
		m_Texture3DResPool[i].m_bFree = DM_TRUE;
		m_Texture3DResPool[i].m_Width = 0;
		m_Texture3DResPool[i].m_Height = 0;
		m_Texture3DResPool[i].m_Depth = 0;
		m_Texture3DResPool[i].m_Mipmap = 1;
		m_Texture3DResPool[i].m_Format = RGDF_UNKNOWN;
		m_Texture3DResPool[i].m_TexCat = TEXCAT_NORMAL;
		m_Texture3DResPool[i].m_TempTexID = -1;
        m_Texture3DResPool[i].m_VideoMemSize = 0;
		m_Texture3DResPool[i].m_TextureData.m_pRenderTargetView = DM_NULL;
		m_Texture3DResPool[i].m_TextureData.m_pShaderResourceView = DM_NULL;
		m_Texture3DResPool[i].m_TextureData.m_pTexture = DM_NULL;
	}
	m_FreeTexture3DRes = k_MaxTexture3DRes;

    // texture cube resource init
    for (Int i = 0; i < k_MaxTextureCubeRes; ++i)
    {
        m_TextureCubeResPool[i].m_bFree = DM_TRUE;
        m_TextureCubeResPool[i].m_Width = 0;
        m_TextureCubeResPool[i].m_Height = 0;
        m_TextureCubeResPool[i].m_Depth = 0;
        m_TextureCubeResPool[i].m_Mipmap = 1;
        m_TextureCubeResPool[i].m_Format = RGDF_UNKNOWN;
        m_TextureCubeResPool[i].m_TexCat = TEXCAT_NORMAL;
        m_TextureCubeResPool[i].m_TempTexID = -1;
        m_TextureCubeResPool[i].m_VideoMemSize = 0;
        m_TextureCubeResPool[i].m_TextureData.m_pRenderTargetView = DM_NULL;
        m_TextureCubeResPool[i].m_TextureData.m_pShaderResourceView = DM_NULL;
        m_TextureCubeResPool[i].m_TextureData.m_pTexture = DM_NULL;
    }
    m_FreeTextureCubeRes = k_MaxTextureCubeRes;

    // vertex shader resource init
    for (Int i = 0; i < k_MaxVertexShaderRes; ++i)
    {
        m_VertexShaderResPool[i].m_bFree = DM_TRUE;
        m_VertexShaderResPool[i].m_pVertexShader = DM_NULL;
        m_VertexShaderResPool[i].m_pVertexInputLayout = DM_NULL;
    }
    m_FreeVertexShaderRes = k_MaxVertexShaderRes;

    // pixel shader resource init
    for (Int i = 0; i < k_MaxPixelShaderRes; ++i)
    {
        m_PixelShaderResPool[i].m_bFree = DM_TRUE;
        m_PixelShaderResPool[i].m_pPixelShader = DM_NULL;
    }
    m_FreePixelShaderRes = k_MaxPixelShaderRes;

    // vertex buffer resource init
    for (Int i = 0; i < k_MaxVertexBufferRes; ++i)
    {
        m_VertexBufferResPool[i].m_bFree = DM_TRUE;
        m_VertexBufferResPool[i].m_pVertexBuffer = DM_NULL;
    }
    m_FreeVertexBufferRes = k_MaxVertexBufferRes;

    // index buffer resource init
    for (Int i = 0; i < k_MaxIndexBufferRes; ++i)
    {
        m_IndexBufferResPool[i].m_bFree = DM_TRUE;
        m_IndexBufferResPool[i].m_pIndexBuffer = DM_NULL;
    }
    m_FreeIndexBufferRes = k_MaxIndexBufferRes;

    // const buffer resource init
    for (Int i = 0; i < k_MaxConstBufferRes; ++i)
    {
        m_ConstBufferResPool[i].m_bFree = DM_TRUE;
        m_ConstBufferResPool[i].m_pConstBuffer = DM_NULL;
    }
    m_FreeConstBufferRes = k_MaxConstBufferRes;

    // render operation buffer resource init
    for (Int i = 0; i < k_MaxRenderOperationRes; ++i)
    {
        m_RenderOperationResPool[i].m_bFree = DM_TRUE;
        m_RenderOperationResPool[i].m_bDepthTest = DM_FALSE;
        m_RenderOperationResPool[i].m_bDepthWrite = DM_FALSE;
    }
    m_FreeRenderOperationRes = k_MaxRenderOperationRes;


    // create full screen vertex shader
    DResult l_Result;
    RCVertexElementDesc l_VED[] = {
        {RVS_POSITION, DStringHash("DVector2f")},
        {RVS_TEXTURE0, DStringHash("DVector2f")}
    };
    const Char* l_pFullscreenVS = "\
struct VS_INPUT                                             \n\
{                                                           \n\
    float2 Pos : POSITION;                                  \n\
    float2 Tex : TEXCOORD0;                                 \n\
};                                                          \n\
struct PS_INPUT                                             \n\
{                                                           \n\
    float4 Pos : SV_POSITION;                               \n\
    float2 Tex : TEXCOORD0;                                 \n\
};                                                          \n\
cbuffer cbShaderParamBlock : register( b0 )                 \n\
{                                                           \n\
	float4        UVCoef;                                   \n\
};                                                          \n\
PS_INPUT RCVSMain( VS_INPUT input )                         \n\
{                                                           \n\
    PS_INPUT output = (PS_INPUT)0;                          \n\
    output.Pos = float4(input.Pos, 0.5, 1);                 \n\
    output.Tex = input.Tex * UVCoef.xy + UVCoef.zw;         \n\
    return output;                                          \n\
}                                                           \n\
";

    DString l_DataPath;
    getDataPath(l_DataPath);
    l_Result = createVertexShader(m_FullscreenVS, l_pFullscreenVS, "RCVSMain", l_DataPath + "shadergen\\rcvsshader.vs", l_VED, 2);
    DOME_ASSERT(DM_SUCC(l_Result));

    // create full screen vertex buffer
    struct _RCFullscreenVertex
    {
        DVector2f       m_Position;
        DVector2f       m_UV;
    };
    _RCFullscreenVertex l_Vertices[] = 
    {
        {DVector2f( -1.0f,  1.0f ), DVector2f(  0.0f,  0.0f ) },
        {DVector2f(  1.0f,  1.0f ), DVector2f(  1.0f,  0.0f ) },
        {DVector2f(  1.0f, -1.0f ), DVector2f(  1.0f,  1.0f ) },

        {DVector2f( -1.0f,  1.0f ), DVector2f(  0.0f,  0.0f ) },
        {DVector2f(  1.0f, -1.0f ), DVector2f(  1.0f,  1.0f ) },
        {DVector2f( -1.0f, -1.0f ), DVector2f(  0.0f,  1.0f ) },
    };

    l_Result = createVertexBuffer(m_FullscreenVB, sizeof(_RCFullscreenVertex), 6, RBU_DYNAMIC);
    DOME_ASSERT(DM_SUCC(l_Result));

    _RCFullscreenVertex* l_pVB = (_RCFullscreenVertex*)lockVertexBuffer(m_FullscreenVB, RTLS_WRITEONLY);
    for (Int i = 0; i < 6; ++i)
    {
        l_pVB[i].m_Position = l_Vertices[i].m_Position;
        l_pVB[i].m_UV = l_Vertices[i].m_UV;
    }
    unlockVertexBuffer(m_FullscreenVB);

    // create all dx11 blend state
    HRESULT hr;
    m_DX11BlendState[RBM_UNKNOWN] = DM_NULL;

    D3D11_BLEND_DESC l_BlendDesc_Copy;
    l_BlendDesc_Copy.AlphaToCoverageEnable = DM_FALSE;
    l_BlendDesc_Copy.IndependentBlendEnable = DM_FALSE;
    l_BlendDesc_Copy.RenderTarget[0].BlendEnable = DM_FALSE;
    l_BlendDesc_Copy.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    l_BlendDesc_Copy.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    l_BlendDesc_Copy.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    l_BlendDesc_Copy.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    l_BlendDesc_Copy.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    l_BlendDesc_Copy.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    l_BlendDesc_Copy.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_OSRendererData.m_pDevice->CreateBlendState(&l_BlendDesc_Copy, &m_DX11BlendState[RBM_COPY]);
    DOME_ASSERT(S_OK == hr);

    D3D11_BLEND_DESC l_BlendDesc_AlphaBlend;
    l_BlendDesc_AlphaBlend.AlphaToCoverageEnable = DM_FALSE;
    l_BlendDesc_AlphaBlend.IndependentBlendEnable = DM_FALSE;
    l_BlendDesc_AlphaBlend.RenderTarget[0].BlendEnable = DM_TRUE;
    l_BlendDesc_AlphaBlend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    l_BlendDesc_AlphaBlend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    l_BlendDesc_AlphaBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    l_BlendDesc_AlphaBlend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    l_BlendDesc_AlphaBlend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    l_BlendDesc_AlphaBlend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    l_BlendDesc_AlphaBlend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_OSRendererData.m_pDevice->CreateBlendState(&l_BlendDesc_AlphaBlend, &m_DX11BlendState[RBM_ALPHABLEND]);
    DOME_ASSERT(S_OK == hr);

    D3D11_BLEND_DESC l_BlendDesc_Add;
    l_BlendDesc_Add.AlphaToCoverageEnable = DM_FALSE;
    l_BlendDesc_Add.IndependentBlendEnable = DM_FALSE;
    l_BlendDesc_Add.RenderTarget[0].BlendEnable = DM_TRUE;
    l_BlendDesc_Add.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    l_BlendDesc_Add.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    l_BlendDesc_Add.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    l_BlendDesc_Add.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    l_BlendDesc_Add.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    l_BlendDesc_Add.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    l_BlendDesc_Add.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_OSRendererData.m_pDevice->CreateBlendState(&l_BlendDesc_Add, &m_DX11BlendState[RBM_ADD]);
    DOME_ASSERT(S_OK == hr);

	// set to default pipeline state
	modifyPipelineStateToDefault();
}

RCRenderer_DX11::~RCRenderer_DX11()
{
    m_DX11BlendState[RBM_COPY]->Release();
    m_DX11BlendState[RBM_COPY] = DM_NULL;
    m_DX11BlendState[RBM_ALPHABLEND]->Release();
    m_DX11BlendState[RBM_ALPHABLEND] = DM_NULL;
    m_DX11BlendState[RBM_ADD]->Release();
    m_DX11BlendState[RBM_ADD] = DM_NULL;

	for (int i = 0; i < m_DX11RasterizerStateArray.size(); ++i)
	{
		m_DX11RasterizerStateArray[i].m_pDX11RasterizerState->Release();
	}
	m_DX11RasterizerStateArray.clear();


    destroyVertexShader(m_FullscreenVS);
    destroyVertexBuffer(m_FullscreenVB);
}

RCOSRendererDataPtr RCRenderer_DX11::getOSRendererData()
{
    return &m_OSRendererData;
}

// vertex shader resource
DResult             RCRenderer_DX11::createVertexShader(OSVertexShader& o_Result, const DString& i_Code, const DString& i_Entry, const DString& i_CompiledFile, const RCVertexElementDesc* i_ElementArray, Int i_NumElement)
{
    if(m_FreeVertexShaderRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxVertexShaderRes; ++i)
    {
        if (m_VertexShaderResPool[i].m_bFree)
        {
            HRESULT     l_Hr;
            ID3DBlob*   l_pErrorBlob = NULL;
            ID3DBlob*   l_pVSBlob = NULL;
            DWORD       dwShaderFlags = D3D10_SHADER_OPTIMIZATION_LEVEL0|D3D10_SHADER_DEBUG;//D3DCOMPILE_ENABLE_STRICTNESS;
            DString     l_ShaderFile = "rcvshader";

            if (i_CompiledFile.size() > 0)
            {
                DFile l_File(i_CompiledFile);
                if (DM_SUCC(l_File.create(DM_FALSE)))
                {
                    Int l_NumWrite = i_Code.size();
                    l_File.write(i_Code.c_str(), l_NumWrite);
                    DOME_ASSERT(l_NumWrite == i_Code.size());

                    l_File.close();
                }
                l_ShaderFile = i_CompiledFile;
            }

            // compile vertex shader
            l_Hr = D3DCompile(i_Code.c_str(), i_Code.getCharCount(), l_ShaderFile.c_str(), NULL, NULL, i_Entry.c_str(), "vs_5_0", dwShaderFlags, 0, &l_pVSBlob, &l_pErrorBlob);
            DOME_ASSERT( SUCCEEDED( l_Hr ) );
            RC_RELEASE(l_pErrorBlob);

            // create vertex shader
            l_Hr = m_OSRendererData.m_pDevice->CreateVertexShader( l_pVSBlob->GetBufferPointer(), l_pVSBlob->GetBufferSize(), NULL, &m_VertexShaderResPool[i].m_pVertexShader);
            DOME_ASSERT( SUCCEEDED( l_Hr ) );

            // create vertex shader input layout
            TArray<D3D11_INPUT_ELEMENT_DESC> l_LayoutElement;
            Int l_CurAlignedByte = 0;
            for (Int n = 0; n < i_NumElement; ++n)
            {
                D3D11_INPUT_ELEMENT_DESC l_Element;

                DXGI_FORMAT l_Format;
                Int l_DataSize;
                const static DStringHash k_TypeID_U8("U8");
                const static DStringHash k_TypeID_U16("U16");
                const static DStringHash k_TypeID_U32("U32");
                const static DStringHash k_TypeID_S8("S8");
                const static DStringHash k_TypeID_S16("S16");
                const static DStringHash k_TypeID_S32("S32");
                const static DStringHash k_TypeID_F32("F32");
                const static DStringHash k_TypeID_DVector2f("DVector2f");
                const static DStringHash k_TypeID_DVector3f("DVector3f");
                const static DStringHash k_TypeID_DVector4f("DVector4f");


                if (i_ElementArray[n].m_TypeID == k_TypeID_U8){l_Format = DXGI_FORMAT_R8_UINT;l_DataSize = 1;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_U16){l_Format = DXGI_FORMAT_R16_UINT;l_DataSize = 2;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_U32){l_Format = DXGI_FORMAT_R32_UINT;l_DataSize = 4;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_S8){l_Format = DXGI_FORMAT_R8_SINT;l_DataSize = 1;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_S16){l_Format = DXGI_FORMAT_R16_SINT;l_DataSize = 2;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_S32){l_Format = DXGI_FORMAT_R32_SINT;l_DataSize = 4;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_F32){l_Format = DXGI_FORMAT_R32_FLOAT;l_DataSize = 4;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_DVector2f){l_Format = DXGI_FORMAT_R32G32_FLOAT;l_DataSize = 8;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_DVector3f){l_Format = DXGI_FORMAT_R32G32B32_FLOAT;l_DataSize = 12;}
                else if (i_ElementArray[n].m_TypeID == k_TypeID_DVector4f){l_Format = DXGI_FORMAT_R32G32B32A32_FLOAT;l_DataSize = 16;}
                else
                {
                    DOME_ASSERT(0);
                }

                switch (i_ElementArray[n].m_Semantic)
                {
                case RVS_POSITION:
                    {
                        l_Element.SemanticName = "POSITION";
                        l_Element.SemanticIndex = 0;

                        l_Element.Format = l_Format;
                        l_Element.InputSlot = 0;
                        l_Element.AlignedByteOffset = l_CurAlignedByte;
                        l_CurAlignedByte += l_DataSize;
                        l_Element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                        l_Element.InstanceDataStepRate = 0;
                    }
                    break;
                case RVS_TEXTURE0:
                case RVS_TEXTURE1:
                case RVS_TEXTURE2:
                case RVS_TEXTURE3:
                case RVS_TEXTURE4:
                case RVS_TEXTURE5:
                case RVS_TEXTURE6:
                case RVS_TEXTURE7:
                case RVS_TEXTURE8:
                case RVS_TEXTURE9:
                case RVS_TEXTURE10:
                case RVS_TEXTURE11:
                case RVS_TEXTURE12:
                case RVS_TEXTURE13:
                case RVS_TEXTURE14:
                case RVS_TEXTURE15:
                    {
                        l_Element.SemanticName = "TEXCOORD";
                        l_Element.SemanticIndex = i_ElementArray[n].m_Semantic - RVS_TEXTURE0;

                        l_Element.Format = l_Format;
                        l_Element.InputSlot = 0;
                        l_Element.AlignedByteOffset = l_CurAlignedByte;
                        l_CurAlignedByte += l_DataSize;
                        l_Element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                        l_Element.InstanceDataStepRate = 0;
                    }
                    break;
                }
                l_LayoutElement.push_back(l_Element);
            }

            l_Hr = m_OSRendererData.m_pDevice->CreateInputLayout( &l_LayoutElement[0], i_NumElement, l_pVSBlob->GetBufferPointer(),
                                            l_pVSBlob->GetBufferSize(), &m_VertexShaderResPool[i].m_pVertexInputLayout );
            DOME_ASSERT( SUCCEEDED(l_Hr) );
            RC_RELEASE(l_pVSBlob);

            m_VertexShaderResPool[i].m_bFree = DM_FALSE;
            m_FreeVertexShaderRes --;
            o_Result.set(i, this);
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyVertexShader(OSVertexShader i_VS)
{
    Int l_Index = i_VS.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxVertexShaderRes);
    DOME_ASSERT(!m_VertexShaderResPool[l_Index].m_bFree);

    RC_RELEASE(m_VertexShaderResPool[l_Index].m_pVertexInputLayout);
    RC_RELEASE(m_VertexShaderResPool[l_Index].m_pVertexShader);
    m_VertexShaderResPool[l_Index].m_bFree = DM_TRUE;
    m_FreeVertexShaderRes ++;

    return R_SUCCESS;
}

OSVertexShader      RCRenderer_DX11::getFullscreenVS()
{
    return m_FullscreenVS;
}

/*DECLARE_INTERFACE_(RCPixelShaderInclude, ID3DInclude)
{
    RCPixelShaderInclude(RCRenderer* i_pRenderer)
        : m_pRenderer(i_pRenderer)
    {

    }

    STDMETHOD(Open)(THIS_ D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) override
    {
        DString l_FullPath;
        m_pRenderer->getDataPath(l_FullPath);
        l_FullPath += pFileName;
        DFile l_IncFile(l_FullPath);
        if (DM_SUCC(l_IncFile.open(DM_FALSE)))
        {
            Int l_FileSize = l_IncFile.getLength();
            l_IncFile.seek(DM_TRUE, 0);
            *ppData = DOME_Alloc(l_FileSize);
            l_IncFile.read((Char*)*ppData, l_FileSize);
            *pBytes = l_FileSize;
            l_IncFile.close();
            return S_OK;
        }
        return S_FALSE;
    }

    STDMETHOD(Close)(THIS_ LPCVOID pData) override
    {
        DOME_Free((void*)pData);
        return S_OK;
    }

    RCRenderer*         m_pRenderer;
};*/

class RCPixelShaderInclude : public ID3DInclude
{
public:
    RCPixelShaderInclude(RCRenderer* i_pRenderer)
        : m_pRenderer(i_pRenderer)
    {

    }

    HRESULT __stdcall Open(THIS_ D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
    {
        DString l_FullPath;
        m_pRenderer->getDataPath(l_FullPath);
        l_FullPath += "mdos\\";
        l_FullPath += pFileName;
        DFile l_IncFile(l_FullPath, DOME_GetExternalFS());
        if (DM_SUCC(l_IncFile.open(DM_FALSE)))
        {
            Int l_FileSize = l_IncFile.getLength();
            l_IncFile.seek(DM_TRUE, 0);
            *ppData = DOME_Alloc(l_FileSize);
            l_IncFile.read((Char*)*ppData, l_FileSize);
            *pBytes = l_FileSize;
            l_IncFile.close();
            return S_OK;
        }
        return S_FALSE;
    }

    HRESULT __stdcall Close(THIS_ LPCVOID pData)
    {
        DOME_Free((void*)pData);
        return S_OK;
    }

    RCRenderer*         m_pRenderer;
};

void ReportMsg(HRESULT l_Hr, Uint l_Hash, const DString& i_Code, bool l_bActualCompile)
{
	if (FAILED(l_Hr)/* || true*/)
	{
		char szGuid[256];
		GUID l_Guid_t;
		memset(&l_Guid_t, 0, sizeof(GUID));
		memcpy(&l_Guid_t, &l_Hash, sizeof(l_Hash));
		int nRetCode = sprintf_s(szGuid, 256, "{%08x-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}",
			l_Guid_t.Data1,
			l_Guid_t.Data2, l_Guid_t.Data3,
			l_Guid_t.Data4[0], l_Guid_t.Data4[1],
			l_Guid_t.Data4[2], l_Guid_t.Data4[3], l_Guid_t.Data4[4], l_Guid_t.Data4[5], l_Guid_t.Data4[6], l_Guid_t.Data4[7]
		);
        if (nRetCode < 0)
            goto Exit0;

		szGuid[nRetCode] = '\0';

		char szMsg[512] = { 0 };
		const char* szContent = i_Code.c_str();
		int nContentLen = (int)strlen(szContent);
		sprintf_s(szMsg, "HR:%x\r\nHash:%s\r\nActualCompile:%d\r\nShaderLen:%d\r\n", l_Hr, szGuid, l_bActualCompile, nContentLen);
		MessageBoxA(NULL, szMsg, "请联系客服，并发送截图 和 客户端下的rc_shader_msg文件。", 0);

		FILE* fp = NULL;
		if (fopen_s(&fp, "rc_shader_msg.txt", "wb") == 0 && fp)
		{
			fprintf_s(fp, "HR:%x\r\nHash:%s\r\nActualCompile:%d\r\nShader:%s\r\n", l_Hr, szGuid, l_bActualCompile, i_Code.c_str());
			fclose(fp);
			fp = NULL;
		}
	}
Exit0:
    return;
}

// pixel shader resource
DResult             RCRenderer_DX11::createPixelShader(OSPixelShader& o_Result, const DString& i_Signature, const DString& i_Code, const DString& i_Entry, const DString& i_CompiledFile, DString& o_StringResult)
{
    RCPixelShaderInclude l_IncCallback(this);

    if(m_FreePixelShaderRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxPixelShaderRes; ++i)
    {
        if (m_PixelShaderResPool[i].m_bFree)
        {
            HRESULT l_Hr;
            DWORD dwShaderFlags = D3D10_SHADER_OPTIMIZATION_LEVEL0|D3D10_SHADER_DEBUG;//D3DCOMPILE_ENABLE_STRICTNESS;
            ID3DBlob* l_pErrorBlob = NULL;
            ID3DBlob* l_pPSBlob = NULL;
            DString     l_ShaderFile = "rcpshader";

            if (!m_pShaderCache)
            {
                if (i_CompiledFile.size() > 0)
                {
                    DFile l_File(i_CompiledFile);
                    if (DM_SUCC(l_File.create(DM_FALSE)))
                    {
                        Int l_NumWrite = i_Code.size();
                        l_File.write(i_Code.c_str(), l_NumWrite);
                        DOME_ASSERT(l_NumWrite == i_Code.size());

                        l_File.close();
                    }
                    l_ShaderFile = i_CompiledFile;
                }

                l_Hr = D3DCompile(i_Code.c_str(), i_Code.size(), l_ShaderFile.c_str(), NULL, &l_IncCallback, i_Entry.c_str(), "ps_5_0", dwShaderFlags, 0, &l_pPSBlob, &l_pErrorBlob);
                DOME_ASSERT(SUCCEEDED(l_Hr));
                if (FAILED(l_Hr))
                {
                    const char* l_pErrorMsg = NULL;
                    l_pErrorMsg = (char*)l_pErrorBlob->GetBufferPointer();

const char* l_DefaultPS = "\
float4 RCPSMain(float4 i_Pos : SV_POSITION, float2 i_UV : TEXCOORD0) : SV_TARGET0           \
{                                                                                           \
    return float4(1,0,0,1);                                                                 \
}                                                                                           \
";
                    RC_RELEASE(l_pErrorBlob);
                    l_Hr = D3DCompile(l_DefaultPS, strlen(l_DefaultPS), l_ShaderFile.c_str(), NULL, &l_IncCallback, i_Entry.c_str(), "ps_5_0", dwShaderFlags, 0, &l_pPSBlob, &l_pErrorBlob);
                    DOME_ASSERT(SUCCEEDED(l_Hr));
                }
                RC_RELEASE(l_pErrorBlob);

                l_Hr = m_OSRendererData.m_pDevice->CreatePixelShader( l_pPSBlob->GetBufferPointer(), l_pPSBlob->GetBufferSize(), NULL, &m_PixelShaderResPool[i].m_pPixelShader);
                DOME_ASSERT( SUCCEEDED( l_Hr ) );
                RC_RELEASE(l_pPSBlob);
            }
            else
            {
				GUID l_Guid;
				char l_GuidStr[128];
				Uint l_Hash = OS_String::TStrHashFnv1_Uint(i_Signature.c_str());


				memset(&l_Guid, 0, sizeof(GUID));
				memcpy(&l_Guid, &l_Hash, sizeof(l_Hash));


                {
                    FILE* l_pFile = NULL;
                    if (fopen_s(&l_pFile, "debugrcshader.flag", "r") == 0)
                    {
                        fclose(l_pFile);
                        l_pFile = NULL;


						sprintf_s(l_GuidStr, sizeof(l_GuidStr), "logs\\rcshadergen\\{%08x-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}.rcshader",
							l_Guid.Data1,
							l_Guid.Data2, l_Guid.Data3,
							l_Guid.Data4[0], l_Guid.Data4[1],
							l_Guid.Data4[2], l_Guid.Data4[3], l_Guid.Data4[4], l_Guid.Data4[5], l_Guid.Data4[6], l_Guid.Data4[7]
						);

                        if (fopen_s(&l_pFile, l_GuidStr, "wb") == 0)
                        {
                            fprintf(l_pFile, "%s\n\n%s", i_CompiledFile.c_str(), i_Code.c_str());
                            fclose(l_pFile);
                        }
                    }
                }



                void* l_pBuffer = DM_NULL;
                int l_BuffLen = 0;
                void* l_pShader = m_pShaderCache->getCacheShader(i_Signature.c_str(), l_Guid, &l_pBuffer, &l_BuffLen, i_Code.c_str(), i_Code.size(), i_Entry.c_str(), &l_IncCallback, "ps_5_0");
                DOME_ASSERT(l_pShader);

                l_Hr = m_OSRendererData.m_pDevice->CreatePixelShader( l_pBuffer, l_BuffLen, NULL, &m_PixelShaderResPool[i].m_pPixelShader);
                DOME_ASSERT( SUCCEEDED( l_Hr ) );

                m_pShaderCache->releaseCacheShader(l_pShader);
            }

            m_PixelShaderResPool[i].m_bFree = DM_FALSE;
            m_FreePixelShaderRes --;
            o_Result.set(i, this);
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyPixelShader(OSPixelShader i_PS)
{
    Int l_Index = i_PS.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxVertexShaderRes);
    DOME_ASSERT(!m_PixelShaderResPool[l_Index].m_bFree);

    RC_RELEASE(m_PixelShaderResPool[l_Index].m_pPixelShader);
    m_PixelShaderResPool[l_Index].m_bFree = DM_TRUE;
    m_FreePixelShaderRes ++;

    return R_SUCCESS;
}

// vertex buffer resource
DResult             RCRenderer_DX11::createVertexBuffer(OSVertexBuffer& o_Result, Int i_VertexSize, Int i_NumVertex, RCBUFFUSAGE i_Usage)
{
    if(m_FreeVertexBufferRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxVertexBufferRes; ++i)
    {
        if (m_VertexBufferResPool[i].m_bFree)
        {
            HRESULT l_Hr;
            D3D11_BUFFER_DESC l_BufDesc;
            ZeroMemory( &l_BufDesc, sizeof(l_BufDesc) );
            //TODO: always use dynamic here, but I should support other later
            l_BufDesc.Usage = D3D11_USAGE_DYNAMIC;
            l_BufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            l_BufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            l_BufDesc.ByteWidth = i_VertexSize * i_NumVertex;
            l_Hr = DS_CreateBuffer(m_OSRendererData.m_pDevice,  &l_BufDesc, DM_NULL, &m_VertexBufferResPool[i].m_pVertexBuffer );
            DOME_ASSERT( SUCCEEDED( l_Hr ) );

            m_VertexBufferResPool[i].m_bFree = DM_FALSE;
            m_FreeVertexBufferRes --;
            m_VertexBufferResPool[i].m_Usage = i_Usage;
            m_VertexBufferResPool[i].m_VertexSize = i_VertexSize;
            m_VertexBufferResPool[i].m_NumVertex = i_NumVertex;
            o_Result.set(i, this);
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyVertexBuffer(OSVertexBuffer i_VBuffer)
{
    Int l_Index = i_VBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxVertexBufferRes);
    DOME_ASSERT(!m_VertexBufferResPool[l_Index].m_bFree);

    RC_RELEASE(m_VertexBufferResPool[l_Index].m_pVertexBuffer);
    m_VertexBufferResPool[l_Index].m_bFree = DM_TRUE;
    m_FreeVertexBufferRes ++;
    m_VertexBufferResPool[l_Index].m_Usage = RBU_DEFAULT;
    m_VertexBufferResPool[l_Index].m_VertexSize = 0;
    m_VertexBufferResPool[l_Index].m_NumVertex = 0;

    return R_SUCCESS;
}

OSVertexBuffer      RCRenderer_DX11::getFullscreenVB()
{
    return m_FullscreenVB;
}

void*               RCRenderer_DX11::lockVertexBuffer(OSVertexBuffer i_VBuffer, RCBUFFLOCKSTYLE i_LockStyle)
{
    Int l_Index = i_VBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxVertexBufferRes);
    DOME_ASSERT(!m_VertexBufferResPool[l_Index].m_bFree);

    HRESULT l_Hr;
    D3D11_MAPPED_SUBRESOURCE l_mapss;
    D3D11_MAP l_map = D3D11_MAP_READ_WRITE;
    if(i_LockStyle == RTLS_READONLY)
        l_map = D3D11_MAP_READ;
    else if(i_LockStyle == RTLS_WRITEONLY)
        l_map = D3D11_MAP_WRITE_DISCARD;
    else if(i_LockStyle == RTLS_READWRITE)
        l_map = D3D11_MAP_READ_WRITE;

    l_Hr = DS_Map(m_OSRendererData.m_pDeviceContext, m_VertexBufferResPool[l_Index].m_pVertexBuffer, 0, l_map, 0, &l_mapss);
    if (!SUCCEEDED(l_Hr))
    {
        return DM_NULL;
    }

    return l_mapss.pData;
}

DResult             RCRenderer_DX11::unlockVertexBuffer(OSVertexBuffer i_VBuffer)
{
    Int l_Index = i_VBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxVertexBufferRes);
    DOME_ASSERT(!m_VertexBufferResPool[l_Index].m_bFree);

    DS_Unmap(m_OSRendererData.m_pDeviceContext, m_VertexBufferResPool[l_Index].m_pVertexBuffer, 0);
    return R_SUCCESS;
}

// index buffer resource
DResult             RCRenderer_DX11::createIndexBuffer(OSIndexBuffer& o_Result, Int i_IndexNum, Bool i_b32Bit, RCBUFFUSAGE i_Usage)
{
    if(m_FreeIndexBufferRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxIndexBufferRes; ++i)
    {
        if (m_IndexBufferResPool[i].m_bFree)
        {
            HRESULT l_Hr;
            D3D11_BUFFER_DESC l_BufDesc;
            ZeroMemory( &l_BufDesc, sizeof(l_BufDesc) );
            //TODO: always use dynamic here, but I should support other later
            l_BufDesc.Usage = D3D11_USAGE_DYNAMIC;
            l_BufDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            l_BufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            l_BufDesc.ByteWidth = (i_b32Bit?4:2) * i_IndexNum;
            l_Hr = DS_CreateBuffer(m_OSRendererData.m_pDevice,  &l_BufDesc, DM_NULL, &m_IndexBufferResPool[i].m_pIndexBuffer );
            DOME_ASSERT( SUCCEEDED( l_Hr ) );

            m_IndexBufferResPool[i].m_bFree = DM_FALSE;
            m_FreeIndexBufferRes --;
            m_IndexBufferResPool[i].m_Usage = i_Usage;
            m_IndexBufferResPool[i].m_b32Bit = i_b32Bit;
            m_IndexBufferResPool[i].m_NumIndex = i_IndexNum;
            o_Result.set(i, this);
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyIndexBuffer(OSIndexBuffer i_IBuffer)
{
    Int l_Index = i_IBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxIndexBufferRes);
    DOME_ASSERT(!m_IndexBufferResPool[l_Index].m_bFree);

    RC_RELEASE(m_IndexBufferResPool[l_Index].m_pIndexBuffer);
    m_IndexBufferResPool[l_Index].m_bFree = DM_TRUE;
    m_FreeIndexBufferRes ++;
    m_IndexBufferResPool[l_Index].m_Usage = RBU_DEFAULT;
    m_IndexBufferResPool[l_Index].m_b32Bit = 0;
    m_IndexBufferResPool[l_Index].m_NumIndex = 0;

    return R_SUCCESS;
}

void*               RCRenderer_DX11::lockIndexBuffer(OSIndexBuffer i_IBuffer, RCBUFFLOCKSTYLE i_LockStyle)
{
    Int l_Index = i_IBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxIndexBufferRes);
    DOME_ASSERT(!m_IndexBufferResPool[l_Index].m_bFree);

    HRESULT l_Hr;
    D3D11_MAPPED_SUBRESOURCE l_mapss;
    D3D11_MAP l_map = D3D11_MAP_READ_WRITE;
    if(i_LockStyle == RTLS_READONLY)
        l_map = D3D11_MAP_READ;
    else if(i_LockStyle == RTLS_WRITEONLY)
        l_map = D3D11_MAP_WRITE_DISCARD;
    else if(i_LockStyle == RTLS_READWRITE)
        l_map = D3D11_MAP_READ_WRITE;

    l_Hr = DS_Map(m_OSRendererData.m_pDeviceContext, m_IndexBufferResPool[l_Index].m_pIndexBuffer, 0, l_map, 0, &l_mapss);
    if (!SUCCEEDED(l_Hr))
    {
        return DM_NULL;
    }

    return l_mapss.pData;
}

DResult             RCRenderer_DX11::unlockIndexBuffer(OSIndexBuffer i_IBuffer)
{
    Int l_Index = i_IBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxIndexBufferRes);
    DOME_ASSERT(!m_IndexBufferResPool[l_Index].m_bFree);

    DS_Unmap(m_OSRendererData.m_pDeviceContext, m_IndexBufferResPool[l_Index].m_pIndexBuffer, 0);
    return R_SUCCESS;
}


// const buffer resource
DResult             RCRenderer_DX11::createConstBuffer(OSConstBuffer& o_Result, Int i_NumFloat4, RCBUFFUSAGE i_Usage)
{
    if(m_FreeConstBufferRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxConstBufferRes; ++i)
    {
        if (m_ConstBufferResPool[i].m_bFree)
        {
            HRESULT l_Hr;
            D3D11_BUFFER_DESC l_BufDesc;
            ZeroMemory( &l_BufDesc, sizeof(l_BufDesc) );
            //TODO: always use dynamic here, but I should support other later
            l_BufDesc.Usage = D3D11_USAGE_DYNAMIC;
            l_BufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            l_BufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            l_BufDesc.ByteWidth = (UINT)(i_NumFloat4 * sizeof(F32) * 4);
            l_Hr = DS_CreateBuffer(m_OSRendererData.m_pDevice,  &l_BufDesc, DM_NULL, &m_ConstBufferResPool[i].m_pConstBuffer );
            DOME_ASSERT( SUCCEEDED( l_Hr ) );

            m_ConstBufferResPool[i].m_bFree = DM_FALSE;
            m_FreeConstBufferRes --;
            m_ConstBufferResPool[i].m_Usage = i_Usage;
            o_Result.set(i, this);
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyConstBuffer(OSConstBuffer i_CBuffer)
{
    Int l_Index = i_CBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxConstBufferRes);
    DOME_ASSERT(!m_ConstBufferResPool[l_Index].m_bFree);

    RC_RELEASE(m_ConstBufferResPool[l_Index].m_pConstBuffer);
    m_ConstBufferResPool[l_Index].m_bFree = DM_TRUE;
    m_FreeConstBufferRes ++;
    m_ConstBufferResPool[l_Index].m_Usage = RBU_DEFAULT;

    return R_SUCCESS;
}

void*               RCRenderer_DX11::lockConstBuffer(OSConstBuffer i_CBuffer, RCBUFFLOCKSTYLE i_LockStyle)
{
    Int l_Index = i_CBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxConstBufferRes);
    DOME_ASSERT(!m_ConstBufferResPool[l_Index].m_bFree);

    HRESULT l_Hr;
    D3D11_MAPPED_SUBRESOURCE l_mapss;
    D3D11_MAP l_map = D3D11_MAP_READ_WRITE;
    if(i_LockStyle == RTLS_READONLY)
        l_map = D3D11_MAP_READ;
    else if(i_LockStyle == RTLS_WRITEONLY)
        l_map = D3D11_MAP_WRITE_DISCARD;
    else if(i_LockStyle == RTLS_READWRITE)
        l_map = D3D11_MAP_READ_WRITE;

    l_Hr = DS_Map(m_OSRendererData.m_pDeviceContext, m_ConstBufferResPool[l_Index].m_pConstBuffer, 0, l_map, 0, &l_mapss);
    if (!SUCCEEDED(l_Hr))
    {
        return DM_NULL;
    }

    return l_mapss.pData;
}

DResult             RCRenderer_DX11::unlockConstBuffer(OSConstBuffer i_CBuffer)
{
    Int l_Index = i_CBuffer.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxConstBufferRes);
    DOME_ASSERT(!m_ConstBufferResPool[l_Index].m_bFree);

    DS_Unmap(m_OSRendererData.m_pDeviceContext, m_ConstBufferResPool[l_Index].m_pConstBuffer, 0);
    return R_SUCCESS;
}

// common render functions
DResult             RCRenderer_DX11::clearRenderTarget(OSTexture2D i_Rt, const DVector4f& i_ClearColor)
{
    if(!i_Rt.isValid())
        return R_FAILED;

    Int l_IdxTex = i_Rt.getHandle();
    DOME_ASSERT(l_IdxTex >= 0 && l_IdxTex < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_IdxTex].m_bFree);
    DOME_ASSERT(m_TextureResPool[l_IdxTex].m_TextureData.m_pRenderTargetView);
            
    m_OSRendererData.m_pDeviceContext->ClearRenderTargetView(m_TextureResPool[l_IdxTex].m_TextureData.m_pRenderTargetView, &i_ClearColor.x);

    return R_SUCCESS;
}

DResult             RCRenderer_DX11::setRenderTargets(Int i_NumRt, const OSTexture2D* i_pRtArray, OSTexture2D i_DepthTex)
{
    const Int k_MaxRt = 16;
    ID3D11RenderTargetView* l_RtArray[k_MaxRt];
    ID3D11DepthStencilView* l_pDepthStencilView = DM_NULL;
    DOME_ASSERT(i_NumRt < k_MaxRt);
    for (Int i = 0; i < i_NumRt; ++i)
    {
        if (i_pRtArray[i].isValid())
        {
            Int l_IdxTex = i_pRtArray[i].getHandle();
            DOME_ASSERT(l_IdxTex >= 0 && l_IdxTex < k_MaxTextureRes);
            DOME_ASSERT(!m_TextureResPool[l_IdxTex].m_bFree);
            DOME_ASSERT(m_TextureResPool[l_IdxTex].m_TextureData.m_pRenderTargetView);
            
            l_RtArray[i] = m_TextureResPool[l_IdxTex].m_TextureData.m_pRenderTargetView;
        }
        else
        {
            l_RtArray[i] = DM_NULL;
        }
    }

    {
        if (i_DepthTex.isValid())
        {
            Int l_IdxTex = i_DepthTex.getHandle();
            DOME_ASSERT(l_IdxTex >= 0 && l_IdxTex < k_MaxTextureRes);
            DOME_ASSERT(!m_TextureResPool[l_IdxTex].m_bFree);
            DOME_ASSERT(m_TextureResPool[l_IdxTex].m_TextureData.m_pDepthStencilView);

            l_pDepthStencilView = m_TextureResPool[l_IdxTex].m_TextureData.m_pDepthStencilView;
        }
    }

    m_OSRendererData.m_pDeviceContext->OMSetRenderTargets(i_NumRt, l_RtArray, l_pDepthStencilView);

    return R_SUCCESS;
}

DResult             RCRenderer_DX11::setViewports(Int i_NumViewport, const RCViewportInfo* i_ViewportArray)
{
    m_OSRendererData.m_pDeviceContext->RSSetViewports(i_NumViewport, (const D3D11_VIEWPORT*)i_ViewportArray);
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::modifyBlendMode(RCBLENDMODE i_Mode)
{
    m_CurBlendMode = i_Mode;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::modifyDepthState(Bool i_bDepthTest, Bool i_bDepthWrite, RCDEPTHTESTFUNC i_DepthFunc)
{
    m_bCurDepthTest = i_bDepthTest;
    m_bCurDepthWrite = i_bDepthWrite;
    m_CurDepthFunc = i_DepthFunc;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::modifyCullMode(RCCULLMODE i_CullMode, Bool i_bCCWFront)
{
    m_CurCullMode = i_CullMode;
    m_bCurCCWFront = i_bCCWFront;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::modifyPipelineStateToDefault()
{
    m_CurBlendMode      = RBM_COPY;
    m_bCurDepthTest     = DM_FALSE;
    m_bCurDepthWrite    = DM_FALSE;
    m_CurDepthFunc      = RC_DEPTHTEST_LESS;
    m_CurCullMode       = RCM_CULL_NONE;
    m_bCurCCWFront      = DM_FALSE;

    return R_SUCCESS;
}

DResult             RCRenderer_DX11::commitPipelineState(Bool i_bForce)
{
    // BLEND MODE
    switch (m_CurBlendMode)
    {
    case RBM_COPY:
        {
            FLOAT l_BlendFactors[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            m_OSRendererData.m_pDeviceContext->OMSetBlendState(m_DX11BlendState[RBM_COPY], l_BlendFactors, 0xFFffFFff);
        }
        break;

    case RBM_ALPHABLEND:
        {
            FLOAT l_BlendFactors[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            m_OSRendererData.m_pDeviceContext->OMSetBlendState(m_DX11BlendState[RBM_ALPHABLEND], l_BlendFactors, 0xFFffFFff);
        }
        break;

    case RBM_ADD:
        {
            FLOAT l_BlendFactors[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            m_OSRendererData.m_pDeviceContext->OMSetBlendState(m_DX11BlendState[RBM_ADD], l_BlendFactors, 0xFFffFFff);
        }
        break;

    default:
        {
            DOME_ASSERT(0);
        }
        break;
    }

    // DEPTH STENCIL STATE

    // RASTERIZER STATE
    ID3D11RasterizerState* l_pRasterizerState = DM_NULL;
    for (Int i = 0; i < m_DX11RasterizerStateArray.size(); ++i)
    {
        if (m_CurCullMode == m_DX11RasterizerStateArray[i].m_CullMode &&
            m_bCurCCWFront == m_DX11RasterizerStateArray[i].m_bCCWFront)
        {
            l_pRasterizerState = m_DX11RasterizerStateArray[i].m_pDX11RasterizerState;
            break;
        }
    }
    if (!l_pRasterizerState)
    {
        HRESULT hr;
        D3D11_RASTERIZER_DESC l_Desc;
        l_Desc.FillMode = D3D11_FILL_SOLID;
        l_Desc.CullMode = (D3D11_CULL_MODE)(m_CurCullMode + 1);
        l_Desc.FrontCounterClockwise = m_bCurCCWFront;
        l_Desc.DepthBias = 0;
        l_Desc.SlopeScaledDepthBias = 0.0f;
        l_Desc.DepthBiasClamp = 0.0f;
        l_Desc.DepthClipEnable = DM_TRUE;
        l_Desc.ScissorEnable = DM_FALSE;
        l_Desc.MultisampleEnable = DM_FALSE;
        l_Desc.AntialiasedLineEnable = DM_FALSE;

        hr = m_OSRendererData.m_pDevice->CreateRasterizerState(&l_Desc, &l_pRasterizerState);
        DOME_ASSERT(S_OK == hr);

        _DX11RasterizerStateInfo l_RSInfo;
        l_RSInfo.m_CullMode = m_CurCullMode;
        l_RSInfo.m_bCCWFront = m_bCurCCWFront;
        l_RSInfo.m_pDX11RasterizerState = l_pRasterizerState;
        m_DX11RasterizerStateArray.push_back(l_RSInfo);
    }
    m_OSRendererData.m_pDeviceContext->RSSetState(l_pRasterizerState);

    return R_SUCCESS;
}

// render operation resource
DResult             RCRenderer_DX11::createRenderOperation(OSRenderOperation& o_Result)
{
    HRESULT hrRetCode = S_OK;
    if(m_FreeRenderOperationRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxRenderOperationRes; ++i)
    {
        if (m_RenderOperationResPool[i].m_bFree)
        {
            for (Int n = 0; n < k_MaxRenderTarget; ++n)
            {
                m_RenderOperationResPool[i].m_RenderTargets[n].set(-1, DM_NULL);
            }
            m_RenderOperationResPool[i].m_DepthTexture.set(-1, DM_NULL);
            m_RenderOperationResPool[i].m_VertexShader.set(-1, DM_NULL);
            m_RenderOperationResPool[i].m_PixelShader.set(-1, DM_NULL);
            m_RenderOperationResPool[i].m_VertexBuffer.set(-1, DM_NULL);
            m_RenderOperationResPool[i].m_IndexBuffer.set(-1, DM_NULL);
            for (Int n = 0; n < k_MaxConstBuffer; ++n)
            {
                m_RenderOperationResPool[i].m_VSConstBuffer[n].set(-1, DM_NULL);
                m_RenderOperationResPool[i].m_PSConstBuffer[n].set(-1, DM_NULL);
            }
            for (Int n = 0; n < k_MaxTexture; ++n)
            {
                m_RenderOperationResPool[i].m_VSTextures[n].set(-1, DM_NULL);
                m_RenderOperationResPool[i].m_PSTextures[n].set(-1, DM_NULL);
            }
            m_RenderOperationResPool[i].m_bDepthTest = DM_FALSE;
            m_RenderOperationResPool[i].m_bDepthWrite = DM_FALSE;
            m_RenderOperationResPool[i].m_D3D11Viewport.TopLeftX = 0.0f;
            m_RenderOperationResPool[i].m_D3D11Viewport.TopLeftY = 0.0f;
            m_RenderOperationResPool[i].m_D3D11Viewport.Width = 1.0f;
            m_RenderOperationResPool[i].m_D3D11Viewport.Height = 1.0f;
            m_RenderOperationResPool[i].m_D3D11Viewport.MinDepth = 0.0f;
            m_RenderOperationResPool[i].m_D3D11Viewport.MaxDepth = 1.0f;


            m_RenderOperationResPool[i].m_bFree = DM_FALSE;
            m_FreeRenderOperationRes --;

            D3D11_DEPTH_STENCIL_DESC l_DS_Desc;
            l_DS_Desc.DepthEnable = DM_FALSE;
            l_DS_Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            l_DS_Desc.StencilEnable = DM_FALSE;
            hrRetCode = m_OSRendererData.m_pDevice->CreateDepthStencilState(&l_DS_Desc, &m_RenderOperationResPool[i].m_pDepthStencilState);
            if(FAILED(hrRetCode))
            {
                return R_FAILED;
            }

            o_Result.set(i, this);
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyRenderOperation(OSRenderOperation i_RO)
{
    Int l_Index = i_RO.getHandle();
    DOME_ASSERT(l_Index >= 0 && l_Index < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_Index].m_bFree);

    for (Int n = 0; n < k_MaxRenderTarget; ++n)
    {
        m_RenderOperationResPool[l_Index].m_RenderTargets[n].set(-1, DM_NULL);
    }
    m_RenderOperationResPool[l_Index].m_DepthTexture.set(-1, DM_NULL);
    m_RenderOperationResPool[l_Index].m_VertexShader.set(-1, DM_NULL);
    m_RenderOperationResPool[l_Index].m_PixelShader.set(-1, DM_NULL);
    m_RenderOperationResPool[l_Index].m_VertexBuffer.set(-1, DM_NULL);
    m_RenderOperationResPool[l_Index].m_IndexBuffer.set(-1, DM_NULL);
    for (Int n = 0; n < k_MaxConstBuffer; ++n)
    {
        m_RenderOperationResPool[l_Index].m_VSConstBuffer[n].set(-1, DM_NULL);
        m_RenderOperationResPool[l_Index].m_PSConstBuffer[n].set(-1, DM_NULL);
    }
    for (Int n = 0; n < k_MaxTexture; ++n)
    {
        m_RenderOperationResPool[l_Index].m_VSTextures[n].set(-1, DM_NULL);
        m_RenderOperationResPool[l_Index].m_PSTextures[n].set(-1, DM_NULL);
    }
    m_RenderOperationResPool[l_Index].m_bDepthTest = DM_FALSE;
    m_RenderOperationResPool[l_Index].m_bDepthWrite = DM_FALSE;
    m_RenderOperationResPool[l_Index].m_D3D11Viewport.TopLeftX = 0.0f;
    m_RenderOperationResPool[l_Index].m_D3D11Viewport.TopLeftY = 0.0f;
    m_RenderOperationResPool[l_Index].m_D3D11Viewport.Width = 1.0f;
    m_RenderOperationResPool[l_Index].m_D3D11Viewport.Height = 1.0f;
    m_RenderOperationResPool[l_Index].m_D3D11Viewport.MinDepth = 0.0f;
    m_RenderOperationResPool[l_Index].m_D3D11Viewport.MaxDepth = 1.0f;

    m_RenderOperationResPool[l_Index].m_pDepthStencilState->Release();
    m_RenderOperationResPool[l_Index].m_pDepthStencilState = DM_NULL;

    m_RenderOperationResPool[l_Index].m_bFree = DM_TRUE;
    m_FreeRenderOperationRes ++;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetRenderTarget(OSRenderOperation i_RO, Int i_Index, OSTexture2D i_RT)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxRenderTarget);

    m_RenderOperationResPool[l_ROIdx].m_RenderTargets[i_Index] = i_RT;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetRenderTarget(OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_RT)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxRenderTarget);

    o_RT = m_RenderOperationResPool[l_ROIdx].m_RenderTargets[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetDepthBuffer(OSRenderOperation i_RO, OSTexture2D i_Depth)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_DepthTexture = i_Depth;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetDepthBuffer(OSRenderOperation i_RO, OSTexture2D& o_Depth)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    o_Depth = m_RenderOperationResPool[l_ROIdx].m_DepthTexture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetVertexShader(OSRenderOperation i_RO, OSVertexShader i_VertexShader)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_VertexShader = i_VertexShader;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetVertexShader(OSRenderOperation i_RO, OSVertexShader& o_VertexShader)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    o_VertexShader = m_RenderOperationResPool[l_ROIdx].m_VertexShader;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetPixelShader(OSRenderOperation i_RO, OSPixelShader i_PixelShader)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_PixelShader = i_PixelShader;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetPixelShader(OSRenderOperation i_RO, OSPixelShader& o_PixelShader)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    o_PixelShader = m_RenderOperationResPool[l_ROIdx].m_PixelShader;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetVertexBuffer(OSRenderOperation i_RO, OSVertexBuffer i_VBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_VertexBuffer = i_VBuffer;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetVertexBuffer(OSRenderOperation i_RO, OSVertexBuffer& o_VBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    o_VBuffer = m_RenderOperationResPool[l_ROIdx].m_VertexBuffer;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetIndexBuffer(OSRenderOperation i_RO, OSIndexBuffer i_IBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_IndexBuffer = i_IBuffer;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetIndexBuffer(OSRenderOperation i_RO, OSIndexBuffer& o_IBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    o_IBuffer = m_RenderOperationResPool[l_ROIdx].m_IndexBuffer;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetVS_ConstBuffer(OSRenderOperation i_RO, Int i_Index, OSConstBuffer i_CBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxConstBuffer);

    m_RenderOperationResPool[l_ROIdx].m_VSConstBuffer[i_Index] = i_CBuffer;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetVS_ConstBuffer(OSRenderOperation i_RO, Int i_Index, OSConstBuffer& o_CBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxConstBuffer);

    o_CBuffer = m_RenderOperationResPool[l_ROIdx].m_VSConstBuffer[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetVS_Texture(OSRenderOperation i_RO, Int i_Index, OSTexture2D i_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture);

    m_RenderOperationResPool[l_ROIdx].m_VSTextures[i_Index] = i_Texture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetVS_Texture(OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture);

    o_Texture = m_RenderOperationResPool[l_ROIdx].m_VSTextures[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetVS_Texture3D(OSRenderOperation i_RO, Int i_Index, OSTexture3D i_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture3D);

    m_RenderOperationResPool[l_ROIdx].m_VSTextures3D[i_Index] = i_Texture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetVS_Texture3D(OSRenderOperation i_RO, Int i_Index, OSTexture3D& o_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture3D);

    o_Texture = m_RenderOperationResPool[l_ROIdx].m_VSTextures3D[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetVS_TextureCube(OSRenderOperation i_RO, Int i_Index, OSTextureCube i_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTextureCube);

    m_RenderOperationResPool[l_ROIdx].m_VSTexturesCube[i_Index] = i_Texture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetVS_TextureCube(OSRenderOperation i_RO, Int i_Index, OSTextureCube& o_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTextureCube);

    o_Texture = m_RenderOperationResPool[l_ROIdx].m_VSTexturesCube[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetPS_ConstBuffer(OSRenderOperation i_RO, Int i_Index, OSConstBuffer i_CBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxConstBuffer);

    m_RenderOperationResPool[l_ROIdx].m_PSConstBuffer[i_Index] = i_CBuffer;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetPS_ConstBuffer(OSRenderOperation i_RO, Int i_Index, OSConstBuffer& o_CBuffer)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxConstBuffer);

    o_CBuffer = m_RenderOperationResPool[l_ROIdx].m_PSConstBuffer[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetPS_Texture(OSRenderOperation i_RO, Int i_Index, OSTexture2D i_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture);

    m_RenderOperationResPool[l_ROIdx].m_PSTextures[i_Index] = i_Texture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetPS_Texture(OSRenderOperation i_RO, Int i_Index, OSTexture2D& o_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture);

    o_Texture = m_RenderOperationResPool[l_ROIdx].m_PSTextures[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetPS_Texture3D(OSRenderOperation i_RO, Int i_Index, OSTexture3D i_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture3D);

    m_RenderOperationResPool[l_ROIdx].m_PSTextures3D[i_Index] = i_Texture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetPS_Texture3D(OSRenderOperation i_RO, Int i_Index, OSTexture3D& o_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTexture);

    o_Texture = m_RenderOperationResPool[l_ROIdx].m_PSTextures3D[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetPS_TextureCube(OSRenderOperation i_RO, Int i_Index, OSTextureCube i_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTextureCube);

    m_RenderOperationResPool[l_ROIdx].m_PSTexturesCube[i_Index] = i_Texture;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_GetPS_TextureCube(OSRenderOperation i_RO, Int i_Index, OSTextureCube& o_Texture)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    DOME_ASSERT(i_Index >= 0 && i_Index < k_MaxTextureCube);

    o_Texture = m_RenderOperationResPool[l_ROIdx].m_PSTexturesCube[i_Index];
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetRS_EnableDepthTest(OSRenderOperation i_RO, Bool i_bEnable)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_bDepthTest = i_bEnable;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetRS_EnableDepthWrite(OSRenderOperation i_RO, Bool i_bEnable)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_bDepthWrite = i_bEnable;
    return R_SUCCESS;
}

DResult				RCRenderer_DX11::ro_SetRS_BlendMode(OSRenderOperation i_RO, RCBLENDMODE i_Mode)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_BlendMode = i_Mode;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetRS_CullMode(OSRenderOperation i_RO, RCCULLMODE i_Mode, Bool i_bCCWFront)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_CullMode = i_Mode;
    m_RenderOperationResPool[l_ROIdx].m_bCCWFront = i_bCCWFront;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_SetViewport(OSRenderOperation i_RO, F32 x, F32 y, F32 width, F32 height, F32 i_MinDepth, F32 i_MaxDepth)
{
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);

    m_RenderOperationResPool[l_ROIdx].m_D3D11Viewport.TopLeftX = x;
    m_RenderOperationResPool[l_ROIdx].m_D3D11Viewport.TopLeftY = y;
    m_RenderOperationResPool[l_ROIdx].m_D3D11Viewport.Width = width;
    m_RenderOperationResPool[l_ROIdx].m_D3D11Viewport.Height = height;
    m_RenderOperationResPool[l_ROIdx].m_D3D11Viewport.MinDepth = i_MinDepth;
    m_RenderOperationResPool[l_ROIdx].m_D3D11Viewport.MaxDepth = i_MaxDepth;

    return R_SUCCESS;
}

DResult             RCRenderer_DX11::ro_Execute(OSRenderOperation i_RO)
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_ROEXECUTE, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    Int l_ROIdx = i_RO.getHandle();
    DOME_ASSERT(l_ROIdx >= 0 && l_ROIdx < k_MaxRenderOperationRes);
    DOME_ASSERT(!m_RenderOperationResPool[l_ROIdx].m_bFree);
    _RenderOperationRes* l_pRo = m_RenderOperationResPool + l_ROIdx;
    DResult l_Result = R_SUCCESS;
    Bool l_bUseIndex = DM_FALSE;
    Int  l_NumVertex = 0;
    Int  l_NumIndex = 0;

    // set render targets and depth
    ID3D11RenderTargetView* l_RenderTargets[k_MaxRenderTarget];
    ID3D11DepthStencilView* l_pDepth = DM_NULL;
    Int l_NumRt = 0;
    for(Int i = 0; i < k_MaxRenderTarget; ++i)
        l_RenderTargets[i] = DM_NULL;
    for (Int i = 0; i < k_MaxRenderTarget; ++i)
    {
        if (l_pRo->m_RenderTargets[i].isValid())
        {
            l_RenderTargets[i] = m_TextureResPool[l_pRo->m_RenderTargets[i].getHandle()].m_TextureData.m_pRenderTargetView;
            l_NumRt = i + 1;
        }
        else
        {
            break;
        }
    }

    m_OSRendererData.m_pDeviceContext->OMSetRenderTargets(l_NumRt, l_RenderTargets, l_pDepth);


    // set vs const buffer
    for (Int i = 0; i < k_MaxConstBuffer; ++i)
    {
        if (l_pRo->m_VSConstBuffer[i].isValid())
        {
            m_OSRendererData.m_pDeviceContext->VSSetConstantBuffers(i, 1, &m_ConstBufferResPool[l_pRo->m_VSConstBuffer[i].getHandle()].m_pConstBuffer);
        }
        else
            break;
    }

    // set vs textures
	Int l_TexRegIndex = 0;
    for (Int i = 0; i < k_MaxTexture; ++i)
    {
        if (l_pRo->m_VSTextures[i].isValid())
        {
            m_OSRendererData.m_pDeviceContext->VSSetShaderResources(l_TexRegIndex++, 1, &m_TextureResPool[l_pRo->m_VSTextures[i].getHandle()].m_TextureData.m_pShaderResourceView);
        }
        else
            break;
    }

	// set vs 3D textures
	for (Int i = 0; i < k_MaxTexture3D; ++i)
	{
		if (l_pRo->m_VSTextures3D[i].isValid())
		{
			m_OSRendererData.m_pDeviceContext->VSSetShaderResources(l_TexRegIndex++, 1, &m_Texture3DResPool[l_pRo->m_VSTextures3D[i].getHandle()].m_TextureData.m_pShaderResourceView);
		}
		else
			break;
	}

    // set vs Cube textures
    for (Int i = 0; i < k_MaxTextureCube; ++i)
    {
        if (l_pRo->m_VSTexturesCube[i].isValid())
        {
            m_OSRendererData.m_pDeviceContext->VSSetShaderResources(l_TexRegIndex++, 1, &m_TextureCubeResPool[l_pRo->m_VSTexturesCube[i].getHandle()].m_TextureData.m_pShaderResourceView);
        }
        else
            break;
    }

    // set vs shader and input layout
    if (l_pRo->m_VertexShader.isValid())
    {
        m_OSRendererData.m_pDeviceContext->VSSetShader(m_VertexShaderResPool[l_pRo->m_VertexShader.getHandle()].m_pVertexShader, NULL, 0);
        m_OSRendererData.m_pDeviceContext->IASetInputLayout(m_VertexShaderResPool[l_pRo->m_VertexShader.getHandle()].m_pVertexInputLayout);
    }
    else
        l_Result = R_FAILED;

    // set ps const buffer
    for (Int i = 0; i < k_MaxConstBuffer; ++i)
    {
        if (l_pRo->m_PSConstBuffer[i].isValid())
        {
            m_OSRendererData.m_pDeviceContext->PSSetConstantBuffers(i, 1, &m_ConstBufferResPool[l_pRo->m_PSConstBuffer[i].getHandle()].m_pConstBuffer);
        }
        else
            break;
    }

    // set ps textures
	l_TexRegIndex = 0;
    for (Int i = 0; i < k_MaxTexture; ++i)
    {
        if (l_pRo->m_PSTextures[i].isValid())
        {
            ID3D11ShaderResourceView* l_pSRV = m_TextureResPool[l_pRo->m_PSTextures[i].getHandle()].m_TextureData.m_pShaderResourceView;
            m_OSRendererData.m_pDeviceContext->PSSetShaderResources(l_TexRegIndex++, 1, &l_pSRV);
        }
        else
            break;
    }

	// set ps 3D textures
	for (Int i = 0; i < k_MaxTexture3D; ++i)
	{
		if (l_pRo->m_PSTextures3D[i].isValid())
		{
			m_OSRendererData.m_pDeviceContext->PSSetShaderResources(l_TexRegIndex++, 1, &m_Texture3DResPool[l_pRo->m_PSTextures3D[i].getHandle()].m_TextureData.m_pShaderResourceView);
		}
		else
			break;
	}

    // set ps 3D textures
    for (Int i = 0; i < k_MaxTextureCube; ++i)
    {
        if (l_pRo->m_PSTexturesCube[i].isValid())
        {
            m_OSRendererData.m_pDeviceContext->PSSetShaderResources(l_TexRegIndex++, 1, &m_TextureCubeResPool[l_pRo->m_PSTexturesCube[i].getHandle()].m_TextureData.m_pShaderResourceView);
        }
        else
            break;
    }

    // set ps shader
    if (l_pRo->m_PixelShader.isValid())
    {
        m_OSRendererData.m_pDeviceContext->PSSetShader(m_PixelShaderResPool[l_pRo->m_PixelShader.getHandle()].m_pPixelShader, NULL, 0);
    }
    else
        l_Result = R_FAILED;

    // set viewport
    m_OSRendererData.m_pDeviceContext->RSSetViewports(1, &l_pRo->m_D3D11Viewport);
    m_OSRendererData.m_pDeviceContext->OMSetDepthStencilState(l_pRo->m_pDepthStencilState, 0);

    // set vertex buffer
    if (l_pRo->m_VertexBuffer.isValid())
    {
        UINT stride = m_VertexBufferResPool[l_pRo->m_VertexBuffer.getHandle()].m_VertexSize;
        UINT offset = 0;
        m_OSRendererData.m_pDeviceContext->IASetVertexBuffers(0, 1, &m_VertexBufferResPool[l_pRo->m_VertexBuffer.getHandle()].m_pVertexBuffer, &stride, &offset);
        l_NumVertex = m_VertexBufferResPool[l_pRo->m_VertexBuffer.getHandle()].m_NumVertex;
    }
    else
        l_Result = R_FAILED;

    // set index buffer
    if (l_pRo->m_IndexBuffer.isValid())
    {
        Bool l_b32Bit = m_IndexBufferResPool[l_pRo->m_IndexBuffer.getHandle()].m_b32Bit;

        m_OSRendererData.m_pDeviceContext->IASetIndexBuffer(m_IndexBufferResPool[l_pRo->m_IndexBuffer.getHandle()].m_pIndexBuffer, l_b32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
        l_NumIndex = m_IndexBufferResPool[l_pRo->m_IndexBuffer.getHandle()].m_NumIndex;
        l_bUseIndex = DM_TRUE;
    }

    // set primitive topology
    m_OSRendererData.m_pDeviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    // blend mode
    if (l_pRo->m_BlendMode != RBM_UNKNOWN)
    {
        modifyBlendMode(l_pRo->m_BlendMode);
    }

    // cull mode
    modifyCullMode(l_pRo->m_CullMode, l_pRo->m_bCCWFront);

    // commit all pipeline state
    commitPipelineState(DM_TRUE);

    // draw
    if (l_bUseIndex)
    {
        KG_DrawIndexed(m_OSRendererData.m_pDeviceContext, l_NumIndex, 0, 0);
    }
    else
    {
        KG_Draw(m_OSRendererData.m_pDeviceContext, l_NumVertex, 0);
    }

	// set ps textures
	for (Int i = 0; i < k_MaxTexture; ++i)
	{
		ID3D11ShaderResourceView* pSRV = NULL;
		if (l_pRo->m_PSTextures[i].isValid())
		{
			m_OSRendererData.m_pDeviceContext->PSSetShaderResources(i, 1, &pSRV);
		}
		else
			break;
	}

   /* {
        ID3D11DepthStencilView* l_pDepth = DM_NULL;
        for(Int i = 0; i < k_MaxRenderTarget; ++i)
            l_RenderTargets[i] = DM_NULL;
        m_OSRendererData.m_pDeviceContext->OMSetRenderTargets(l_NumRt, l_RenderTargets, l_pDepth);
    }*/
    

	FRAMETIMER_END(FTT_RC_CAL_ROEXECUTE);
    return R_SUCCESS;
}




RC_NAMESPACE_END