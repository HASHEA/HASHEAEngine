#include "KXGReporter.h"
#include "Engine/KGLog.h"
#include "KBase/Public/str/KUtf8Convert.h"
#include <sstream>
#include "KBase/Public/io/KIni.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/KEsDrv.h"
#include "Engine/File.h"
//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

KXGReporter g_sX3DXGSDKReporter;

namespace
{
    class Base64
    {
    public:
        static bool Encode(const unsigned char* input, size_t in_len, std::string& out)
        {
            static constexpr char sEncodingTable[] =
                {
                    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                    'w', 'x', 'y', 'z', '0', '1', '2', '3',
                    '4', '5', '6', '7', '8', '9', '+', '/'
                };

            size_t out_len = 4 * ((in_len + 2) / 3);
            out.resize(out_len);
            size_t i;
            char*  p = const_cast<char*>(out.c_str());

            for (i = 0; i < in_len - 2; i += 3)
            {
                *p++ = sEncodingTable[(input[i] >> 2) & 0x3F];
                *p++ = sEncodingTable[((input[i] & 0x3) << 4) | ((int)(input[i + 1] & 0xF0) >> 4)];
                *p++ = sEncodingTable[((input[i + 1] & 0xF) << 2) | ((int)(input[i + 2] & 0xC0) >> 6)];
                *p++ = sEncodingTable[input[i + 2] & 0x3F];
            }
            if (i < in_len)
            {
                *p++ = sEncodingTable[(input[i] >> 2) & 0x3F];
                if (i == (in_len - 1))
                {
                    *p++ = sEncodingTable[((input[i] & 0x3) << 4)];
                    *p++ = '=';
                }
                else
                {
                    *p++ = sEncodingTable[((input[i] & 0x3) << 4) | ((int)(input[i + 1] & 0xF0) >> 4)];
                    *p++ = sEncodingTable[((input[i + 1] & 0xF) << 2)];
                }
                *p++ = '=';
            }

            return true;
        }

        static bool Encode(const char* input, size_t in_len, std::string& out)
        {
            return Encode((const unsigned char*)input, in_len, out);
        }

        static bool Decode(const unsigned char* input, size_t in_len, std::string& out)
        {
            static constexpr unsigned char kDecodingTable[] =
                {
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
                    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
                    64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
                    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
                };

            if (in_len % 4 != 0)
                return false;

            size_t out_len = in_len / 4 * 3;
            if (input[in_len - 1] == '=')
                out_len--;
            if (input[in_len - 2] == '=')
                out_len--;

            out.resize(out_len);

            for (size_t i = 0, j = 0; i < in_len;)
            {
                uint32_t a = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
                uint32_t b = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
                uint32_t c = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
                uint32_t d = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];

                uint32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + (d << 0 * 6);

                if (j < out_len)
                    out[j++] = (triple >> 2 * 8) & 0xFF;
                if (j < out_len)
                    out[j++] = (triple >> 1 * 8) & 0xFF;
                if (j < out_len)
                    out[j++] = (triple >> 0 * 8) & 0xFF;
            }

            return true;
        }

        static bool Decode(const char* input, size_t in_len, std::string& out)
        {
            return Decode((const unsigned char*)input, in_len, out);
        }
    };
} // namespace

