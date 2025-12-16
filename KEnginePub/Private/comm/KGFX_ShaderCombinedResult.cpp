#include "KGFX_ShaderCombinedResult.h"
#include <sstream>

#include "KBase/Public/KMemLeak.h"
#include "CommonDefine.h"
#define TOKEN_SIZE      256
#define BLOCK_NAME_SIZE 64

using namespace gfx;
/////////////////////////////////////////////////////////////////////////////////////////

namespace KGFX_SHADER_COMBINE_HELPER
{
    BOOL IsFirstCharInTable(const char* pLine, char table[], uint32_t count)
    {
        BOOL     bPass = false;
        uint32_t pos   = 0;
        char     c     = ' ';
        for (;;)
        {
            if (pLine[pos] != ' ' && pLine[pos] != '\t')
            {
                c = pLine[pos];
                break;
            }
            if (pLine[pos] == '\0')
            {
                break;
            }
            ++pos;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            if (c == table[i])
            {
                bPass = true;
                break;
            }
        }
        return bPass;
    }

    void GetTwoToken(char* pLine, const char** pFirst, const char** pSecond)
    {
        char*       saveptr = nullptr;
        const char* pDlim   = " \t:;[";
        char*       token   = strtok_r(pLine, pDlim, &saveptr);
        if (pFirst)
        {
            *pFirst = token;
        }
        if (pSecond)
        {
            *pSecond = nullptr;
        }
        if (token != nullptr)
        {
            token = strtok_r(nullptr, pDlim, &saveptr);
            if (pSecond)
            {
                *pSecond = token;
            }
        }
    }

    void GetKeyValueToken(char* pLine, const char** pFirst, const char** pSecond)
    {
        char*       saveptr = nullptr;
        const char* pDlim   = " \t=:;[";
        char*       token   = strtok_r(pLine, pDlim, &saveptr);
        if (pFirst)
        {
            *pFirst = token;
        }
        if (pSecond)
        {
            *pSecond = nullptr;
        }
        if (token != nullptr)
        {
            token = strtok_r(nullptr, pDlim, &saveptr);
            if (pSecond)
            {
                *pSecond = token;
            }
        }
    }

    void GetBorderColor(char* pLine, const char** pFirst, const char** pSecond, const char** c0, const char** c1, const char** c2, const char** c3)
    {
        char*       saveptr = nullptr;
        const char* pDlim   = " \t=:;,[()";
        char*       token   = strtok_r(pLine, pDlim, &saveptr);

        if (pFirst)
            *pFirst = token;
        if (pSecond)
            *pSecond = nullptr;
        if (c0)
            *c0 = nullptr;
        if (c1)
            *c1 = nullptr;
        if (c2)
            *c2 = nullptr;
        if (c3)
            *c3 = nullptr;

        if (token)
        {
            token = strtok_r(nullptr, pDlim, &saveptr);
            if (pSecond)
            {
                *pSecond = token;
            }
            if (token)
            {
                token = strtok_r(nullptr, pDlim, &saveptr);
                if (token && c0)
                {
                    *c0 = token;
                }
                if (token)
                {
                    token = strtok_r(nullptr, pDlim, &saveptr);
                    if (token && c1)
                    {
                        *c1 = token;
                    }
                    if (token)
                    {
                        token = strtok_r(nullptr, pDlim, &saveptr);
                        if (token && c2)
                        {
                            *c2 = token;
                        }

                        if (token)
                        {
                            token = strtok_r(nullptr, pDlim, &saveptr);
                            if (token && c3)
                            {
                                *c3 = token;
                            }
                        }
                    }
                }
            }
        }
    }

    const char* GetSecondToken(char* pLine)
    {
        char*       saveptr = nullptr;
        const char* pDlim   = " :;[";
        const char* token   = strtok_r(pLine, pDlim, &saveptr);
        if (token != nullptr)
        {
            token = strtok_r(nullptr, pDlim, &saveptr);
        }
        return token;
    }

    BOOL IsCommentSegementBegin(const char* pLine)
    {
        BOOL bRet = false;
        if (pLine[0] == '/' && pLine[1] == '*')
        {
            bRet = true;
        }
        return bRet;
    }

    BOOL IsCommentSegementEnd(const char* pLine)
    {
        BOOL bRet = false;
        if (pLine[0] == '*' && pLine[1] == '/')
        {
            bRet = true;
        }
        return bRet;
    }


    BOOL IsCommentLine(const char* pLine)
    {
        const char* p    = pLine;
        BOOL        bRet = FALSE;
        while (p)
        {
            if (*p == ' ')
            {
                p++;
                continue;
            }

            if (*p == '/' && *(p + 1) == '/')
            {
                bRet = TRUE;
            }
            break;
        }
        return bRet;
    }
    uint32_t GetLineBeginPos(const char* pString)
    {
        uint32_t pos = 0;
        for (;;)
        {
            if (pString[pos] != ' ' && pString[pos] != '\t')
            {
                break;
            }
            pos++;
        }
        return pos;
    }

    BOOL ReadEndToken(const char* pString, char* pOut)
    {
        BOOL bReaded  = false;
        pOut[0]       = '\0';
        const char* p = strrchr(pString, ' ') + 1;
        if (p && p[0])
        {
            uint32_t i = 0;
            for (;;)
            {
                pOut[i] = p[i];
                bReaded = true;
                if (p[i] == '\0')
                {
                    break;
                }
                ++i;
            }
        }
        return bReaded;
    }

