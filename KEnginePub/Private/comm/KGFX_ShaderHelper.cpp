#include "KGFX_ShaderHelper.h"
#include "KBase/Public/str/KStrHelper.h"
#include "Engine/KGLog.h"
#include "KBase/Public/io/KFile.h"
#include <sstream>
#include "../IGFX_Private.h"
#include "rapidjson/document.h"
#include "KGFX_ShaderCombinedResult.h"
#include "KBase/Public/memory/KBufferReader.h"
#include "Common/KG_Memory.h"
#include "KGBaseDef/Public/KGMacro.h"

////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Private/dx12/KGFX_DXCComplieDx12.h"
#include "KMaterialSystem/Public/IKMaterialTypes.h"



namespace IncludeFileHelper
{
    void _TrimAWord(const char* pString, char* pOut)
    {
        uint32_t i      = 0;
        uint32_t j      = 0;
        BOOL     bBegan = false;
        pOut[0]         = '\0';
        for (;;)
        {
            if (pString[i] != ' ' && pString[i] != '\r' && pString[i] != '\n')
            {
                bBegan  = true;
                pOut[j] = pString[i];
                j++;
            }

            ++i;

            if (bBegan && (pString[i] == ' ' || pString[i] == '\r' || pString[i] == '\n'))
            {
                pOut[j] = '\0';
                break;
            }

            if (pString[i] == '\0')
            {
                pOut[j] = '\0';
                break;
            }
        }
    }

    BOOL _ReadFromTo(const char* pString, char cBegin, char cEnd, char* pOut)
    {
        uint32_t i      = 0;
        uint32_t j      = 0;
        BOOL     bBegin = false;
        pOut[0]         = '\0';
        BOOL readed     = false;
        for (;;)
        {
            if (pString[i] == cBegin)
            {
                bBegin = true;
                ++i;
            }
            if (bBegin)
            {
                pOut[j] = pString[i];
                ++j;
            }
            ++i;
            if ((bBegin && pString[i] == cEnd) || pString[i] == '\0')
            {
                readed  = true;
                pOut[j] = '\0';
                break;
            }
        }
        return readed;
    }

    BOOL _ReadFromTo2(const char* pString, char cBegin, char cEnd0, char cEnd1, char* pOut)
    {
        uint32_t i      = 0;
        uint32_t j      = 0;
        BOOL     bBegin = false;
        pOut[0]         = '\0';
        BOOL readed     = false;
        for (;;)
        {
            if (pString[i] == cBegin)
            {
                bBegin = true;
                ++i;
            }
            if (bBegin)
            {
                pOut[j] = pString[i];
                ++j;
            }
            ++i;
            if ((bBegin && (pString[i] == cEnd0 || pString[i] == cEnd1)) || pString[i] == '\0')
            {
                readed  = true;
                pOut[j] = '\0';
                break;
            }
        }
        return readed;
    }

    const char* _GetShaderTypeName(gfx::ShaderStageType eShaderStage)
    {
        const char* szShaderTypeName = nullptr;

        switch (eShaderStage)
        {
        case gfx::ShaderStageType::Vertex:
            szShaderTypeName = "vs";
            break;
        case gfx::ShaderStageType::Fragment:
            szShaderTypeName = "fs";
            break;
        case gfx::ShaderStageType::Compute:
            szShaderTypeName = "cs";
            break;
        case gfx::ShaderStageType::Geometry:
            szShaderTypeName = "gs";
            break;
        case gfx::ShaderStageType::Hull:
            szShaderTypeName = "tc";
            break;
        case gfx::ShaderStageType::Domain:
            szShaderTypeName = "te";
            break;
        }
        ASSERT(szShaderTypeName);
        return szShaderTypeName;
    }

    const char* GetShaderTypeName(gfx::ShaderStageType eShaderStage)
    {
        const auto& items = gfx::ShaderStageType_info::items();

        auto it = std::find_if(items.begin(), items.end(), [eShaderStage](const auto& item) {
            return item.first == eShaderStage;
        });

        if (eShaderStage == gfx::ShaderStageType::Fragment)
        {
            return "fs";
        }
        return it->second.c_str();
    }

    void _ReadBracket(const char* pString, char* pOut)
    {
        uint32_t i      = 0;
        uint32_t j      = 0;
        BOOL     bBegan = false;
        pOut[0]         = '\0';
        for (;;)
        {
            if (pString[i] == '[')
            {
                bBegan = true;
            }

            if (bBegan)
            {
                pOut[j] = pString[i];

                if (pString[i] == ']')
                {
                    pOut[j + 1] = '\0';
                    break;
                }
                ++j;
            }

            ++i;

            if (pString[i] == '\0')
            {
                pOut[j] = '\0';
                break;
            }
        }
    }

    void _TrimAWordTo(const char* pString, char pToEnd, char* pOut)
    {
        uint32_t i      = 0;
        uint32_t j      = 0;
        BOOL     bBegan = false;
        pOut[0]         = '\0';
        for (;;)
        {
            if (pString[i] != ' ' && pString[i] != '\r' && pString[i] != '\n')
            {
                bBegan  = true;
                pOut[j] = pString[i];
                j++;
            }

            ++i;

            if (bBegan && (pString[i] == ' ' || pString[i] == '\r' || pString[i] == '\n'))
            {
                pOut[j] = '\0';
                break;
            }

            if (pString[i] == '\0' || pString[i] == pToEnd)
            {
                pOut[j] = '\0';
                break;
            }
        }
    }

    BOOL _IsCurCommentLine(const char* szline)
    {
        uint32_t i               = 0;
        BOOL     bIsCommanetLine = false;
        BOOL     bBegan          = false;
        for (;;)
        {
            if (!bBegan && szline[i] == '/' && szline[i + 1] == '/')
            {
                bIsCommanetLine = true;
                break;
            }

            if (szline[i] != ' ')
            {
                bBegan = true;
                break;
            }

            if (szline[i] == '\0')
            {
                break;
            }
            ++i;
        }
        return bIsCommanetLine;
    }

    BOOL _IsDigitString(const char* p)
    {
        while (*p != '\0')
        {
            if (!isdigit(*p++))
            {
                return false;
            }
        }
        return true;
    }