void KXGReporter::TrackShaderVSFSCompileEvent(int nPlatform, const char* pcszShaderSource, const char* pcszIncludedShaderSource, const char* pcszShaderDef, const char* pcszMacro, uint32_t uHashVs, uint32_t uHashFs)
{
#if 0 // YGame先关闭shader上报
	// Release 才上报shader编译信息，避免开发本地Debug上报测试信息，污染上报数据
//#ifdef NDEBUG
	// #if 1
	PROF_CPU_DETAIL();

	bool                               bRetCode                        = false;
	char                               szConfigPath[MAX_PATH]          = "";
	unsigned                           uShaderCmdHash                  = 0;
	char                               szShaderHash[64]                = "";
	char                               szShaderCompileCmd[1024]        = "";
	char                               szShaderSoruce[MAX_PATH]        = "";
	char                               szShaderIncludeSoruce[MAX_PATH] = "";
	std::map<const char*, const char*> mapEventBody;
	std::string                        strEncodedShaderCmd;
	std::string                        strDecodedShaderCmd;
	KIni                               pConfigFile;
	KEngineOptions*                    pEngineOptions = NSEngine::GetEngineOptions();

	const char* pcszPlatform      = "PLATFORM=";
	const char* pcszPlatformBegin = nullptr;

	KGLOG_PROCESS_ERROR(IsValid());
	KGLOG_PROCESS_ERROR(pEngineOptions);

	// 非客户端模式下 不提交shader编译信息
	//KG_PROCESS_SUCCESS(!pEngineOptions->bHDClient);
	// 没有开启宏转换为特殊化常量，不上报
	KG_PROCESS_SUCCESS(!DrvOption::bMacroToSpicalizationConstantsEnable);
	// 如果上报Key为空，也不上报
	KG_PROCESS_SUCCESS(pEngineOptions->strShaderCompileEventId.empty());

	KGLOG_PROCESS_ERROR(pcszShaderSource);
	KGLOG_PROCESS_ERROR(pcszIncludedShaderSource);
	KGLOG_PROCESS_ERROR(pcszShaderDef);
	KGLOG_PROCESS_ERROR(pcszMacro);

	pcszPlatformBegin = strstr(pcszMacro, pcszPlatform);
	ASSERT(!pcszPlatformBegin);

	// _EncodeShaderCmd 后全ASCII码，不必转UTF8了
	// KCONV::Convert("UTF-8", "GBK", pcszShaderSource, MAX_PATH, szShaderSoruce, MAX_PATH);
	// KCONV::Convert("UTF-8", "GBK", pcszIncludedShaderSource, MAX_PATH, szShaderIncludeSoruce, MAX_PATH);

	// 部分调试宏过滤，不上报
	if (strstr(pcszMacro, "SHADER_FLAG=") ||
	    strstr(pcszMacro, "DEBUG_PBR=") ||
	    strstr(pcszMacro, "MTL_DEBUG_FLAG="))
	{
		KG_PROCESS_SUCCESS(TRUE);
	}

	snprintf(szShaderCompileCmd, countof(szShaderCompileCmd), "vsfs_2,%d,%s,%s,%s,%s", nPlatform, pcszShaderSource, pcszIncludedShaderSource, pcszShaderDef, pcszMacro);
	szShaderCompileCmd[countof(szShaderCompileCmd) - 1] = 0;
	//KGLogPrintf(KGLOG_INFO, "[XXXXXXX] %s", szShaderCompileCmd);

	bRetCode = _EncodeShaderCmd(szShaderCompileCmd, strEncodedShaderCmd);
	KGLOG_PROCESS_ERROR(bRetCode);
	// bRetCode = _DecodeShaderCmd(strEncodedShaderCmd.c_str(), strDecodedShaderCmd);

	uShaderCmdHash = g_FileNameHash(szShaderCompileCmd);
	snprintf(szShaderHash, countof(szShaderHash), "vsfs_%u", uShaderCmdHash);
	szShaderHash[countof(szShaderHash) - 1] = 0;

	mapEventBody.insert(std::make_pair("shader_hash", szShaderHash));
	mapEventBody.insert(std::make_pair("compile_cmd", strEncodedShaderCmd.c_str()));

	//bRetCode = m_piXGSDK->TrackEvent(pEngineOptions->strShaderCompileEventId.c_str(), "shader_compile", mapEventBody);
	//KGLOG_PROCESS_ERROR(bRetCode);

Exit1:
Exit0:
	return;
//#endif
#endif
}

