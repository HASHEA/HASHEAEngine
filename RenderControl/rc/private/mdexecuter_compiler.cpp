/*
    filename:       mdexecuter_compiler.cpp
    author:         Ming Dong
    date:           2016-JUN-15
    description:    
*/

#include "mdexecuter.h"
#include "../public/rceffectmanager.h"
#include "../public/mdoperatorgpu.h"
#include "../public/rcmanager.h"
#include "../public/rcglobal.h"

RC_NAMESPACE_BEGIN

DResult MDExecuter::init()
{
    RCRenderer* l_pRenderer = m_pEffectManager->getRenderer();
    DOME_ASSERT(l_pRenderer);
    DResult l_Result;

    // create vertex shader const buffer
    l_Result = l_pRenderer->createConstBuffer(m_VSParams, 1, RBU_DYNAMIC);
    DOME_ASSERT(DM_SUCC(l_Result));

    DString l_ErrorInfo;
    DString l_Signature = m_Signature.c_str();
    Int l_NumTex, l_NumTex3D, l_NumTexCube, l_NumMat4x4, l_NumMat3x3, l_NumMat2x2, l_NumFloat4, l_NumFloat3, l_NumFloat2, l_NumFloat;
    l_Result = compile(l_Signature, m_PSShaderCode, l_NumTex, l_NumTex3D, l_NumTexCube, l_NumMat4x4, l_NumMat3x3, l_NumMat2x2, l_NumFloat4, l_NumFloat3, l_NumFloat2, l_NumFloat);
    DOME_ASSERT(DM_SUCC(l_Result));

    m_TextureParams.resize(l_NumTex);
    m_Texture3DParams.resize(l_NumTex3D);
    m_TextureCubeParams.resize(l_NumTexCube);
    m_Matrix4x4Params.resize(l_NumMat4x4);
    m_Matrix3x3Params.resize(l_NumMat3x3);
    m_Matrix2x2Params.resize(l_NumMat2x2);
    m_Float4Params.resize(l_NumFloat4);
    m_Float3Params.resize(l_NumFloat3);
    m_Float2Params.resize(l_NumFloat2);
    m_Float1Params.resize(l_NumFloat);

    DString l_DataPath;
    l_pRenderer->getDataPath(l_DataPath);
    l_DataPath += "shadergen\\";
    l_DataPath += l_Signature;
    l_DataPath += ".ps";
    l_Result = l_pRenderer->createPixelShader(m_PixelShader, l_Signature, m_PSShaderCode, "RCPSMain", l_DataPath, l_ErrorInfo);
    DOME_ASSERT(DM_SUCC(l_Result));

    Int l_PSParamSize = l_NumMat4x4 * 4 + l_NumMat3x3 * 3 + l_NumMat2x2 * 2 + l_NumFloat4 + l_NumFloat3 + (l_NumFloat2 + 1) / 2 + (l_NumFloat + 3) / 4;
    if (l_PSParamSize > 0)
    {
        l_Result = l_pRenderer->createConstBuffer(m_PSParams, l_PSParamSize, RBU_DYNAMIC);
        DOME_ASSERT(DM_SUCC(l_Result));
    }

    l_Result = l_pRenderer->createRenderOperation(m_RO);
    DOME_ASSERT(DM_SUCC(l_Result));

    return R_SUCCESS;
}

DResult MDExecuter::uninit()
{
    RCRenderer* l_pRenderer = m_pEffectManager->getRenderer();
    DOME_ASSERT(l_pRenderer);
//    DResult l_Result;

    l_pRenderer->destroyConstBuffer(m_VSParams);
    if(m_PSParams.isValid())
        l_pRenderer->destroyConstBuffer(m_PSParams);
    l_pRenderer->destroyPixelShader(m_PixelShader);
    l_pRenderer->destroyRenderOperation(m_RO);

    return R_SUCCESS;
}