    int _GetSMMaterialID(const char* s)
    {
        static std::map<std::string, int> _MaterialValueMap =
            {
                {"MATERIALID_UNLIT",             0 },
                {"MATERIALID_STANDARD",          1 },
                {"MATERIALID_ANISO",             2 },
                {"MATERIALID_TRANSLUCENCY",      3 },
                {"MATERIALID_SUBSURFACE",        4 },
                {"MATERIALID_SKIN",              5 },
                {"MATERIALID_SKIN_FORWARD",      6 },
                {"MATERIALID_CLOTH",             7 },
                {"MATERIALID_CLEARCOAT",         8 },
                {"MATERIALID_FUR",               9 },
                {"MATERIALID_EYE",               10},
                {"MATERIALID_HAIR",              11},
                {"MATERIALID_SKIN_UE4",          12},
                {"MATERIALID_HAIR_UE4",          13},
                {"MATERIALID_CLOTH_FORWARD",     14},
                {"MATERIALID_NODIRECTION",       15},
                {"MATERIALID_TWO_SIDED_FOLIAGE", 16},
                {"MATERIALID_STREE",             17},
                {"MATERIALID_CLOTH_SUBSURFACE",  18},
                {"MATERIALID_HAIR_SUBSURFACE",   19},
                {"MATERIALID_TERRAIN",           20},
                {"MATERIALID_SSSLUT",            21},
                {"MATERIALID_TRESSFX",           22},
                {"MATERIALID_WATER",             23},
                {"MATERIALID_LAMBERT",           32},
                {"MATERIALID_CLOTH_GGX",         33},
                {"MATERIALID_NUM",               34},
        };


        auto iter = _MaterialValueMap.find(s);

        if (iter != _MaterialValueMap.end())
        {
            return iter->second;
        }
        else
        {
            KASSERT(0);
        }

        return 0;
    }

    int _GetReflectionID(const char* s)
    {
        static std::map<std::string, int> _MaterialValueMap =
            {
                {"REFLECTION_REFLECT",  0},
                {"REFLECTION_SPECULAR", 1},
                {"REFLECTION_METALLIC", 2},
                {"REFLECTION_UNLIT",    3},
        };


        auto iter = _MaterialValueMap.find(s);

        if (iter != _MaterialValueMap.end())
        {
            return iter->second;
        }
        else
        {
            KASSERT(0);
        }

        return 0;
    }

    void _ExpandMacoDefine(const char* szMacroDefine, std::string& szOutString)
    {
        if (szMacroDefine && szMacroDefine[0])
        {
            uint32_t len    = (uint32_t)strlen(szMacroDefine);
            char*    buffer = new char[len + 1];
            buffer[len]     = '\0';
            strncpy(buffer, szMacroDefine, len);
            std::vector<std::string> items;
            KSTR_HELPER::StrSplit(buffer, ";", items);
            for (auto& it : items)
            {
                std::string& item = it;
                KSTR_HELPER::replace_all(item, "=", " ");
                szOutString.append("#define ");
                szOutString.append(item);
                szOutString.append("\n");
            }
            SAFE_DELETE_ARRAY(buffer);
        }
    }

    void ExpandMacoDefineDXC(const char* szMacroDefine, std::unordered_map<std::string, std::string>& szOutString)
    {
        if (szMacroDefine && szMacroDefine[0])
        {
            uint32_t len    = (uint32_t)strlen(szMacroDefine);
            char*    buffer = new char[len + 1];
            buffer[len]     = '\0';
            strncpy(buffer, szMacroDefine, len);
            std::vector<std::string> items;
            KSTR_HELPER::StrSplit(buffer, ";", items);
            for (auto& it : items)
            {
                std::string& item = it;

                std::vector<std::string> keyAndValue = {};
                KSTR_HELPER::StrSplit(item.data(), "=", keyAndValue);
                assert(keyAndValue.size() < 3);
                std::string key   = keyAndValue.at(0);
                std::string value = keyAndValue.size() > 1 ? keyAndValue.at(1) : "";

                auto res = szOutString.insert({key, value});
                /// 同一个宏声明了两次，这是不允许的，请检查自己的shader
                assert(res.second);
            }
            SAFE_DELETE_ARRAY(buffer);
        }
    }

    BOOL   ReadUserShaderMtlId(NSKBase::tagFileLocation& sUserShaderFileLoc,  int32_t& nMaterialID, int32_t& nReflectionID, char& cVaryingMask)
    {
        BOOL bRet = false;
        char szMBUserShader[MAX_PATH] = "";
        NSKBase::KBufferReader* piFileReader = nullptr;
        g_StrCpyLen(szMBUserShader, sUserShaderFileLoc.GetFilePath().Str(), countof(szMBUserShader));

        piFileReader = sUserShaderFileLoc.CreateFileReader();
        KGLOG_PROCESS_ERROR(piFileReader);
        
        {
            std::string strFileContent;
            strFileContent.resize(piFileReader->GetSize());
            piFileReader->Read((char*)strFileContent.data(), piFileReader->GetSize());

            std::string       strLine;
            std::stringstream ss(strFileContent);
            while (std::getline(ss, strLine))
            {
                std::replace(strLine.begin(), strLine.end(), '\r', '\n');
                if (!IncludeFileHelper::_IsCurCommentLine(strLine.c_str()))
                {
                    if (strstr(strLine.c_str(), "SM_Reflection"))
                    {
                        char name[64], value[32];
                        name[0] = value[0] = 0;
                        sscanf(strLine.c_str(), "%*s %s %[^\n]", name, value);
                        char* p = value;
                        if (!IncludeFileHelper::_IsDigitString(value))
                        {
                            nReflectionID = IncludeFileHelper::_GetReflectionID(value);
                        }
                        else
                        {
                            nReflectionID = atoi(value);
                        }
                    }

                    if (strstr(strLine.c_str(), "SM_MaterialID"))
                    {
                        char name[64], value[32];
                        name[0] = value[0] = 0;
                        sscanf(strLine.c_str(), "%*s %s %[^\n]", name, value);
                        char* p = value;
                        if (!IncludeFileHelper::_IsDigitString(value))
                        {
                            nMaterialID = IncludeFileHelper::_GetSMMaterialID(value);
                        }
                        else
                        {
                            // m_nMaterialID = atoi(value);
                            nMaterialID = atoi(value);
                        }
                        break;
                    }
                }
            }

            size_t offset = strFileContent.find("CalculateMainPixelNode(");
            if (offset != std::string::npos)
            {
                if (strFileContent.find(".VertexNormal", offset) != std::string::npos)
                {
                    cVaryingMask |= (char)Varyings::V2F_NORMAL;
                }

                if (strFileContent.find(".VertexPosition", offset) != std::string::npos)
                {
                    cVaryingMask |= (char)Varyings::V2F_POS;
                }
                while ((offset = strFileContent.find("PARAMTER_TEXCOORD(Parameters,", offset)) != std::string::npos)
                {
                    offset += 29;
                    size_t      texEnd = strFileContent.find(")", offset);
                    std::string number = strFileContent.substr(offset, texEnd - offset);
                    int         uvIndex = std::stoi(strFileContent.substr(offset, texEnd - offset));
                    assert(uvIndex <= 6);

                    if (uvIndex >= 1)
                    {
                        cVaryingMask |= 1 << (uvIndex + 1);
                    }
                    offset = texEnd;
                }
            }
        }
        bRet = true;
    Exit0:
        SAFE_RELEASE(piFileReader);
        return bRet;
    }