    BOOL ReadTokenBetween(const char* pString, char cBegin, char cEnd, char* pOut)
    {
        uint32_t i               = 0;
        uint32_t j               = 0;
        BOOL     bBegin          = false;
        BOOL     bEnd            = false;
        pOut[0]                  = '\0';
        BOOL        readed       = false;
        const char* pStringBeing = nullptr;
        for (;;)
        {
            if (pString[i] == cBegin)
            {
                bBegin       = true;
                pStringBeing = pString + i;
            }
            if (pString[i] == cEnd)
            {
                bEnd = true;
            }
            if (pString[i] == '\0')
            {
                break;
            }
            ++i;
        }
        if (!bBegin || !bEnd)
        {
            // 没头没尾，不用继续了
            return false;
        }
        i       = 0;
        bBegin  = false;
        bEnd    = false;
        pString = pStringBeing;
        for (;;)
        {
            if (pString[i] == cBegin)
            {
                bBegin = true;
                ++i;

                while (pString[i] == ' ' && pString[i] != cEnd && pString[i] != '\0')
                {
                    ++i;
                }
            }

            if (bBegin && pString[i] != ' ' && pString[i] != cEnd)
            {
                readed  = true;
                pOut[j] = pString[i];
                ++j;
            }
            ++i;

            if ((bBegin && (pString[i] == cEnd || pString[i] == ' ')) || pString[i] == '\0')
            {
                pOut[j] = '\0';
                break;
            }
        }


        return readed;
    }
}; // namespace KGFX_SHADER_COMBINE_HELPER

const char* KGFX_CombinedShaderResult::GetShaderResult(gfx::ShaderStageType eShaderStage)
{
    const char*  pResultString = nullptr;
    std::string* pResult       = _GetResultString(eShaderStage);
    if (pResult)
    {
        pResultString = pResult->c_str();
    }
    return pResultString;
}

uint32_t KGFX_CombinedShaderResult::GetShaderResultSize(gfx::ShaderStageType eShaderStage)
{
    uint32_t     uSize   = 0;
    std::string* pResult = _GetResultString(eShaderStage);
    if (pResult)
    {
        uSize = (uint32_t)pResult->size();
    }
    return uSize;
}

void KGFX_CombinedShaderResult::SetPreviousPushConstsSize(uint32_t uSize)
{
    m_uPreviousConstsSize = uSize;
}
uint32_t KGFX_CombinedShaderResult::GetPreviousPushConstsSize()
{
    return m_uPreviousConstsSize;
}

std::string* KGFX_CombinedShaderResult::_GetResultString(gfx::ShaderStageType eShaderStage)
{
    std::string* pResult = nullptr;
    switch (eShaderStage)
    {
    case gfx::ShaderStageType::Vertex:
        pResult = &m_szBinedShaderString[0];
        break;
    case gfx::ShaderStageType::Hull:
        pResult = &m_szBinedShaderString[4];
        break;
    case gfx::ShaderStageType::Domain:
        pResult = &m_szBinedShaderString[5];
        break;
    case gfx::ShaderStageType::Geometry:
        pResult = &m_szBinedShaderString[3];
        break;
    case gfx::ShaderStageType::Fragment:
        pResult = &m_szBinedShaderString[1];
        break;
    case gfx::ShaderStageType::Compute:
        pResult = &m_szBinedShaderString[2];
        break;
    case gfx::ShaderStageType::RayGeneration:
        break;
    case gfx::ShaderStageType::Intersection:
        break;
    case gfx::ShaderStageType::AnyHit:
        break;
    case gfx::ShaderStageType::Miss:
        break;
    case gfx::ShaderStageType::Callable:
        break;
    case gfx::ShaderStageType::Amplification:
        break;
    case gfx::ShaderStageType::Mesh:
        break;
    default:
        break;
    }
    ASSERT(pResult);
    return pResult;
}

void KGFX_CombinedShaderResult::Clear()
{
    std::string _tmp0;
    std::string _tmp1;
    std::string _tmp2;
    m_szBinedShaderString[0].swap(_tmp0);
    m_szBinedShaderString[1].swap(_tmp1);
    m_szBinedShaderString[2].swap(_tmp2);
}

/////////////////////////////////////////////////////////////////////////////////////////

KGFX_CombinedShaderResultVK_HLSL::KGFX_CombinedShaderResultVK_HLSL()
{
    m_bWholeOnePushConst = false;
}
KGFX_CombinedShaderResultVK_HLSL::~KGFX_CombinedShaderResultVK_HLSL()
{
    for (uint32_t i = 0; i < SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT; ++i)
    {
        for (auto it : m_vecSamplerState[i])
        {
            _SamplerStateItem* pSamplerStateItem = it;
            SAFE_DELETE(pSamplerStateItem);
        }
        m_vecSamplerState[i].clear();
    }
}

enum _SegmentFixProcessing
{
    NONE_SEGMENT_FIXING,
    INPUT_SEGMENT_FIXING,
    OUTPUT_SEGMENT_FIXING,
    SAMPLER_SEGMENT_FIXING,
    PUSHCONST_STRUCT_SEGMENT_FIXING,
    PUSHCONST_INSTANCE_SEGMENT_FIXING,
    SPECIALIAZATION_CONST_SEGMENT_FIXING
};