/*
    g_Tex_[n]
    g_Mat44_[N]
    g_Mat33_[N]
    g_Mat22_[N]
    g_F4_[N]
    g_F3_[N]
    g_F2_[N]
    g_F1_[N]

    l_TexVal_[N]
    l_Operation_[N]
*/
struct _MDECompileContext
{
    DString                 m_ShaderHead;
    TArray<DHashString>     m_Operators;
    DString                 m_ShaderGlobal;
    DString                 m_ShaderParamsBegin;
    DString                 m_ShaderParamsEnd;
    DString                 m_ShaderParamsMat44;
    DString                 m_ShaderParamsMat33;
    DString                 m_ShaderParamsMat22;
    DString                 m_ShaderParamsFloat4;
    DString                 m_ShaderParamsFloat3;
    DString                 m_ShaderParamsFloat2;
    DString                 m_ShaderParamsFloat1;
    DString                 m_ShaderParamsTex;
    DString                 m_ShaderParamsTex3D;
    DString                 m_ShaderParamsTexCube;
    DString                 m_ShaderMainBegin;
    DString                 m_ShaderMainPreSample;
    DString                 m_ShaderMainEnd;
    DString                 m_ShaderMainCode;
    Int                     m_NumOperation;
    Int                     m_NumTex;
    Int                     m_NumTex3D;
    Int                     m_NumTexCube;
    Int                     m_NumMat4x4;
    Int                     m_NumMat3x3;
    Int                     m_NumMat2x2;
    Int                     m_NumFloat4;
    Int                     m_NumFloat3;
    Int                     m_NumFloat2;
    Int                     m_NumFloat1;
};

Bool MDIsAlphaBet(Char i_Char)
{
    if(i_Char >= 'a' && i_Char <= 'z')
        return DM_TRUE;
    if(i_Char >= 'A' && i_Char <= 'Z')
        return DM_TRUE;
    if(i_Char == '_')
        return DM_TRUE;
    return DM_FALSE;
}

Bool MDIsDigital(Char i_Char)
{
    if(i_Char >= '0' && i_Char <= '9')
        return DM_TRUE;
    return DM_FALSE;
}

Int MDReadOperatorName(const Char* i_pString, DHashString& o_OperatorName)
{
    Char l_Buff[256];
    Int l_NumCopied = 0;
    if(i_pString[0] != 'M' || i_pString[1] != 'D')
        return -1;

    while (DM_TRUE)
    {
        if(MDIsAlphaBet(*i_pString) || MDIsDigital(*i_pString))
            l_Buff[l_NumCopied ++] = *i_pString++;
        else
            break;
    }
    l_Buff[l_NumCopied] = 0;
    o_OperatorName = l_Buff;
    return l_NumCopied;
}

Int MDReadParameterType(const Char* i_pString, DHashString& o_ParameterType)
{
    Char l_Buff[256];
    Int l_NumCopied = 0;
    if(*i_pString == '$')
        l_Buff[l_NumCopied++] = *i_pString++;
    else
        return -1;

    while (DM_TRUE)
    {
        if(MDIsAlphaBet(*i_pString) || MDIsDigital(*i_pString))
            l_Buff[l_NumCopied ++] = *i_pString++;
        else
            break;
    }
    l_Buff[l_NumCopied] = 0;
    o_ParameterType = l_Buff;
    return l_NumCopied;
}

Int MDReadParameterIndex(const Char* i_pString, Int& o_ParameterIndex)
{
    Char l_Buff[256];
    Int l_NumCopied = 0;
    if(*i_pString == '[')
        i_pString ++;
    else
        return -1;

    while (DM_TRUE)
    {
        if(MDIsDigital(*i_pString))
            l_Buff[l_NumCopied++] = *i_pString++;
        else
            break;
    }

    l_Buff[l_NumCopied] = 0;
    if(*i_pString == ']')
        i_pString ++;
    else
        return -1;

    o_ParameterIndex = atol(l_Buff);

    return l_NumCopied + 2;
}