    BOOL ReadWholeShaderFile(const NSKBase::tagFileLocation& sShaderLoc, std::string& szWholeFileString, std::vector<std::string>* pVecReadedFile)
    {
        BOOL        bResult     = false;
        uint64_t    fileLen     = 0;
        uint32_t    uBegin      = 0;
        uint8_t     header[3]   = {0};
        BOOL        bUtf8Bom    = false;
        char        szName[128] = "";
        std::string line;

        NSKBase::KBufferReader* pFileReader = nullptr;
        KGLOG_PROCESS_ERROR(sShaderLoc.IsValid());

        pFileReader = sShaderLoc.CreateFileReader();
        KGLOG_PROCESS_ERROR(pFileReader);

        {
            std::string strShaderFilePath = sShaderLoc.GetFilePath().Str();
            if (pVecReadedFile && std::find(pVecReadedFile->begin(), pVecReadedFile->end(), strShaderFilePath) == pVecReadedFile->end())
            {
                pVecReadedFile->push_back(strShaderFilePath);
            }

            pFileReader->Read(header, 3);
            if (header[0] == 0xef &&
                header[1] == 0xbb &&
                header[2] == 0xbf)
            {
                bUtf8Bom = true;
            }

            // 读取整个文件，并跳过bom头
            if (bUtf8Bom)
            {
                fileLen = pFileReader->GetSize() - 3;
                szWholeFileString.resize(fileLen);
                pFileReader->Seek(3, SEEK_SET);
                pFileReader->Read(&szWholeFileString[0], fileLen);
            }
            else
            {
                fileLen = pFileReader->GetSize();
                szWholeFileString.resize(fileLen);
                pFileReader->Seek(0, SEEK_SET);
                pFileReader->Read(&szWholeFileString[0], fileLen);
            }

            KSTR_HELPER::ReplaceStr(szWholeFileString, "\r\n", "\n", INT_MAX);
        }

        bResult = true;
    Exit0:
        SAFE_RELEASE(pFileReader);
        return bResult;
    }

    BOOL _ReadWholeShaderFile(const NSKBase::tagFileLocation& sShaderLoc, std::string& szWholeFileString)
    {
        return ReadWholeShaderFile(sShaderLoc, szWholeFileString, nullptr);
    }

    size_t _findSectionPos(const std::string& szWholeFileString, const char* sectionName, size_t findPos)
    {
        size_t _nextfindPos = szWholeFileString.find(sectionName, findPos);
        if (_nextfindPos != std::string::npos && _nextfindPos > 0 && szWholeFileString[_nextfindPos - 1] == ' ')
        {
            // 如果是#include "xxx [section]"，这种前面是空格，那么要排除掉
            _nextfindPos = szWholeFileString.find(sectionName, _nextfindPos + 1);
            _findSectionPos(szWholeFileString, sectionName, _nextfindPos);
        }
        return _nextfindPos;
    }

    std::string _simplifyPath(const std::string& path)
    {
        std::vector<std::string> components;
        std::string              component;
        std::istringstream       stream(path);

        // 使用 stringstream 按 '/' 分割路径
        while (std::getline(stream, component, '/'))
        {
            if (component == "" || component == ".")
            {
                // 忽略空字符串和当前目录符号
                continue;
            }
            else if (component == "..")
            {
                // 回退到上一级目录
                if (!components.empty())
                {
                    components.pop_back();
                }
            }
            else
            {
                // 添加有效的路径组件
                components.push_back(component);
            }
        }

        // 构建简化后的路径
        std::string simplifiedPath;
        for (size_t i = 0; i < components.size(); ++i)
        {
            simplifiedPath += components[i];
            if (i < components.size() - 1)
            {
                simplifiedPath += "/";
            }
        }

        return simplifiedPath;
    }