BOOL KGFX_CombinedShaderResultVK_HLSL::Fix(const char* pcszShaderName, const char* pcszSrcShaderContent, gfx::ShaderStageType eShaderStage)
{
    static BOOL bDebugPrint = false;
    BOOL        bResult     = false;
    BOOL        bRetCode    = false;

    // 当前独占要处理的段落任务，一个段落任务没处理完成，不会去做别的段落逻辑任务
    _SegmentFixProcessing fixing = NONE_SEGMENT_FIXING;

    uint32_t eShaderStageId = GetGraphicAndComputeShaderId(eShaderStage);

    std::string* pString = _GetResultString(eShaderStage);
    *pString             = pcszSrcShaderContent;

    BOOL bIsCommentLine         = false;
    BOOL bIsCommentSegmentBegin = false;

    std::string fixedContent;
    fixedContent.reserve(pString->size() + 1024);

    // 注释代码段强行换行，后面按行解析的时候才方便处理端起端末
    size_t commentSegmentPos = pString->find("/*");
    while (commentSegmentPos != std::string::npos)
    {
        size_t pos0                  = pString->rfind("//", commentSegmentPos);
        size_t pos1                  = pString->rfind("\n", commentSegmentPos);
        BOOL   bBeforeComment_Inline = false;
        if (pos0 != std::string::npos && pos1 != std::string::npos && pos0 > pos1)
        {
            // 如果当前行 /* 前面 还出现了//，那么也不用换行了
            bBeforeComment_Inline = true;
        }

        if (commentSegmentPos > 0 && (*pString)[commentSegmentPos - 1] != '/' && bBeforeComment_Inline == false)
        {
            // 碰到 /*这种就换行，但是//*这种不换行
            pString->insert(commentSegmentPos, "\n");
        }

        commentSegmentPos = pString->find("/*", commentSegmentPos + 3);
    }

    commentSegmentPos = pString->find("*/");
    while (commentSegmentPos != std::string::npos)
    {
        pString->insert(commentSegmentPos, "\n");
        commentSegmentPos = pString->find("*/", commentSegmentPos + 3);
    }

    std::string       line;
    std::stringstream ss(*pString);
    char              token[TOKEN_SIZE];
    token[0]                     = '\0';
    uint32_t uOutPutVertLocation = 0;
    char     prefix[TOKEN_SIZE];
    prefix[0] = '\0';

    gfx::KSamplerState* pFixingSamplerState = nullptr;
    BOOL                bInBlock            = false;

    while (std::getline(ss, line))
    {
        if (!bInBlock && line[0] == '{')
        {
            bInBlock = true;
        }

        if (bInBlock && line[0] == '}')
        {
            bInBlock = false;
        }

        bIsCommentLine = KGFX_SHADER_COMBINE_HELPER::IsCommentLine(line.c_str());
        if (bIsCommentLine)
        {
            goto LoopEnd;
        }

        if (!bIsCommentSegmentBegin && KGFX_SHADER_COMBINE_HELPER::IsCommentSegementBegin(line.c_str()))
        {
            bIsCommentSegmentBegin = true;
            goto LoopEnd;
        }

        if (KGFX_SHADER_COMBINE_HELPER::IsCommentSegementEnd(line.c_str()))
        {
            bIsCommentSegmentBegin = false;
            goto LoopEnd;
        }

        if (bIsCommentSegmentBegin)
        {
            // 注释段落优先级最高，可以插入任何任务，没做完，不继续处理
            goto LoopEnd;
        }


        {
            uint32_t linesize = (uint32_t)line.size();

            size_t pos0 = 0;


            /////////////////////////////////////fix input output/////////////////////////////
            if (fixing == NONE_SEGMENT_FIXING && linesize > 5 &&
                line.find("struct") != std::string::npos &&
                ((line.find("VertexIn") != std::string::npos && eShaderStage != gfx::ShaderStageType::Fragment) || line.find("SPIRV_Cross_Input") != std::string::npos))
            {
                fixing = INPUT_SEGMENT_FIXING;
                goto LoopEnd;
            }

            if (eShaderStage != gfx::ShaderStageType::Compute && fixing == INPUT_SEGMENT_FIXING && linesize > 5 && line.find(":") != std::string::npos && line.find("SV_POSITION") == std::string::npos && line.find("SV_VertexID") == std::string::npos && line.find("SV_InstanceID") == std::string::npos)
            {
                bRetCode = KGFX_SHADER_COMBINE_HELPER::ReadTokenBetween(line.c_str(), ':', ';', token);
                ASSERT(bRetCode);
                uint32_t beginPos = KGFX_SHADER_COMBINE_HELPER::GetLineBeginPos(line.c_str());

                if (eShaderStage == gfx::ShaderStageType::Vertex)
                {
                    sprintf(prefix, "[[vk::location(_%s)]] ", token);
                    line.insert(beginPos, prefix);
                }
                else
                {
                    auto it = m_mapVertexOutLocationTable.find(token);
                    if (it != m_mapVertexOutLocationTable.end())
                    {
                        uOutPutVertLocation = it->second;
                        snprintf(prefix, TOKEN_SIZE, "[[vk::location(%d)]] ", uOutPutVertLocation);
                        line.insert(beginPos, prefix);
                    }
                    else
                    {
                        KGLogPrintf(KGLOG_ERR, "%s ps output 没有 vs对应的项 %s, 请检查shader匹配情况", pcszShaderName, token);
                        // ASSERT(0);
                    }
                }

                goto LoopEnd;
            }

            if (fixing == INPUT_SEGMENT_FIXING && line.find("};") != std::string::npos)
            {
                fixing = NONE_SEGMENT_FIXING;
                goto LoopEnd;
            }

            ///////////////////////////////////////fix output/////////////////////////////////////
            if (fixing == NONE_SEGMENT_FIXING && linesize > 5 && line.find("struct") != std::string::npos &&
                (line.find("VertexOut") != std::string::npos ||
                 line.find("GSOutput") != std::string::npos ||
                 line.find("DSOutput") != std::string::npos ||
                 line.find("HSOutput") != std::string::npos ||
                 line.find("SPIRV_Cross_Output") != std::string::npos))
            {
                fixing              = OUTPUT_SEGMENT_FIXING;
                // output如果有多个，每次碰到就重新计数
                uOutPutVertLocation = 0;
                goto LoopEnd;
            }

            if (fixing == OUTPUT_SEGMENT_FIXING && linesize > 5 &&
                line.find(":") != std::string::npos &&
                line.find("SV_POSITION") == std::string::npos &&
                line.find("SV_Position") == std::string::npos &&
                line.find("SV_TARGET") == std::string::npos &&
                line.find("SV_Target") == std::string::npos)
            {
                bRetCode = KGFX_SHADER_COMBINE_HELPER::ReadTokenBetween(line.c_str(), ':', ';', token);
                ASSERT(bRetCode);
                uint32_t beginPos = KGFX_SHADER_COMBINE_HELPER::GetLineBeginPos(line.c_str());
                if (eShaderStage == gfx::ShaderStageType::Vertex || eShaderStage == gfx::ShaderStageType::Geometry || eShaderStage == gfx::ShaderStageType::Hull || eShaderStage == gfx::ShaderStageType::Domain)
                {
                    snprintf(prefix, TOKEN_SIZE, "[[vk::location(%d)]] ", uOutPutVertLocation);
                    m_mapVertexOutLocationTable.insert(std::make_pair<>(token, uOutPutVertLocation));
                    ++uOutPutVertLocation;
                }
                else
                {
                    auto it = m_mapVertexOutLocationTable.find(token);
                    if (it != m_mapVertexOutLocationTable.end())
                    {
                        uOutPutVertLocation = it->second;
                        snprintf(prefix, TOKEN_SIZE, "[[vk::location(%d)]] ", uOutPutVertLocation);
                    }
                    else
                    {
                        KGLogPrintf(KGLOG_WARNING, "%s ps output 没有 vs对应的项 %s, 请检查shader匹配情况", pcszShaderName, token);
                        // ASSERT(0);
                    }
                }
                line.insert(beginPos, prefix);
                goto LoopEnd;
            }

            if (fixing == OUTPUT_SEGMENT_FIXING && line.find("};") != std::string::npos)
            {
                fixing = NONE_SEGMENT_FIXING;
                goto LoopEnd;
            }

            ///////////////////////////////////////处理pushconsts struct/////////////////////////////////////
            //if (fixing == NONE_SEGMENT_FIXING && linesize > 5 && line.find("struct") != std::string::npos && line.find("PushConsts") != std::string::npos)
            //{
            //    fixing = PUSHCONST_STRUCT_SEGMENT_FIXING;
            //    goto LoopEnd;
            //}

            //if (fixing == PUSHCONST_STRUCT_SEGMENT_FIXING && line.find("{") != std::string::npos)
            //{
            //    if (eShaderStage != gfx::ShaderStageType::Vertex && !m_bWholeOnePushConst)
            //    {
            //        snprintf(prefix, TOKEN_SIZE, "\n[[vk::offset(%d)]] ", GetPreviousPushConstsSize());
            //        line.append(prefix);
            //    }
            //    goto LoopEnd;
            //}

            //if (fixing == PUSHCONST_STRUCT_SEGMENT_FIXING && line.find("};") != std::string::npos)
            //{
            //    fixing = NONE_SEGMENT_FIXING;
            //    goto LoopEnd;
            //}


            ///////////////////////////////////////处理pushconst instance/////////////////////////////////////
            //size_t pos_PushConsts_ = line.find("PushConsts_");
            //if (fixing == NONE_SEGMENT_FIXING && linesize > 5 && line.find("cbuffer") != std::string::npos && pos_PushConsts_ != std::string::npos)
            //{
            //    size_t n = pos_PushConsts_ + strlen("PushConsts_");
            //    if (n == line.length())
            //    {
            //        // PushConsts_ 结尾，没有Shader类别，那么认为是共享的整个pushconst，不用分段加offset
            //        m_bWholeOnePushConst = true;
            //    }
            //    fixing = PUSHCONST_INSTANCE_SEGMENT_FIXING;
            //    // 忽略这一行，不添加
            //    continue;
            //}

            //if (fixing == PUSHCONST_INSTANCE_SEGMENT_FIXING)
            //{
            //    if (line.find("PushConsts") != std::string::npos)
            //    {
            //        uint32_t beginPos = KGFX_SHADER_COMBINE_HELPER::GetLineBeginPos(line.c_str());
            //        if (beginPos)
            //        {
            //            line.erase(0, beginPos);
            //        }
            //        line.insert(0, "[[vk::push_constant]] ");
            //        goto LoopEnd;
            //    }
            //    else if (line.find("};") != std::string::npos)
            //    {
            //        fixing = NONE_SEGMENT_FIXING;
            //        // 忽略这一行，不添加
            //        continue;
            //    }
            //    else
            //    {
            //        // 不是PushConsts行，统统忽略掉
            //        continue;
            //    }
            //}
            //
            if (fixing == NONE_SEGMENT_FIXING && linesize > 5 && line.find("cbuffer") != std::string::npos && line.find("SpecializationConsts") != std::string::npos)
            {
                goto LoopEnd;
            }
            ///////////////////////////////////////处理specialization const////////////////////////////////
            //if (fixing == NONE_SEGMENT_FIXING && linesize > 5 && line.find("cbuffer") != std::string::npos && line.find("SpecializationConsts") != std::string::npos)
            //{
            //    fixing = SPECIALIAZATION_CONST_SEGMENT_FIXING;
            //    continue;
            //}

            //if (fixing == SPECIALIAZATION_CONST_SEGMENT_FIXING && line.find("};") != std::string::npos)
            //{
            //    fixing = NONE_SEGMENT_FIXING;
            //    continue;
            //}

            //if (fixing == SPECIALIAZATION_CONST_SEGMENT_FIXING)
            //{
            //    if (strchr(line.c_str(), ';'))
            //    {
            //        BOOL bIsIntOrUInt = false;
            //        BOOL bIsFloat     = false;

            //        const char* pType      = nullptr;
            //        const char* pParamName = nullptr;

            //        uint32_t beginPos = KGFX_SHADER_COMBINE_HELPER::GetLineBeginPos(line.c_str());
            //        if (beginPos)
            //        {
            //            line.erase(0, beginPos);
            //        }
            //        strncpy(token, line.c_str(), TOKEN_SIZE);
            //        KGFX_SHADER_COMBINE_HELPER::GetTwoToken(token, &pType, &pParamName);

            //        ASSERT(pType && pType[0] && pParamName && pParamName[0]);
            //        if (pType && pType[0] && pParamName && pParamName[0])
            //        {
            //            if (strstr(pType, "int"))
            //            {
            //                bIsIntOrUInt = true;
            //            }
            //            else if (strstr(pType, "float"))
            //            {
            //                bIsFloat = true;
            //            }
            //            else
            //            {
            //                KGLogPrintf(KGLOG_ERR, "%s,请检查 %s", "specializationConst  只支持 int uint float 三种类型", pParamName);
            //                goto Exit0;
            //            }

            //            uint32_t locationId = 0;
            //            auto     it         = m_mapSpecializationTable.find(pParamName);
            //            if (it == m_mapSpecializationTable.end())
            //            {
            //                locationId = m_uMaxSpecializationId++;
            //                m_mapSpecializationTable.insert(std::make_pair<>(pParamName, locationId));
            //            }
            //            else
            //            {
            //                locationId = it->second;
            //            }
            //            if (bIsIntOrUInt)
            //            {
            //                snprintf(prefix, TOKEN_SIZE, "[[vk::constant_id(%d)]] const %s %s = 0u;", locationId, pType, pParamName);
            //            }
            //            else if (bIsFloat)
            //            {
            //                snprintf(prefix, TOKEN_SIZE, "[[vk::constant_id(%d)]] const %s %s = 0.0;", locationId, pType, pParamName);
            //            }
            //            line = prefix;
            //        }
            //        goto LoopEnd;
            //    }
            //    else
            //    {
            //        continue;
            //    }
            //}


            ///////////////////////////////////////专门处理SamplerState/////////////////////////////////////
            if (fixing == NONE_SEGMENT_FIXING && linesize > 5 && (line.find("SamplerState") != std::string::npos || line.find("SamplerComparisonState") != std::string::npos) &&
                line.find(",") == std::string::npos && line.find(")") == std::string::npos && line.find("{") == std::string::npos)
            {
                size_t registorPos = line.find(":");
                if (registorPos != std::string::npos)
                {
                    line.erase(registorPos);
                }
                uint32_t beginPos = KGFX_SHADER_COMBINE_HELPER::GetLineBeginPos(line.c_str());

                strncpy(token, line.c_str(), TOKEN_SIZE);
                const char* pBLockName = KGFX_SHADER_COMBINE_HELPER::GetSecondToken(token);
                ASSERT(pBLockName && pBLockName[0]);

                if (pBLockName && pBLockName[0])
                {
                    uint32_t bindid = 0;
                    auto     it     = m_mapBindingTable.find(pBLockName);
                    if (it == m_mapBindingTable.end())
                    {
                        bindid = m_uMaxBindingId++;
                        m_mapBindingTable.insert(std::make_pair<>(pBLockName, bindid));
                    }
                    else
                    {
                        bindid = it->second;
                    }
                    snprintf(prefix, TOKEN_SIZE, "[[vk::binding(%d,0)]] ", bindid);
                    line.insert(beginPos, prefix);

                    BOOL bFind = false;
                    for (auto it : m_vecSamplerState[eShaderStageId])
                    {
                        _SamplerStateItem* pSamplerStateItem = it;
                        if (pSamplerStateItem->name == pBLockName)
                        {
                            bFind               = true;
                            pFixingSamplerState = &pSamplerStateItem->samplerState;
                        }
                    }
                    ASSERT(!bFind);
                    if (!bFind)
                    {
                        _SamplerStateItem* pSamplerStateItem = new _SamplerStateItem;
                        pSamplerStateItem->name              = pBLockName;
                        pFixingSamplerState                  = &pSamplerStateItem->samplerState;
                        m_vecSamplerState[eShaderStageId].emplace_back(pSamplerStateItem);
                    }
                }

                if (line.find(";") == std::string::npos)
                {
                    line.append(";");
                    fixing = SAMPLER_SEGMENT_FIXING;
                }
                goto LoopEnd;
            }

            if (fixing == SAMPLER_SEGMENT_FIXING)
            {
                if (line.find(";") != std::string::npos && line.find("}") == std::string::npos)
                {
                    const char* pKey   = nullptr;
                    const char* pValue = nullptr;
                    strncpy(token, line.c_str(), TOKEN_SIZE);
                    KGFX_SHADER_COMBINE_HELPER::GetKeyValueToken(token, &pKey, &pValue);
                    if (pKey && pKey[0] && pValue && pValue[0])
                    {
                        pFixingSamplerState->bNeedShaderInit = false;
                        if (strcmp(pKey, "Filter") == 0)
                        {
                            if (strcmp(pValue, "MIN_MAG_MIP_POINT") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMinFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_NEAREST;
                            }
                            else if (strcmp(pValue, "MIN_MAG_POINT_MIP_LINEAR") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMinFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
                            }
                            else if (strcmp(pValue, "MIN_POINT_MAG_LINEAR_MIP_POINT") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMinFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_NEAREST;
                            }
                            else if (strcmp(pValue, "MIN_POINT_MAG_MIP_LINEAR") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMinFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
                            }
                            else if (strcmp(pValue, "MIN_LINEAR_MAG_MIP_POINT") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMinFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_NEAREST;
                            }
                            else if (strcmp(pValue, "MIN_LINEAR_MAG_POINT_MIP_LINEAR") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_NEAREST;
                                pFixingSamplerState->enuMinFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
                            }
                            else if (strcmp(pValue, "MIN_MAG_LINEAR_MIP_POINT") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMinFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_NEAREST;
                            }
                            else if (strcmp(pValue, "MIN_MAG_MIP_LINEAR") == 0)
                            {
                                pFixingSamplerState->enuMagFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMinFilter  = FILTER_LINEAR;
                                pFixingSamplerState->enuMipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
                            }
                            else if (strcmp(pValue, "ANISOTROPIC") == 0)
                            {
                                pFixingSamplerState->enuMagFilter   = FILTER_LINEAR;
                                pFixingSamplerState->enuMinFilter   = FILTER_LINEAR;
                                pFixingSamplerState->enuMipmapMode  = SAMPLER_MIPMAP_MODE_LINEAR;
                                pFixingSamplerState->bCompareEnable = true;
                            }
                            else
                            {
                                ASSERT(0);
                            }
                        }
                        else if (strcmp(pKey, "MaxAnisotropy") == 0)
                        {
                            pFixingSamplerState->fMaxAnisotropy = (float)atof(pValue);
                        }
                        else if (strcmp(pKey, "AddressU") == 0)
                        {
                            if (strcmp(pValue, "Wrap") == 0)
                            {
                                pFixingSamplerState->enuAddressModeU = SAMPLER_ADDRESS_MODE_REPEAT;
                            }
                            else if (strcmp(pValue, "Mirror") == 0)
                            {
                                pFixingSamplerState->enuAddressModeU = SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                            }
                            else if (strcmp(pValue, "MirrorOnce") == 0)
                            { // 这种可能有点不一样
                                pFixingSamplerState->enuAddressModeU = SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
                            }
                            else if (strcmp(pValue, "Clamp") == 0)
                            {
                                pFixingSamplerState->enuAddressModeU = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                            }
                            else if (strcmp(pValue, "Border") == 0)
                            {
                                pFixingSamplerState->enuAddressModeU = SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                            }
                            else
                            {
                                ASSERT(0);
                            }
                        }
                        else if (strcmp(pKey, "AddressV") == 0)
                        {
                            if (strcmp(pValue, "Wrap") == 0)
                            {
                                pFixingSamplerState->enuAddressModeV = SAMPLER_ADDRESS_MODE_REPEAT;
                            }
                            else if (strcmp(pValue, "Mirror") == 0)
                            {
                                pFixingSamplerState->enuAddressModeV = SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                            }
                            else if (strcmp(pValue, "MirrorOnce") == 0)
                            { // 这种可能有点不一样
                                pFixingSamplerState->enuAddressModeV = SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
                            }
                            else if (strcmp(pValue, "Clamp") == 0)
                            {
                                pFixingSamplerState->enuAddressModeV = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                            }
                            else if (strcmp(pValue, "Border") == 0)
                            {
                                pFixingSamplerState->enuAddressModeV = SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                            }
                            else
                            {
                                ASSERT(0);
                            }
                        }
                        else if (strcmp(pKey, "AddressW") == 0)
                        {
                            if (strcmp(pValue, "Wrap") == 0)
                            {
                                pFixingSamplerState->enuAddressModeW = SAMPLER_ADDRESS_MODE_REPEAT;
                            }
                            else if (strcmp(pValue, "Mirror") == 0)
                            {
                                pFixingSamplerState->enuAddressModeW = SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                            }
                            else if (strcmp(pValue, "MirrorOnce") == 0)
                            { // 这种可能有点不一样
                                pFixingSamplerState->enuAddressModeW = SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
                            }
                            else if (strcmp(pValue, "Clamp") == 0)
                            {
                                pFixingSamplerState->enuAddressModeW = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                            }
                            else if (strcmp(pValue, "Border") == 0)
                            {
                                pFixingSamplerState->enuAddressModeW = SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                            }
                            else
                            {
                                ASSERT(0);
                            }
                        }
                        else if (strcmp(pKey, "BorderColor") == 0)
                        {
                            char token2[TOKEN_SIZE];
                            token2[0] = '\0';
                            strncpy(token2, line.c_str(), TOKEN_SIZE);
                            const char *k = nullptr, *v = nullptr, *c0 = nullptr, *c1 = nullptr, *c2 = nullptr, *c3 = nullptr;
                            KGFX_SHADER_COMBINE_HELPER::GetBorderColor(token2, &k, &v, &c0, &c1, &c2, &c3);
                            BOOL bIsFloat = true;
                            if (strchr(c0, '.') == nullptr &&
                                strchr(c1, '.') == nullptr &&
                                strchr(c2, '.') == nullptr &&
                                strchr(c3, '.') == nullptr)
                            {
                                bIsFloat = false;
                            }
                            float r = (float)atof(c0);
                            float g = (float)atof(c1);
                            float b = (float)atof(c2);
                            float a = (float)atof(c3);
                            if (r == 0.0f && g == 0.0f && b == 0.0f && a == 0.0f)
                            {
                                if (bIsFloat)
                                {
                                    pFixingSamplerState->enuBorderColor = BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                                }
                                else
                                {
                                    pFixingSamplerState->enuBorderColor = BORDER_COLOR_INT_TRANSPARENT_BLACK;
                                }
                            }
                            else if (r == 0.0f && g == 0.0f && b == 0.0f && a == 1.0f)
                            {
                                if (bIsFloat)
                                {
                                    pFixingSamplerState->enuBorderColor = BORDER_COLOR_FLOAT_OPAQUE_BLACK;
                                }
                                else
                                {
                                    pFixingSamplerState->enuBorderColor = BORDER_COLOR_INT_OPAQUE_BLACK;
                                }
                            }
                            else if (r == 1.0f && g == 1.0f && b == 1.0f && a == 1.0f)
                            {
                                if (bIsFloat)
                                {
                                    pFixingSamplerState->enuBorderColor = BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                                }
                                else
                                {
                                    pFixingSamplerState->enuBorderColor = BORDER_COLOR_INT_OPAQUE_WHITE;
                                }
                            }
                        }
                        else if (strcmp(pKey, "MinLOD") == 0)
                        {
                            float v                        = (float)atof(pValue);
                            pFixingSamplerState->fToMinLod = v;
                        }
                        else if (strcmp(pKey, "MaxLOD") == 0)
                        {
                            float v                        = (float)atof(pValue);
                            pFixingSamplerState->fToMaxLod = v;
                        }
                        else if (strcmp(pKey, "MipLODBias") == 0)
                        {
                            float v                          = (float)atof(pValue);
                            pFixingSamplerState->fMipLodBias = v;
                        }
                        else if (strcmp(pKey, "ComparisonFunc") == 0)
                        {
                            if (strcmp(pValue, "Never") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_NEVER;
                            }
                            else if (strcmp(pValue, "Less") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_LESS;
                            }
                            else if (strcmp(pValue, "Equal") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_EQUAL;
                            }
                            else if (strcmp(pValue, "LessEqual") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_LESS_OR_EQUAL;
                            }
                            else if (strcmp(pValue, "Greater") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_GREATER;
                            }
                            else if (strcmp(pValue, "NotEqual") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_NOT_EQUAL;
                            }
                            else if (strcmp(pValue, "GreaterEqual") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_GREATER_OR_EQUAL;
                            }
                            else if (strcmp(pValue, "Always") == 0)
                            {
                                pFixingSamplerState->enuCompareFunc = SAMPLER_COMPARE_OP_ALWAYS;
                            }
                            else
                            {
                                ASSERT(0);
                            }
                        }
                    }
                    else
                    {
                        KGLogPrintf(KGLOG_ERR, "解析 %s 出错", line.c_str());
                        goto Exit0;
                    }
                }

                if (line.find("};") != std::string::npos)
                {
                    if (pFixingSamplerState->fToMinLod == 0.0f && pFixingSamplerState->fToMaxLod == 0.0f && pFixingSamplerState->fMipLodBias == 0.0f)
                    {
                        pFixingSamplerState->bEnableMipmap = false;
                    }
                    fixing              = NONE_SEGMENT_FIXING;
                    pFixingSamplerState = nullptr;
                }
                line.insert(0, "//");
                goto LoopEnd;
            }

            ///////////////////////////////////////处理各种单行绑定/////////////////////////////////////
            BOOL bRightBracket = false;
            BOOL bLeftBracket  = false;
            if (linesize > 5)
            {
                bRightBracket = (line.find(")") != std::string::npos);
                bLeftBracket  = (line.find("(") != std::string::npos);
            }
            static char fistBlockMatchChars[] = {'c', 'B', 'R', 'S', 'A', 'C', 'T', 'V'};
            if (!bInBlock && linesize > 5 &&
                (line.find(",") == std::string::npos) &&
                (bRightBracket == bLeftBracket) &&
                KGFX_SHADER_COMBINE_HELPER::IsFirstCharInTable(line.c_str(), fistBlockMatchChars, _countof(fistBlockMatchChars)) // 每行都匹配这么多查找，有点可怕，搞个首字母表，加速过滤一下吧
                && (line.find("cbuffer ") != std::string::npos ||                                                                                                                                                                         // 对应ubo
                    line.find("Buffer ") != std::string::npos ||                                                                                                                                                                          // 相当于readonly imagebuffer
                    line.find("RWBuffer ") != std::string::npos ||                                                                                                                                                                        // 相当于imagebuffer
                    line.find("RWStructuredBuffer ") != std::string::npos ||                                                                                                                                                              // 相当于可读写的ssbo
                    line.find("StructuredBuffer ") != std::string::npos ||                                                                                                                                                                // 相当于只读的ssbo
                    line.find("RWTexture1D ") != std::string::npos ||
                    line.find("RWTexture2D ") != std::string::npos ||
                    line.find("RWTexture3D ") != std::string::npos ||
                    line.find("RWTexture1DArray ") != std::string::npos ||
                    line.find("RWTexture2DArray ") != std::string::npos ||
                    line.find("RWTextureR64U ") != std::string::npos ||
                    line.find("TextureR64U ") != std::string::npos ||
                    line.find("VirtualTexturePageTable ") != std::string::npos ||
                    line.find("RWVirtualTexturePageTable ") != std::string::npos ||
                    line.find("AppendStructuredBuffer ") != std::string::npos ||  // 对应 StructuredBuffer
                    line.find("ConsumeStructuredBuffer ") != std::string::npos || // 对应 RWStructuredBuffer
                    line.find("RaytracingAccelerationStructure ") != std::string::npos ||
                    // line.find("SamplerState") != std::string::npos ||
                    line.find("Texture1D ") != std::string::npos ||
                    line.find("Texture2D ") != std::string::npos ||
                    line.find("Texture3D ") != std::string::npos ||
                    line.find("Texture1DArray ") != std::string::npos ||
                    line.find("Texture2DArray ") != std::string::npos ||
                    line.find("TextureCube ") != std::string::npos ||
                    line.find("TextureCubeArray ") != std::string::npos ||
                    line.find("Texture2DMS ") != std::string::npos ||
                    line.find("Texture2DMSArray ") != std::string::npos ||
                    line.find("cbuffer<") != std::string::npos ||
                    line.find("Buffer<") != std::string::npos ||
                    line.find("RWBuffer<") != std::string::npos ||
                    line.find("RWStructuredBuffer<") != std::string::npos ||
                    line.find("RWTexture1D<") != std::string::npos ||
                    line.find("RWTexture2D<") != std::string::npos ||
                    line.find("RWTexture3D<") != std::string::npos ||
                    line.find("RWTexture1DArray<") != std::string::npos ||
                    line.find("RWTexture2DArray<") != std::string::npos ||
                    line.find("StructuredBuffer<") != std::string::npos ||
                    line.find("AppendStructuredBuffer<") != std::string::npos ||
                    line.find("ConsumeStructuredBuffer<") != std::string::npos ||
                    // line.find("SamplerState") != std::string::npos ||
                    line.find("Texture1D<") != std::string::npos ||
                    line.find("Texture2D<") != std::string::npos ||
                    line.find("Texture3D<") != std::string::npos ||
                    line.find("Texture1DArray<") != std::string::npos ||
                    line.find("Texture2DArray<") != std::string::npos ||
                    line.find("TextureCube<") != std::string::npos ||
                    line.find("TextureCubeArray<") != std::string::npos ||
                    line.find("Texture2DMS<") != std::string::npos ||
                    line.find("Texture2DMSArray<") != std::string::npos)
                && line.find("\\") == std::string::npos
                )
                
            {
                size_t registorPos = line.find(":");
                if (registorPos != std::string::npos)
                {
                    line.erase(registorPos);
                }

                uint32_t beginPos = KGFX_SHADER_COMBINE_HELPER::GetLineBeginPos(line.c_str());


                strncpy(token, line.c_str(), TOKEN_SIZE);
                const char* pBLockName = KGFX_SHADER_COMBINE_HELPER::GetSecondToken(token);
                if (pBLockName && pBLockName[0])
                {
                    uint32_t bindid = 0;
                    auto     it     = m_mapBindingTable.find(pBLockName);
                    if (it == m_mapBindingTable.end())
                    {
                        bindid = m_uMaxBindingId++;
                        m_mapBindingTable.insert(std::make_pair<>(pBLockName, bindid));
                    }
                    else
                    {
                        bindid = it->second;
                    }
                    snprintf(prefix, TOKEN_SIZE, "[[vk::binding(%d,0)]] ", bindid);
                    line.insert(beginPos, prefix);
                }

                // snprintf(prefix, TOKEN_SIZE, "[[vk::binding(%d,0)]] ", uBindingId);
                // line.insert(beginPos, prefix);
                //++uBindingId;
            }
        }
    LoopEnd:
        fixedContent.append(line);
        fixedContent.append("\n");
        if (bDebugPrint)
        {
            printf("%s \r\n", line.c_str());
        }
    }

    pString->swap(fixedContent);
    bResult = true;
Exit0:
    return bResult;
}