Int MDParseOperation(const Char* i_pString, _MDECompileContext& io_Context)
{
    const Char* l_pOldStart = i_pString;

    if (i_pString == nullptr)
        return -1;

    // the signature should begin with a operator
    if(i_pString[0] != 'M' || i_pString[1] != 'D')
        return -1;

    Int                     l_NChar;
    Int                     l_BuffUsed;
    Char                    l_Buff[2048];
    DHashString             l_OperatorName;
    DString                 l_OperationCode;
    DString                 l_OperationParams = " ";
    const MDOperator*       l_pOperator = DM_NULL;
    const MDOperatorGpu*    l_pGpuOperator = DM_NULL;

    l_NChar = MDReadOperatorName(i_pString, l_OperatorName);
    if(l_NChar < 0) return -1;
    i_pString += l_NChar;

    l_pOperator = RCManager::Instance().getMDOperator(l_OperatorName.getHash());
    if(!l_pOperator) return -1;
    if(!l_pOperator->isGpuOperator()) return -1;
    l_pGpuOperator = (const MDOperatorGpu*)l_pOperator;

    // if the operator is not used yet, add its global code
    Bool l_bUsed = DM_FALSE;
    for (Int i = 0; i < io_Context.m_Operators.size(); ++i)
    {
        if (l_OperatorName == io_Context.m_Operators[i])
        {
            l_bUsed = DM_TRUE;
            break;
        }
    }
    if (!l_bUsed)
    {
        io_Context.m_ShaderGlobal += l_pGpuOperator->getGlobalShader();
        io_Context.m_Operators.push_back(l_OperatorName);
    }

    if(i_pString[0] != '(')
        return -1;
    i_pString ++;

    // read all parameters
    Int l_InputIndex = 0;
    DSimpleTypeID l_InputTypeID;
    while (DM_TRUE)
    {
        if (l_InputIndex == l_pGpuOperator->getInputCount())
        {
            if(i_pString[0] != ')')
                return -1;
            i_pString ++;
            break;
        }
        else if (l_InputIndex > 0)
        {
            if(i_pString[0] != ',')
                return -1;
            i_pString++;
        }

         l_InputTypeID = l_pGpuOperator->getInputTypeID(l_InputIndex);

        if (i_pString[0] == 'M' && i_pString[1] == 'D')
        {
            // this is a operation
            l_NChar = MDParseOperation(i_pString, io_Context);
            if(l_NChar < 0) return -1;
            i_pString += l_NChar;

            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", l_Operation_%lld", io_Context.m_NumOperation - 1);
            DOME_ASSERT(l_BuffUsed > 0);
            l_OperationParams += l_Buff;
        }
        else if(i_pString[0] == '$')
        {
            DHashString l_ParamType;
            Int l_SubIdx;

            l_NChar = MDReadParameterType(i_pString, l_ParamType);
            if(l_NChar < 0) return -1;
            i_pString += l_NChar;

            l_NChar = MDReadParameterIndex(i_pString, l_SubIdx);
            if(l_NChar < 0) return -1;
            i_pString += l_NChar;

            if (l_ParamType == "$TEX")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_OSTexture2D)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumTex)
                    io_Context.m_NumTex = l_SubIdx + 1;

//                sprintf(l_Buff, "Texture2D g_Tex_%lld : register( t%lld );\n", l_SubIdx, l_SubIdx);
//                io_Context.m_ShaderParamsTex += l_Buff;