    BOOL IsHlslInnerSection(const char* pStr)
    {
        if (
            strstr(pStr, "[numthreads") ||
            strstr(pStr, "[earlydepthstencil") ||
            strstr(pStr, "[noinline") ||
            strstr(pStr, "[maxvertexcount") ||
            strstr(pStr, "[domain") ||
            strstr(pStr, "[partitioning") ||
            strstr(pStr, "[outputtopology") ||
            strstr(pStr, "[outputcontrolpoints") ||
            strstr(pStr, "[patchconstantfunc") ||
            strstr(pStr, "[maxtessfactor") ||
            strstr(pStr, "[branch") ||
            strstr(pStr, "[flatten") ||
            strstr(pStr, "[unroll") ||
            strstr(pStr, "[target") ||
            strstr(pStr, "[RootSignature") ||
            strstr(pStr, "[ResourceBinding") ||
            strstr(pStr, "[allow_uav_condition") ||
            strstr(pStr, "[fastopt") ||
            strstr(pStr, "[precise") ||
            strstr(pStr, "[maxrootparameters")
        )
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    void AppendInclude(
        const NSKBase::tagFileLocation& sWholeFileStringFrom,
        const std::string&              szWholeFileString,
        const std::string&              szIncludeString,
        std::string&                    szContent,
        const char*                     pcszCurrentPath,
        std::vector<std::string>&       vecReadedFile,
        std::set<std::string>&          setIncluded,
        std::list<std::string>&  includeStack
    )
    {
        char file[MAX_PATH];
        file[0]        = '\0';
        const char* pp = strrchr(szIncludeString.c_str(), '/');
        if (pp)
        {
            IncludeFileHelper::_ReadFromTo(pp, '/', '\"', file);
        }
        else
        {
            IncludeFileHelper::_ReadFromTo(szIncludeString.c_str(), '\"', '\"', file);
        }
        if (szIncludeString.find(".inc") == std::string::npos)
        {
            if (setIncluded.find(file) == setIncluded.end())
            {
                setIncluded.insert(file);
            }
            else
            {
                return;
            }
        }
        BOOL bRetCode = false;
        char szSectionName[128];
        szSectionName[0] = '\0';
        _ReadBracket(szIncludeString.c_str(), szSectionName);

        if (szIncludeString.find("self ") != std::string::npos)
        {
            char txt[1024];
            snprintf(txt, 1024, "/*****%s %s*****/\n", sWholeFileStringFrom.GetFilePath().Str(), szSectionName);
            szContent.append(txt);

            size_t      findPos = _findSectionPos(szWholeFileString, szSectionName, 0);
            std::string line;
            if (findPos != std::string::npos)
            {
                std::stringstream ss(szWholeFileString);
                ss.seekg(findPos);
                std::getline(ss, line);
                while (std::getline(ss, line))
                {
                    // 碰到普通的section[xxx]或者@xxx，停止读取，但是不能是[[]]这种形式，因为[[vk::xxx]]这种是合法的语法结构
                    //            if (line.size() >= 2
                    //                && ((line[0] == '[' && line[1] != '[') || line[0] == '@')
                    //                && strstr(line.c_str(), "[numthreads") == nullptr  //hlsl cs的线程组说明
                    //                )
                    //{
                    //	break;
                    //}
                    BOOL bIsCommentLine = _IsCurCommentLine(line.c_str());

                    size_t findPos1 = line.find("#include");
                    if (!bIsCommentLine && findPos1 != std::string::npos)
                    {
                        if (line.find(USER_SHADER_NAME) != std::string::npos)
                        {
                            char file[MAX_PATH];
                            file[0] = '\0';
                            IncludeFileHelper::_ReadFromTo(line.c_str(), '\"', '\"', file);

                            if (setIncluded.find(file) == setIncluded.end())
                            {
                                // 如果是usershaderline，那么不去做include，保持这行直接添加上去，之后再去fix
                                szContent.append(line);
                                szContent.append("\n");
                                setIncluded.insert(file);
                            }
                        }
                        else
                        {
                            IncludeFileHelper::AppendInclude(sWholeFileStringFrom, szWholeFileString, line, szContent, pcszCurrentPath, vecReadedFile, setIncluded, includeStack);
                        }
                    }
                    else
                    {
                        szContent.append(line);
                        szContent.append("\n");
                    }
                }
            }
            else
            {
                KGLogPrintf(KGLOG_ERR, "%s 找不到 %s", sWholeFileStringFrom.GetFilePath().Str(), szSectionName);
                ASSERT(0);
            }
        }
        else
        {
            char szFileName[128];
            szFileName[0] = '\0';
            _ReadFromTo2(szIncludeString.c_str(), '\"', '\"', ' ', szFileName);
            if (szFileName[0])
            {
                std::string _strFullpath = pcszCurrentPath;
                _strFullpath.append("/");
                _strFullpath.append(szFileName);
                std::string strFullpath = _simplifyPath(_strFullpath);
                // enginedata\material\shader_dx12\include
                //  include 文件递归迭代时，include文件中include的文件，应该以上一层include文件所在目录为参考 —— by huafei
                char _szNewCurrentPath[260];
                _szNewCurrentPath[0] = '\0';
                KSTR_HELPER::SplitPath(strFullpath.c_str(), _szNewCurrentPath, nullptr);


                if (!KGFExist(strFullpath.c_str()))
                {
                    strFullpath = DrvOption::IsDeferRender() ?  "enginedata/material/shader_dx12/include/" : "enginedata/material/shader_mb/include/";
                    strFullpath.append(szFileName);
                    KSTR_HELPER::SplitPath(strFullpath.c_str(), _szNewCurrentPath, nullptr);
                }

                KUniqueStr ustrFullPath = g_CachePathString(strFullpath.c_str(), TRUE);

                NSKBase::tagFileLocation sShaderLoc(ustrFullPath);
                std::string              _szWholeFileString;
                bRetCode = ReadWholeShaderFile(sShaderLoc, _szWholeFileString, &vecReadedFile);
                if (bRetCode)
                {
                    std::string strLine;
                    size_t      stSectionFindPos = 0;
                    if (szSectionName && szSectionName[0])
                    {
                        stSectionFindPos = _findSectionPos(_szWholeFileString, szSectionName, 0);
                    }
                    if (szSectionName && szSectionName[0])
                    {
                        char txt[1024];
                        snprintf(txt, 1024, "/*****%s %s*****/\n", strFullpath.c_str(), szSectionName);
                        szContent.append(txt);
                    }
                    else
                    {
                        includeStack.push_front(strFullpath.c_str());
                        for(auto it : includeStack)
                        {
                            char txt[1024];
                            snprintf(txt, 1024, "/////include:%s\n", it.c_str());
                            szContent.append(txt);
                        }
                    }

                    if (stSectionFindPos != std::string::npos)
                    {
                        std::stringstream ss(_szWholeFileString);
                        ss.seekg(stSectionFindPos);
                        if (szSectionName && szSectionName[0])
                        {
                            std::getline(ss, strLine);
                        }

                        while (std::getline(ss, strLine))
                        {
                            // if (
                            //                      strLine.size() >= 2
                            //                      && ((strLine[0] == '[' && strLine[1] != '[') || strLine[0] == '@')
                            //                      && strstr(strLine.c_str(), "[numthreads") == nullptr  //hlsl cs 的线程组说明
                            //                      )
                            //{
                            //	break;
                            //}
                            BOOL   bIsCommentLine = _IsCurCommentLine(strLine.c_str());
                            size_t findPos1       = strLine.find("#include");
                            if (!bIsCommentLine && findPos1 != std::string::npos)
                            {
                                if (strLine.find(USER_SHADER_NAME) != std::string::npos)
                                {
                                    char file[MAX_PATH];
                                    file[0] = '\0';
                                    IncludeFileHelper::_ReadFromTo(strLine.c_str(), '\"', '\"', file);

                                    if (setIncluded.find(file) == setIncluded.end())
                                    {
                                        // 如果是usershaderline，那么不去做include，保持这行直接添加上去，之后再去fix
                                        szContent.append(strLine);
                                        szContent.append("\n");
                                        setIncluded.insert(file);
                                    }
                                }
                                else
                                {
                                    IncludeFileHelper::AppendInclude(sShaderLoc, _szWholeFileString, strLine, szContent, _szNewCurrentPath, vecReadedFile, setIncluded, includeStack);
                                }
                            }
                            else
                            {
                                szContent.append(strLine);
                                szContent.append("\n");
                            }                            
                        }
                        includeStack.pop_front();
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }
            }
        }
    }
}; // namespace IncludeFileHelper

gfx::KGFX_ShaderFile::KGFX_ShaderFile()
{
}

gfx::KGFX_ShaderFile::~KGFX_ShaderFile()
{
}

BOOL gfx::KGFX_ShaderFile::LoadFileFromWholeFileString(
    const std::string&              szWholeFileString,
    const NSKBase::tagFileLocation& sShaderLoc,
    const char*                     szSectionName
)
{
    m_setAddedInclude.clear();
    BOOL        bResult = false;
    std::string line;
    char        path[260];
    path[0] = '\0';
    char fileName[64];
    fileName[0] = '\0';

    const char* pcszShaderSource = sShaderLoc.GetFilePath().Str();
    ASSERT(pcszShaderSource && pcszShaderSource[0]);
    KSTR_HELPER::SplitPath(pcszShaderSource, path, fileName);

    size_t findPos = 0;
    if (szSectionName && szSectionName[0])
    {
        findPos = IncludeFileHelper::_findSectionPos(szWholeFileString, szSectionName, 0);
        char txt[1024];
        snprintf(txt, 1024, "/*****%s [%s]*****/\n", pcszShaderSource, szSectionName);
        m_szContent.append(txt);
    }
    else
    {
        char txt[1024];
        snprintf(txt, 1024, "/*****%s*****/\n", pcszShaderSource);
        m_szContent.append(txt);
    }
    if (findPos != std::string::npos)
    {
        std::stringstream ss(szWholeFileString);
        ss.seekg(findPos);

        if (szSectionName && szSectionName[0])
        {
            std::getline(ss, line);
        }
        std::list<std::string> includeStack;
        while (std::getline(ss, line))
        {
            // if (
            //             line.size() >=2
            //             && ((line[0] == '[' && line[1] != '[') || line[0] == '@')
            //             && strstr(line.c_str(), "[numthreads") == nullptr  //hlsl cs的线程组说明
            //             )
            //{
            //	break;
            //}
            BOOL   bIsCommentLine = IncludeFileHelper::_IsCurCommentLine(line.c_str());
            size_t _Pos           = line.find("#include");
            if (!bIsCommentLine && _Pos != std::string::npos)
            {
                if (line.find(USER_SHADER_NAME) != std::string::npos)
                {
                    char file[MAX_PATH];
                    file[0] = '\0';
                    IncludeFileHelper::_ReadFromTo(line.c_str(), '\"', '\"', file);

                    if (m_setAddedInclude.find(file) == m_setAddedInclude.end())
                    {
                        // 如果是usershaderline，那么不去做include，保持这行直接添加上去，之后再去fix
                        m_szContent.append(line);
                        m_szContent.append("\n");
                        m_setAddedInclude.insert(file);
                    }
                }
                else
                {
                    IncludeFileHelper::AppendInclude(sShaderLoc, szWholeFileString, line, m_szContent, path, m_vecReadedFileList, m_setAddedInclude, includeStack);
                }
            }
            else
            {
                m_szContent.append(line);
                m_szContent.append("\n");
            }
        }
    }

    bResult = true;
    return bResult;
}

BOOL gfx::KGFX_ShaderFile::LoadFile(const NSKBase::tagFileLocation& sShaderLoc, const char* szSectionName)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    m_sShaderLoc = sShaderLoc;
    if (szSectionName && szSectionName[0])
    {
        m_szSectionName = szSectionName;
    }

    std::string szWholeFileString;
    bRetCode = IncludeFileHelper::ReadWholeShaderFile(m_sShaderLoc, szWholeFileString, &m_vecReadedFileList);
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = LoadFileFromWholeFileString(szWholeFileString, sShaderLoc, szSectionName);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = true;
Exit0:
    return bResult;
}

bool gfx::KGFX_ShaderFile::OnlyLoadFile(const NSKBase::tagFileLocation& sShaderLoc)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    m_sShaderLoc = sShaderLoc;