uint32_t KGFX_CombinedShaderResultVK_HLSL::GetSamplerCount(gfx::ShaderStageType eShaderStage)
{
    uint32_t shaderid = GetGraphicAndComputeShaderId(eShaderStage);
    return (uint32_t)m_vecSamplerState[shaderid].size();
}

BOOL KGFX_CombinedShaderResultVK_HLSL::GetSamplerState(gfx::ShaderStageType eShaderStage, uint32_t i, gfx::KSamplerState** ppOutSamplerState, char* pOutName, uint32_t pOutNameSize)
{
    uint32_t shaderid = GetGraphicAndComputeShaderId(eShaderStage);
    if (i < m_vecSamplerState[shaderid].size())
    {
        _SamplerStateItem* pSamplerItem = m_vecSamplerState[shaderid][i];
        *ppOutSamplerState              = &pSamplerItem->samplerState;
        strncpy(pOutName, pSamplerItem->name.c_str(), pOutNameSize);
        return true;
    }
    else
    {
        return false;
    }
}


/////////////////////////////////////////////////////////////////////////////////////////

KGFX_CombinedShaderResultDx12::KGFX_CombinedShaderResultDx12()
{
}

KGFX_CombinedShaderResultDx12::~KGFX_CombinedShaderResultDx12()
{
}

BOOL KGFX_CombinedShaderResultDx12::Fix(const char* pcszShaderName, const char* pcszSrcShaderContent, gfx::ShaderStageType eShaderStage)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    std::string* pString = _GetResultString(eShaderStage);
    *pString             = pcszSrcShaderContent;

    bResult = true;
    return bResult;
}

uint32_t KGFX_CombinedShaderResultDx12::GetSamplerCount(gfx::ShaderStageType eShaderStage)
{
    return 0;
}
BOOL KGFX_CombinedShaderResultDx12::GetSamplerState(gfx::ShaderStageType eShaderStage, uint32_t i, gfx::KSamplerState** ppOutSamplerState, char* pOutName, uint32_t pOutNameSize)
{
    return false;
}