//                sprintf(l_Buff, "float4 l_TexVal_%lld = RCSampleTexture2D(g_Tex_%lld, RCLinearSampler, i_UV);\n", l_SubIdx, l_SubIdx);
//                io_Context.m_ShaderMainCode += l_Buff;

                if (l_pGpuOperator->canMergeInput(l_InputIndex))
                {
                    l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", l_TexVal_%lld", l_SubIdx);
                    DOME_ASSERT(l_BuffUsed > 0);
                }
                else
                {
                    l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_Tex_%lld", l_SubIdx);
                    DOME_ASSERT(l_BuffUsed > 0);
                }
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$T3D")
            {
                if (l_InputTypeID != RCGlobal::k_SimpleTypeID_OSTexture3D)
                    return -1;

                if (l_SubIdx >= io_Context.m_NumTex3D)
                    io_Context.m_NumTex3D = l_SubIdx + 1;

//                sprintf(l_Buff, "float4x4        g_Mat44_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsMat44 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_T3D_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$TCB")
            {
                if (l_InputTypeID != RCGlobal::k_SimpleTypeID_OSTextureCube)
                    return -1;

                if (l_SubIdx >= io_Context.m_NumTexCube)
                    io_Context.m_NumTexCube = l_SubIdx + 1;

                //                sprintf(l_Buff, "float4x4        g_Mat44_%lld;\n", l_SubIdx);
                //                io_Context.m_ShaderParamsMat44 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_TCB_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$M44")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_DMatrix4x4f)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumMat4x4)
                    io_Context.m_NumMat4x4 = l_SubIdx + 1;

//                sprintf(l_Buff, "float4x4        g_Mat44_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsMat44 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_Mat44_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$M33")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_DMatrix3x3f)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumMat3x3)
                    io_Context.m_NumMat3x3 = l_SubIdx + 1;

//                sprintf(l_Buff, "float3x3        g_Mat33_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsMat33 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_Mat33_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$M22")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_DMatrix2x2f)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumMat2x2)
                    io_Context.m_NumMat2x2 = l_SubIdx + 1;

//                sprintf(l_Buff, "float2x2        g_Mat22_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsMat22 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_Mat22_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$F4")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_DVector4f)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumFloat4)
                    io_Context.m_NumFloat4 = l_SubIdx + 1;

//                sprintf(l_Buff, "float4          g_F4_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsFloat4 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_F4_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$F3")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_DVector3f)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumFloat3)
                    io_Context.m_NumFloat3 = l_SubIdx + 1;

//                sprintf(l_Buff, "float3          g_F3_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsFloat3 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_F3_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$F2")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_DVector2f)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumFloat2)
                    io_Context.m_NumFloat2 = l_SubIdx + 1;

//                sprintf(l_Buff, "float2          g_F2_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsFloat2 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_F2_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$F1")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_F32)
                    return -1;

                if(l_SubIdx >= io_Context.m_NumFloat1)
                    io_Context.m_NumFloat1 = l_SubIdx + 1;

//                sprintf(l_Buff, "float           g_F1_%lld;\n", l_SubIdx);
//                io_Context.m_ShaderParamsFloat1 += l_Buff;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", g_F1_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else if (l_ParamType == "$REF")
            {
                if(l_InputTypeID != RCGlobal::k_SimpleTypeID_OSTexture2D || !l_pGpuOperator->canMergeInput(l_InputIndex))
                    return -1;

                l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), ", l_Operation_%lld", l_SubIdx);
                DOME_ASSERT(l_BuffUsed > 0);
                l_OperationParams += l_Buff;
            }
            else
                return -1;
        }
        else
            return -1;

        l_InputIndex ++;
    }

    l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float4 l_Operation_%lld = Process%s(__RENDERPIPELINEPARAMETERS_CALL__%s);\n", io_Context.m_NumOperation, l_OperatorName.c_str(), l_OperationParams.c_str());
    DOME_ASSERT(l_BuffUsed > 0);
    io_Context.m_ShaderMainCode += l_Buff;
    io_Context.m_NumOperation ++;

    return i_pString - l_pOldStart;
}