    std::string szWholeFileString;
    bRetCode = IncludeFileHelper::ReadWholeShaderFile(m_sShaderLoc, szWholeFileString, &m_vecReadedFileList);
    KGLOG_PROCESS_ERROR(bRetCode);

    m_szContent = szWholeFileString;
    bResult     = true;
Exit0:
    return bResult;
}

const char* gfx::KGFX_ShaderFile::GetFileContent()
{
    return m_szContent.c_str();
}

size_t gfx::KGFX_ShaderFile::GetFileContentSize()
{
    return m_szContent.size();
}

int32_t gfx::KGFX_ShaderFile::AddRef()
{
    return ++m_nRef;
}

int32_t gfx::KGFX_ShaderFile::GetRef()
{
    return m_nRef;
}

int32_t gfx::KGFX_ShaderFile::Release()
{
    int32_t nRef = --m_nRef;
    if (nRef == 0)
    {
        gfx::KGFX_GetShaderFilePool()->RemoveShaderFile(m_szKey.c_str());
    }
    return nRef;
}


const char* gfx::KGFX_ShaderFile::GetKey()
{
    return m_szKey.c_str();
}

void gfx::KGFX_ShaderFile::SetKey(const char* pKey)
{
    m_szKey = pKey;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

gfx::KGFX_ShaderTechItem::KGFX_ShaderTechItem()
{
    m_pMainShaderFile = nullptr;
    m_pUserShaderFile = nullptr;
    m_szEntryPoint    = "main"; // 默认入口
}

gfx::KGFX_ShaderTechItem::~KGFX_ShaderTechItem()
{
    SAFE_RELEASE(m_pMainShaderFile);
    SAFE_RELEASE(m_pUserShaderFile);
}

BOOL gfx::KGFX_ShaderTechItem::LoadFileFromWholeFileStringNoFix(
    gfx::ShaderStageType            eShaderStage,
    const char*                     szShaderSource,
    const std::string&              szWholeFileString,
    const char*                     szShaderTechName,
    const char*                     szShaderTypeName,
    const NSKBase::tagFileLocation& sUserShaderLoc
)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    m_eShageStage = eShaderStage;

    gfx::KGFX_ShaderFilePool* pShaderFilePool    = gfx::KGFX_GetShaderFilePool();
    char                      szShaderName[1024] = "";
    char                      szhaderDefName[128];
    snprintf(szhaderDefName, 128, "@@%s", szShaderTechName);

    BOOL bJson = false;
    {
        bJson = true;
        // 尝试用json去解析
        rapidjson::Document JsonDocument;
        JsonDocument.Parse(szWholeFileString.c_str());
        KGLOG_ASSERT_EXIT(!JsonDocument.HasParseError());
        auto& ParamObjectArray = JsonDocument["Tech"];
        ASSERT(ParamObjectArray.IsArray());
        BOOL bFind = false;
        for (auto it = ParamObjectArray.Begin(), iend = ParamObjectArray.End(); it != iend; ++it)
        {
            ASSERT(it->IsObject());
            auto        ParamObject = it->GetObject();
            const char* pTechName   = ParamObject["Name"].GetString();
            if (strcmp(pTechName, szShaderTechName) == 0)
            {
                auto& programArray = ParamObject["Program"];
                ASSERT(programArray.IsArray());
                for (auto itt = programArray.Begin(), end = programArray.End(); itt != end; ++itt)
                {
                    ASSERT(itt->IsObject());
                    auto        programItem = itt->GetObject();
                    const char* pShaderType = programItem["ShaderType"].GetString();

                    if (strcmp(pShaderType, szShaderTypeName) == 0)
                    {
                        const char* pEntrypoint  = programItem["Entrypoint"].GetString();
                        const char* pMacroDefine = programItem["MacroDefine"].GetString();
                        const char* pPath        = programItem["Path"].GetString();
                        IncludeFileHelper::ExpandMacoDefineDXC(pMacroDefine, m_szTechMacroDXC);
                        strncpy(szShaderName, pPath, 1024);
                        m_szEntryPoint = pEntrypoint;
                        bFind          = true;
                        break;
                    }
                }
                break;
            }
        }
        if (!bFind)
        {
            KGLogPrintf(KGLOG_ERR, "%s 找不到tech: %s", szShaderSource, szShaderTechName);
        }
    }

    KGLOG_PROCESS_ERROR(szShaderName[0]);

    if (bJson)
    {
        KUniqueStr               ustrShaderPath = g_CachePathString(szShaderName, TRUE);
        NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
        SAFE_RELEASE(m_pMainShaderFile);
        m_pMainShaderFile = pShaderFilePool->OnlyLoadShaderFile(sShaderLoc, nullptr);
        KGLOG_PROCESS_ERROR(m_pMainShaderFile);
    }

    bResult = true;
Exit0:
    return bResult;
}

BOOL gfx::KGFX_ShaderTechItem::LoadFileFromWholeFileStringNoFix2(const char* szShaderName, const char* pMacroDefine)
{
    BOOL bResult = false;

    IncludeFileHelper::ExpandMacoDefineDXC(pMacroDefine, m_szTechMacroDXC);
    gfx::KGFX_ShaderFilePool* pShaderFilePool = gfx::KGFX_GetShaderFilePool();
    KUniqueStr                ustrShaderPath  = g_CachePathString(szShaderName, TRUE);
    NSKBase::tagFileLocation  sShaderLoc(ustrShaderPath);
    SAFE_RELEASE(m_pMainShaderFile);
    m_pMainShaderFile = pShaderFilePool->OnlyLoadShaderFile(sShaderLoc, nullptr);
    KGLOG_PROCESS_ERROR(m_pMainShaderFile);

    bResult = true;
Exit0:
    return bResult;
}