void KXGReporter::TrackShaderCSCompileEvent(int nPlatform, const char* pcszShaderSource, const char* pcszIncludedShaderSource, const char* pcszShaderDef, const char* pcszMacro, uint32_t uHashCS)
{
#if 0 // YGame先关闭shader上报
	// Release 才上报shader编译信息，避免开发本地Debug上报测试信息，污染上报数据
//#ifdef NDEBUG
	// #if 1
	PROF_CPU_DETAIL();

	bool                               bRetCode                        = false;
	char                               szConfigPath[MAX_PATH]          = "";
	unsigned                           uShaderCmdHash                  = 0;
	char                               szShaderHash[64]                = "";
	char                               szShaderCompileCmd[1024]        = "";
	char                               szShaderSoruce[MAX_PATH]        = "";
	char                               szShaderIncludeSoruce[MAX_PATH] = "";
	std::map<const char*, const char*> mapEventBody;
	std::string                        strEncodedShaderCmd;
	std::string                        strDecodedShaderCmd;
	KIni                               pConfigFile;
	KEngineOptions*                    pEngineOptions = NSEngine::GetEngineOptions();

	const char* pcszPlatform      = "PLATFORM=";
	const char* pcszPlatformBegin = nullptr;

	KGLOG_PROCESS_ERROR(IsValid());
	KGLOG_PROCESS_ERROR(pEngineOptions);

	// 非客户端模式下 不提交shader编译信息
	//KG_PROCESS_SUCCESS(!pEngineOptions->bHDClient);
	// 没有开启宏转换为特殊化常量，不上报
	KG_PROCESS_SUCCESS(!DrvOption::bMacroToSpicalizationConstantsEnable);
	// 如果上报Key为空，也不上报
	KG_PROCESS_SUCCESS(pEngineOptions->strShaderCompileEventId.empty());

	KGLOG_PROCESS_ERROR(pcszShaderSource);
	KGLOG_PROCESS_ERROR(pcszIncludedShaderSource);
	KGLOG_PROCESS_ERROR(pcszShaderDef);
	KGLOG_PROCESS_ERROR(pcszMacro);

	pcszPlatformBegin = strstr(pcszMacro, pcszPlatform);
	ASSERT(!pcszPlatformBegin);

	// _EncodeShaderCmd 后全ASCII码，不必转UTF8了
	// KCONV::Convert("UTF-8", "GBK", pcszShaderSource, MAX_PATH, szShaderSoruce, MAX_PATH);
	// KCONV::Convert("UTF-8", "GBK", pcszIncludedShaderSource, MAX_PATH, szShaderIncludeSoruce, MAX_PATH);

	// 部分调试宏过滤，不上报
	if (strstr(pcszMacro, "SHADER_FLAG=") ||
	    strstr(pcszMacro, "DEBUG_PBR=") ||
	    strstr(pcszMacro, "MTL_DEBUG_FLAG="))
	{
		KG_PROCESS_SUCCESS(TRUE);
	}

	snprintf(szShaderCompileCmd, countof(szShaderCompileCmd), "cs_2,%d,%s,%s,%s,%s", nPlatform, pcszShaderSource, pcszIncludedShaderSource, pcszShaderDef, pcszMacro);
	szShaderCompileCmd[countof(szShaderCompileCmd) - 1] = 0;
	//KGLogPrintf(KGLOG_INFO, "[XXXXXXX] %s", szShaderCompileCmd);

	bRetCode = _EncodeShaderCmd(szShaderCompileCmd, strEncodedShaderCmd);
	KGLOG_PROCESS_ERROR(bRetCode);
	// bRetCode = _DecodeShaderCmd(strEncodedShaderCmd.c_str(), strDecodedShaderCmd);

	uShaderCmdHash = g_FileNameHash(szShaderCompileCmd);
	snprintf(szShaderHash, countof(szShaderHash), "cs_%u", uShaderCmdHash);
	szShaderHash[countof(szShaderHash) - 1] = 0;

	mapEventBody.insert(std::make_pair("shader_hash", szShaderHash));
	mapEventBody.insert(std::make_pair("compile_cmd", strEncodedShaderCmd.c_str()));

	//bRetCode = m_piXGSDK->TrackEvent(pEngineOptions->strShaderCompileEventId.c_str(), "shader_compile", mapEventBody);
	//KGLOG_PROCESS_ERROR(bRetCode);

Exit1:
Exit0:
	return;
//#endif
#endif
}

bool KXGReporter::_EncodeShaderCmd(const char* pcszShaderCompileCmd, std::string& strResult)
{
    if (!pcszShaderCompileCmd)
    {
        return false;
    }

    bool bRetCode = Base64::Encode(pcszShaderCompileCmd, strlen(pcszShaderCompileCmd), strResult);

    std::stringstream strBuilder;
    char              chChar;
    for (auto iter = strResult.begin(); iter != strResult.end(); ++iter)
    {
        chChar = *iter;
        if (isupper(chChar))
        {
            strBuilder << '^';
        }
        strBuilder << chChar;
    }
    strResult = strBuilder.str();

    return bRetCode;
}

bool KXGReporter::_DecodeShaderCmd(const char* pcszShaderCompileCmd, std::string& strResult)
{
    if (!pcszShaderCompileCmd)
    {
        return false;
    }
    return Base64::Decode(pcszShaderCompileCmd, strlen(pcszShaderCompileCmd), strResult);
}