DResult MDExecuter::compile(const DString& i_Signature, DString& o_PSShader, Int& o_NumTex, Int& o_NumTex3D, Int& o_NumTexCube, Int& o_NumMat4x4, Int& o_NumMat3x3, Int& o_NumMat2x2, Int& o_NumFloat4, Int& o_NumFloat3, Int& o_NumFloat2, Int& o_NumFloat1)
{
    Int l_BuffUsed;
    Char l_Buff[2048];
    const Char* l_pCurPos = i_Signature.c_str();
    _MDECompileContext l_CompileContext;

    l_CompileContext.m_NumOperation = 0;
    l_CompileContext.m_NumTex = 0;
	l_CompileContext.m_NumTex3D = 0;
    l_CompileContext.m_NumTexCube = 0;
    l_CompileContext.m_NumMat4x4 = 0;
    l_CompileContext.m_NumMat3x3 = 0;
    l_CompileContext.m_NumMat2x2 = 0;
    l_CompileContext.m_NumFloat4 = 0;
    l_CompileContext.m_NumFloat3 = 0;
    l_CompileContext.m_NumFloat2 = 0;
    l_CompileContext.m_NumFloat1 = 0;

    l_CompileContext.m_ShaderHead = "\
#pragma pack_matrix( row_major )                                                            \n\
#define __RENDERPIPELINEPARAMETERS_DEF__              float2 i_UV, float4 i_Pos             \n\
#define __RENDERPIPELINEPARAMETERS_CALL__             i_UV, i_Pos                           \n\
SamplerState RCPointSampler : register(s0)                                                  \n\
{                                                                                           \n\
    Filter = MIN_MAG_MIP_POINT;                                                             \n\
    AddressU = Clamp;                                                                       \n\
    AddressV = Clamp;                                                                       \n\
};                                                                                          \n\
SamplerState RCLinearSampler : register(s1)                                                 \n\
{                                                                                           \n\
    Filter = MIN_MAG_MIP_LINEAR;                                                            \n\
    AddressU = Clamp;                                                                       \n\
    AddressV = Clamp;                                                                       \n\
};                                                                                          \n\
SamplerState RCPointWrapSampler : register(s2)                                              \n\
{                                                                                           \n\
    Filter = MIN_MAG_MIP_POINT;                                                             \n\
    AddressU = Wrap;                                                                        \n\
    AddressV = Wrap;                                                                        \n\
};                                                                                          \n\
SamplerState RCLinearWrapSampler : register(s3)                                             \n\
{                                                                                           \n\
    Filter = MIN_MAG_MIP_LINEAR;                                                            \n\
    AddressU = Wrap;                                                                        \n\
    AddressV = Wrap;                                                                        \n\
};                                                                                          \n\
void RCGetTexture2DSize(Texture2D t, out float4 o_Size)                                     \n\
{                                                                                           \n\
    t.GetDimensions(o_Size.x, o_Size.y);                                                    \n\
    o_Size.zw = 1.0 / o_Size.xy;                                                            \n\
}                                                                                           \n\
float4 RCSampleTexture2D(Texture2D i_Tex, SamplerState i_Sampler, float2 i_UV)              \n\
{                                                                                           \n\
    return i_Tex.Sample(i_Sampler, i_UV);                                                   \n\
}                                                                                           \n\
cbuffer cbCommonParamBlock : register(b1)                                                   \n\
{                                                                                           \n\
    float4 DeviceZToLinearZParam;                                                           \n\
    int    IsReverseZ;                                                                      \n\
    float3 Pad;                                                                             \n\
}                                                                                           \n\
";

    l_CompileContext.m_ShaderParamsBegin = "\
cbuffer cbShaderParamBlock : register( b0 )                                                 \n\
{                                                                                           \n\
";
    l_CompileContext.m_ShaderParamsEnd = "};\n";

    l_CompileContext.m_ShaderMainBegin = "\
float4 RCPSMain(float4 i_Pos : SV_POSITION, float2 i_UV : TEXCOORD0) : SV_TARGET0           \n\
{                                                                                           \n\
";

    l_CompileContext.m_ShaderMainEnd = "}\n";

    Int l_NChar = MDParseOperation(l_pCurPos, l_CompileContext);
    if(l_NChar < 0)
        return R_FAILED;
    if(l_pCurPos[l_NChar] != 0)
        return R_FAILED;

    // Build texture and other parameters
    {
        Int                     l_BuffUsed;
        Char                    l_Buff[2048];
        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumTex; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "Texture2D g_Tex_%lld : register( t%lld );\n", l_SubIdx, l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsTex += l_Buff;

            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float4 l_TexVal_%lld = RCSampleTexture2D(g_Tex_%lld, RCLinearSampler, i_UV);\n", l_SubIdx, l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderMainPreSample += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumTex3D; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "Texture3D       g_T3D_%lld : register( t%lld );\n", l_SubIdx, l_SubIdx + l_CompileContext.m_NumTex);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsTex3D += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumTexCube; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "TextureCube       g_TCB_%lld : register( t%lld );\n", l_SubIdx, l_SubIdx + l_CompileContext.m_NumTex3D);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsTexCube += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumMat4x4; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float4x4        g_Mat44_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsMat44 += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumMat3x3; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float3x3        g_Mat33_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsMat33 += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumMat2x2; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float2x2        g_Mat22_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsMat22 += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumFloat4; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float4          g_F4_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsFloat4 += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumFloat3; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float3          g_F3_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsFloat3 += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumFloat2; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float2          g_F2_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsFloat2 += l_Buff;
        }

        for (Int l_SubIdx = 0; l_SubIdx < l_CompileContext.m_NumFloat1; ++l_SubIdx)
        {
            l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "float           g_F1_%lld;\n", l_SubIdx);
            DOME_ASSERT(l_BuffUsed > 0);
            l_CompileContext.m_ShaderParamsFloat1 += l_Buff;
        }
    }

    DString l_ShaderParamsMat3x3Pad;
    DString l_ShaderParamsMat2x2Pad;
    DString l_ShaderParamsFloat3Pad;
    DString l_ShaderParamsFloat2Pad;
    DString l_ReturnCode;

    if (l_CompileContext.m_NumMat3x3 > 0)
        l_ShaderParamsMat3x3Pad = "float           g_Mat33_Pad;\n";
    if (l_CompileContext.m_NumMat2x2 > 0)
        l_ShaderParamsMat2x2Pad = "float2          g_Mat22_Pad;\n";
    if(l_CompileContext.m_NumFloat3 > 0)
        l_ShaderParamsFloat3Pad = "float           g_Float3_Pad;\n";
    if(l_CompileContext.m_NumFloat2 % 2 == 1)
        l_ShaderParamsFloat2Pad = "float2          g_Float2_Pad;\n";
    l_BuffUsed = OS_String::StrFormat(l_Buff, sizeof(l_Buff), "return l_Operation_%lld;\n", l_CompileContext.m_NumOperation - 1);
    DOME_ASSERT(l_BuffUsed > 0);
    l_ReturnCode = l_Buff;

    DString l_ShaderCode = 
        l_CompileContext.m_ShaderHead + 
        l_CompileContext.m_ShaderGlobal + 
        l_CompileContext.m_ShaderParamsTex +
        l_CompileContext.m_ShaderParamsTex3D +
        l_CompileContext.m_ShaderParamsTexCube +
        l_CompileContext.m_ShaderParamsBegin + 
        l_CompileContext.m_ShaderParamsMat44 + 
        l_CompileContext.m_ShaderParamsMat33 + 
        l_ShaderParamsMat3x3Pad +
        l_CompileContext.m_ShaderParamsMat22 + 
        l_ShaderParamsMat2x2Pad +
        l_CompileContext.m_ShaderParamsFloat4 + 
        l_CompileContext.m_ShaderParamsFloat3 + 
        l_ShaderParamsFloat3Pad +
        l_CompileContext.m_ShaderParamsFloat2 + 
        l_ShaderParamsFloat2Pad + 
        l_CompileContext.m_ShaderParamsFloat1 +
        l_CompileContext.m_ShaderParamsEnd + 
        l_CompileContext.m_ShaderMainBegin + 
        l_CompileContext.m_ShaderMainPreSample + 
        l_CompileContext.m_ShaderMainCode + 
        l_ReturnCode + 
        l_CompileContext.m_ShaderMainEnd; 

    o_PSShader = l_ShaderCode;
    o_NumTex = l_CompileContext.m_NumTex;
	o_NumTex3D = l_CompileContext.m_NumTex3D;
    o_NumTexCube = l_CompileContext.m_NumTexCube;
    o_NumMat4x4 = l_CompileContext.m_NumMat4x4;
    o_NumMat3x3 = l_CompileContext.m_NumMat3x3;
    o_NumMat2x2 = l_CompileContext.m_NumMat2x2;
    o_NumFloat4 = l_CompileContext.m_NumFloat4;
    o_NumFloat3 = l_CompileContext.m_NumFloat3;
    o_NumFloat2 = l_CompileContext.m_NumFloat2;
    o_NumFloat1 = l_CompileContext.m_NumFloat1;

    return R_SUCCESS;
}


RC_NAMESPACE_END