BOOL gfx::KGFX_ShaderTechItem::LoadFileFromWholeFileString(
    gfx::ShaderStageType            eShaderStage,
    const char*                     szShaderSource,
    const std::string&              szWholeFileString,
    const char*                     szShaderTechName,
    const char*                     szShaderTypeName,
    const NSKBase::tagFileLocation& sUserShaderLoc
)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    m_eShageStage = eShaderStage;

    gfx::KGFX_ShaderFilePool* pShaderFilePool    = gfx::KGFX_GetShaderFilePool();
    gfx::KGFX_ShaderTechItem* pShaderDefPassItem = nullptr;
    char                      szShaderName[1024] = "";
    char                      szhaderDefName[128];
    snprintf(szhaderDefName, 128, "@@%s", szShaderTechName);

    size_t findPos = szWholeFileString.find(szhaderDefName);
    BOOL   bJson   = false;
    if (findPos != std::string::npos)
    {
        char techName[32];
        techName[0] = '\0';
        snprintf(techName, 32, "@%s", szShaderTypeName);

        std::string       line;
        std::stringstream ss(szWholeFileString);
        ss.seekg(findPos);
        std::getline(ss, line);
        while (std::getline(ss, line))
        {
            if (line.find("@@") != std::string::npos)
            {
                break;
            }

            if (IncludeFileHelper::_IsCurCommentLine(line.c_str()))
            {
                continue;
            }

            if (line.find("#define ") != std::string::npos)
            {
                m_szTechMacro.append(line);
                m_szTechMacro.append("\n");
                continue;
            }

            size_t findTechPos = line.find(techName);
            if (findTechPos != std::string::npos)
            {
                findTechPos = line.find("=", findTechPos);
                if (findTechPos != std::string::npos)
                {
                    IncludeFileHelper::_TrimAWord(line.c_str() + findTechPos + 1, szShaderName);
                    break;
                }
            }
        }
    }
    else
    {
        bJson = true;
        // 尝试用json去解析
        rapidjson::Document JsonDocument;
        JsonDocument.Parse(szWholeFileString.c_str());
        KGLOG_ASSERT_EXIT(!JsonDocument.HasParseError());
        auto& ParamObjectArray = JsonDocument["Tech"];
        ASSERT(ParamObjectArray.IsArray());
        BOOL bFind = false;
        for (auto it = ParamObjectArray.Begin(), iend = ParamObjectArray.End(); it != iend; ++it)
        {
            ASSERT(it->IsObject());
            auto        ParamObject = it->GetObject();
            const char* pTechName   = ParamObject["Name"].GetString();
            if (strcmp(pTechName, szShaderTechName) == 0)
            {
                auto& programArray = ParamObject["Program"];
                ASSERT(programArray.IsArray());
                for (auto itt = programArray.Begin(), end = programArray.End(); itt != end; ++itt)
                {
                    ASSERT(itt->IsObject());
                    auto        programItem = itt->GetObject();
                    const char* pShaderType = programItem["ShaderType"].GetString();

                    if (strcmp(pShaderType, szShaderTypeName) == 0)
                    {
                        const char* pEntrypoint  = programItem["Entrypoint"].GetString();
                        const char* pMacroDefine = programItem["MacroDefine"].GetString();
                        const char* pPath        = programItem["Path"].GetString();
                        /*if (strstr(pPath, "shader_dx12") != 0 || strstr(pPath, "shader_mb") == 0)
                        {
                            int a = 0;
                        }

                        if (strstr(pPath, "shader_dx12") == 0 || strstr(pPath, "shader_mb") != 0)
                        {
                            int a = 0;
                        }*/
                        IncludeFileHelper::_ExpandMacoDefine(pMacroDefine, m_szTechMacro);
                        strncpy(szShaderName, pPath, 1024);
                        m_szEntryPoint = pEntrypoint;
                        bFind          = true;
                        break;
                    }
                }
                break;
            }
        }
        if (!bFind)
        {
            KGLogPrintf(KGLOG_ERR, "%s 找不到tech: %s", szShaderSource, szShaderTechName);
        }
    }

    KGLOG_PROCESS_ERROR(szShaderName[0]);

    if (bJson)
    {
        KUniqueStr               ustrShaderPath = g_CachePathString(szShaderName, TRUE);
        NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
        m_pMainShaderFile = pShaderFilePool->RequestStaticShaderFile(eShaderStage, sShaderLoc, nullptr);
        KGLOG_PROCESS_ERROR(m_pMainShaderFile);
    }
    else
    {
        KUniqueStr               ustrShaderPath = g_CachePathString(szShaderSource, TRUE);
        NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
        // 经典模式，内部[section] 访问的形式
        m_pMainShaderFile = pShaderFilePool->RequestStaticShaderFile(eShaderStage, sShaderLoc, szShaderName);
        KGLOG_PROCESS_ERROR(m_pMainShaderFile);
    }

    if (sUserShaderLoc.IsValid())
    {
        m_pUserShaderFile = pShaderFilePool->RequestStaticShaderFile(eShaderStage, sUserShaderLoc, nullptr);
    }


    bResult = true;
Exit0:
    return bResult;
}

int32_t gfx::KGFX_ShaderTechItem::AddRef()
{
    return ++m_nRef;
}

int32_t gfx::KGFX_ShaderTechItem::GetRef()
{
    return m_nRef;
}

int32_t gfx::KGFX_ShaderTechItem::Release()
{
    int32_t nRef = --m_nRef;
    if (nRef == 0)
    {
        KGFX_ShaderFilePool* pPool = gfx::KGFX_GetShaderFilePool();
        BOOL                 bRet  = pPool->RemoveTechItem(m_szKey.c_str());
        ASSERT(bRet);
    }
    return nRef;
}

gfx::ShaderStageType gfx::KGFX_ShaderTechItem::GetShaderStage()
{
    return m_eShageStage;
}

const char* gfx::KGFX_ShaderTechItem::GetEntryPoint()
{
    return m_szEntryPoint.c_str();
}

BOOL gfx::KGFX_ShaderTechItem::CombineShader(const char* szMacros, gfx::IKGFX_CombinedShaderResult* pCombineShaderResult)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    size_t      userShaderPos = 0;
    std::string combineShaderResult;

    KGLOG_PROCESS_ERROR(m_pMainShaderFile);

    if (szMacros && szMacros[0])
    {
        IncludeFileHelper::_ExpandMacoDefine(szMacros, combineShaderResult);
    }

    if (!m_szTechMacro.empty())
    {
        combineShaderResult.append(m_szTechMacro);
    }

    combineShaderResult.append(m_pMainShaderFile->GetFileContent());

    userShaderPos = combineShaderResult.find(USER_SHADER_NAME);
    if (userShaderPos != std::string::npos)
    {
        size_t begin  = combineShaderResult.rfind("#include ", userShaderPos);
        size_t endPos = combineShaderResult.find("\n", begin);
        KGLOG_PROCESS_ERROR(begin != std::string::npos && endPos != std::string::npos);

        combineShaderResult.erase(begin, endPos - begin);
        if (m_pUserShaderFile)
        {
            const char* pUserShaderContent = m_pUserShaderFile->GetFileContent();
            combineShaderResult.insert(begin, pUserShaderContent);
        }
    }


    bRetCode = pCombineShaderResult->Fix(m_szKey.c_str(), combineShaderResult.c_str(), m_eShageStage);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = true;
Exit0:
    return bResult;
}

BOOL gfx::KGFX_ShaderTechItem::LoadFile(
    gfx::ShaderStageType eShaderStage,
    const char*          szShaderSource,
    const char*          szShaderDefPassName,
    const char*          szShaderTypeName,
    const char*          szUserShaderName
)
{
    BOOL bResult  = false;
    BOOL bRetCode = false;

    m_eShageStage         = eShaderStage;
    m_szShaderSource      = szShaderSource;
    m_szShaderDefPassName = szShaderDefPassName;
    m_szShaderTypeName    = szShaderTypeName;
    m_szUserShaderSource  = szUserShaderName;

    std::string szWholeFileString;

    KUniqueStr               ustrShaderPath = g_CachePathString(szShaderSource, TRUE);
    NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
    bRetCode = IncludeFileHelper::ReadWholeShaderFile(sShaderLoc, szWholeFileString, nullptr);
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = LoadFileFromWholeFileString(eShaderStage, szShaderSource, szWholeFileString, szShaderDefPassName, szShaderTypeName, sShaderLoc);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = true;
Exit0:
    return bResult;
}

const char* gfx::KGFX_ShaderTechItem::GetKey()
{
    return m_szKey.c_str();
}

void gfx::KGFX_ShaderTechItem::SetKey(const char* pKey)
{
    m_szKey = pKey;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

gfx::KGFX_ShaderFilePool::KGFX_ShaderFilePool()
{
}

gfx::KGFX_ShaderFilePool::~KGFX_ShaderFilePool()
{
    if (!m_mapShaderFile.empty())
    {
        KGLogPrintf(KGLOG_ERR, "KGFX_ShaderFilePool::m_mapIncludedShaderFile 没有释放干净，看看是不是有泄露了？");
        std::lock_guard<std::mutex> lock(m_ShaderMainShaderFileLock);
        for (auto& it : m_mapShaderFile)
        {
            SAFE_DELETE(it.second);
            // SAFE_RELEASE(it.second);
        }
        m_mapShaderFile.clear();
    }

    if (!m_mapShaderTechItem.empty())
    {
        KGLogPrintf(KGLOG_ERR, "KGFX_ShaderFilePool::m_mapShaderDefPassItem 没有释放干净，看看是不是有泄露了？");
        for (auto& it : m_mapShaderTechItem)
        {
            SAFE_RELEASE(it.second);
        }
        m_mapShaderTechItem.clear();
    }
}

BOOL gfx::KGFX_ShaderFilePool::RemoveShaderFile(const char* pKey)
{
    BOOL                        bRemoved = false;
    std::lock_guard<std::mutex> lock(m_ShaderMainShaderFileLock);
    auto                        it = m_mapShaderFile.find(pKey);
    if (it != m_mapShaderFile.end())
    {
        gfx::KGFX_ShaderFile* pFile = it->second;
        SAFE_DELETE(pFile);
        m_mapShaderFile.erase(it);
        bRemoved = true;
    }
    else
    {
        ASSERT(0);
    }
    return bRemoved;
}

BOOL gfx::KGFX_ShaderFilePool::RemoveTechItem(const char* pKey)
{
    BOOL                        bRemoved = false;
    std::lock_guard<std::mutex> lock(m_shaderTechItemLock);
    auto                        it = m_mapShaderTechItem.find(pKey);
    if (it != m_mapShaderTechItem.end())
    {
        gfx::KGFX_ShaderTechItem* pItem = it->second;
        SAFE_DELETE(pItem);
        m_mapShaderTechItem.erase(it);
        bRemoved = true;
    }
    else
    {
        ASSERT(0);
    }
    return bRemoved;
}

gfx::KGFX_ShaderFile* gfx::KGFX_ShaderFilePool::OnlyLoadShaderFile(const NSKBase::tagFileLocation& sShaderLoc, const char* szSectionName)
{
    std::lock_guard<std::mutex> lock(m_ShaderMainShaderFileLock);

    gfx::KGFX_ShaderFile* pMainShaderFile = nullptr;
    char                  key[1024];
    key[0] = '\0';

    if (szSectionName && szSectionName[0])
    {
        snprintf(key, 1024, "%s_%s", sShaderLoc.GetFilePath().Str(), szSectionName);
    }
    else
    {
        snprintf(key, 1024, "%s", sShaderLoc.GetFilePath().Str());
    }

    auto it = m_mapShaderFile.find(key);
    if (it == m_mapShaderFile.end())
    {
        pMainShaderFile = new gfx::KGFX_ShaderFile;
        BOOL bRetCode   = pMainShaderFile->OnlyLoadFile(sShaderLoc);
        if (bRetCode)
        {
            pMainShaderFile->SetKey(key);
            m_mapShaderFile.insert(std::make_pair<>(key, pMainShaderFile));
        }
        else
        {
            ASSERT(0);
            SAFE_DELETE(pMainShaderFile)
            goto Exit0;
        }
    }
    else
    {
        pMainShaderFile = it->second;
        pMainShaderFile->AddRef();
    }
Exit0:
    return pMainShaderFile;
}


gfx::KGFX_ShaderFile* gfx::KGFX_ShaderFilePool::RequestStaticShaderFile(
    gfx::ShaderStageType            eShaderStage,
    const NSKBase::tagFileLocation& sShaderLoc,
    const char*                     szSectionName
)
{
    std::lock_guard<std::mutex> lock(m_ShaderMainShaderFileLock);

    gfx::KGFX_ShaderFile* pMainShaderFile = nullptr;
    char                  key[1024];
    key[0] = '\0';

    if (szSectionName && szSectionName[0])
    {
        snprintf(key, 1024, "%s_%s", sShaderLoc.GetFilePath().Str(), szSectionName);
    }
    else
    {
        snprintf(key, 1024, "%s", sShaderLoc.GetFilePath().Str());
    }

    auto it = m_mapShaderFile.find(key);
    if (it == m_mapShaderFile.end())
    {
        pMainShaderFile = new gfx::KGFX_ShaderFile;
        BOOL bRetCode   = pMainShaderFile->LoadFile(sShaderLoc, szSectionName);
        if (bRetCode)
        {
            pMainShaderFile->SetKey(key);
            m_mapShaderFile.insert(std::make_pair<>(key, pMainShaderFile));
        }
        else
        {
            //ASSERT(0);
            SAFE_DELETE(pMainShaderFile)
            goto Exit0;
        }
    }
    else
    {
        pMainShaderFile = it->second;
        pMainShaderFile->AddRef();
    }
Exit0:
    return pMainShaderFile;
}


gfx::KGFX_ShaderTechItem* gfx::KGFX_ShaderFilePool::RequestFromShaderFile(
    gfx::ShaderStageType            eShaderStage,
    const char* szShaderFileName,
    const NSKBase::tagFileLocation& sUserShaderLoc,
    const char* szSection
)
{
    BOOL                        bRetCode = false;
    gfx::KGFX_ShaderTechItem* pShaderTechItem = nullptr;
    std::lock_guard<std::mutex> lock(m_shaderTechItemLock);

    char                        key[1024];
    key[0] = '\0';
    if (szSection && szSection[0])
    {
        snprintf(key, 1024, "%s@@@%s", szShaderFileName, szSection);
    }
    else
    {
        snprintf(key, 1024, "%s", szShaderFileName);
    }
    auto it = m_mapShaderTechItem.find(key);
    if (it == m_mapShaderTechItem.end())
    {
        BOOL bOk = true;
        gfx::KGFX_ShaderFilePool* pShaderFilePool = KGFX_GetShaderFilePool();
        pShaderTechItem = new gfx::KGFX_ShaderTechItem;
        pShaderTechItem->SetKey(key);
        pShaderTechItem->m_eShageStage = eShaderStage;
        KUniqueStr               ustrFileName = g_CachePathString(szShaderFileName, TRUE);
        NSKBase::tagFileLocation sShaderFileLoc(ustrFileName);
        pShaderTechItem->m_pMainShaderFile = pShaderFilePool->RequestStaticShaderFile(eShaderStage, sShaderFileLoc, szSection);
        if (!pShaderTechItem->m_pMainShaderFile)
        {
            bOk = false;
        }

        if (sUserShaderLoc.IsValid())
        {
            pShaderTechItem->m_pUserShaderFile = pShaderFilePool->RequestStaticShaderFile(eShaderStage, sUserShaderLoc, nullptr);
            if (!pShaderTechItem->m_pUserShaderFile)
            {
                bOk = false;
            }
        }

        if (bOk)
        {
            m_mapShaderTechItem.insert(std::make_pair<>(key, pShaderTechItem));
        }
        else
        {
            SAFE_DELETE(pShaderTechItem);
            goto Exit0;
        }
    }
    else
    {
        pShaderTechItem = it->second;
        pShaderTechItem->AddRef();
    }

Exit0:
    return pShaderTechItem;
}

gfx::KGFX_ShaderTechItem* gfx::KGFX_ShaderFilePool::RequestFromTechFile(
    gfx::ShaderStageType            eShaderStage,
    const char*                     szTechFileName,
    const char*                     szTechName,
    const NSKBase::tagFileLocation& sUserShaderLoc
)
{
    BOOL bRetCode = false;

    auto szShaderTypeName = IncludeFileHelper::GetShaderTypeName(eShaderStage);

    gfx::KGFX_ShaderTechItem*   pShaderTechItem = nullptr;
    std::lock_guard<std::mutex> lock(m_shaderTechItemLock);
    char                        key[1024] = "";
    snprintf(key, 1024, "%s@@%s@%s", szTechFileName, szTechName, szShaderTypeName);

    auto it = m_mapShaderTechItem.find(key);
    if (it == m_mapShaderTechItem.end())
    {
        std::string szWholeFileString;

        KUniqueStr               ustrFileName = g_CachePathString(szTechFileName, TRUE);
        NSKBase::tagFileLocation sTechFileLoc(ustrFileName);
        IncludeFileHelper::ReadWholeShaderFile(sTechFileLoc, szWholeFileString, nullptr);

        pShaderTechItem = new gfx::KGFX_ShaderTechItem;
        pShaderTechItem->SetKey(key);
        bRetCode = pShaderTechItem->LoadFileFromWholeFileString(eShaderStage, szTechFileName, szWholeFileString, szTechName, szShaderTypeName, sUserShaderLoc);
        if (bRetCode)
        {
            m_mapShaderTechItem.insert(std::make_pair<>(key, pShaderTechItem));
        }
        else
        {
            SAFE_DELETE(pShaderTechItem);
            goto Exit0;
        }
    }
    else
    {
        pShaderTechItem = it->second;
        pShaderTechItem->AddRef();
    }

Exit0:
    return pShaderTechItem;
}


//////////////////////////////////////////////////////////////////////////////////////////////////


gfx::KGFX_ShaderFilePool* g_pKGFXShaderFilePool = nullptr;

void gfx::KGFX_CreateShaderFilePool()
{
    if (!g_pKGFXShaderFilePool)
    {
        g_pKGFXShaderFilePool = new gfx::KGFX_ShaderFilePool;
    }
}
void gfx::KGFX_DestroyShaderFilePool()
{
    SAFE_DELETE(g_pKGFXShaderFilePool);
}

gfx::KGFX_ShaderFilePool* gfx::KGFX_GetShaderFilePool()
{
    return g_pKGFXShaderFilePool;
}

gfx::KGFX_DXCComplierDX12* g_pKGFXDXCShaderComplier = nullptr;

void gfx::KGFX_CreateDXCComplier()
{
    if (!g_pKGFXDXCShaderComplier)
    {
        g_pKGFXDXCShaderComplier = new gfx::KGFX_DXCComplierDX12;
        g_pKGFXDXCShaderComplier->Init();
        std::filesystem::path hlslRootPath = L"enginedata/material/shader_dx12/include/";
        g_pKGFXDXCShaderComplier->SetIncludePath(hlslRootPath);
    }
}

void gfx::KGFX_DestroyDXCComplier()
{
    if (g_pKGFXDXCShaderComplier)
    {
        SAFE_DELETE(g_pKGFXDXCShaderComplier);
    }
}

gfx::KGFX_DXCComplierDX12* gfx::KGFX_GetDXCComplier()
{
    return g_pKGFXDXCShaderComplier;
}
// gfx::KGFX_ShaderResource::KGFX_ShaderResource()
//{
// }
//
// gfx::KGFX_ShaderResource::~KGFX_ShaderResource()
//{
//
// }
//
// BOOL gfx::KGFX_ShaderResource::LoadShader(
//	const char* szShaderTypeName,
//	const char* szShaderSource,
//	const char* szIncludedShaderSource,
//	const char* szShaderDef,
//	const char* szMacro,
//	BOOL bReCreate,
//	BOOL bByBuildToolCmd = false,
//	int nPlatform = 0,
//	KEnumMtlTaskLevel uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
//{
//	BOOL bResult = false;
//	BOOL bRetCode = false;
//	KGFX_ShaderFile *pShaderFile = new gfx::KGFX_ShaderFile;
//
//	bRetCode = pShaderFile->LoadShader(szShaderTypeName, szShaderSource, szIncludedShaderSource, szShaderDef, szMacro, bReCreate, bByBuildToolCmd, nPlatform, uThreadLevel);
//	KGLOG_PROCESS_ERROR(bRetCode);
//
//	m_vecShaderFile.push_back(pShaderFile);
//
//	bResult = true;
// Exit0:
//	return bResult;
// }
//
// BOOL gfx::KGFX_ShaderResource::PostLoad()
//{
//
// }
