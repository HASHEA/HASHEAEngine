#pragma once

#ifdef _WIN32
#include "KBase/Public/str/KStrHelper.h"
#include <unordered_set>
#include "KGBaseDef/Public/core_base_macro.h"

struct ShaderCreateFileRecord
{
private:
    std::mutex                   m_mtxFileLock;
    std::unordered_set<uint32_t> m_setUniqueInfo;
    FILE*                        m_pfShaderCreateFileLog = nullptr;

public:
    ShaderCreateFileRecord()
    {
        ASSERT(!m_pfShaderCreateFileLog);
        m_pfShaderCreateFileLog = fopen("compileshader.log", "wb");
    }

    ~ShaderCreateFileRecord()
    {
        if (m_pfShaderCreateFileLog)
        {
            fclose(m_pfShaderCreateFileLog);
            m_pfShaderCreateFileLog = nullptr;
            m_setUniqueInfo.clear();
        }
    }

    void Recorder(const char* pcszCompileInfo)
    {
        ASSERT(pcszCompileInfo);
        if (!m_pfShaderCreateFileLog)
        {
            return;
        }
        uint32_t uHashCode = KSTR_HELPER::GetHashCodeForString32Bit(pcszCompileInfo);
        if (m_setUniqueInfo.find(uHashCode) == m_setUniqueInfo.end())
        {
            m_setUniqueInfo.insert(uHashCode);
            m_mtxFileLock.lock();
            fprintf(m_pfShaderCreateFileLog, "%s", pcszCompileInfo);
#ifdef JX3_X3D
            fflush(m_pfShaderCreateFileLog);
#endif
            m_mtxFileLock.unlock();
        }
    }
};

// 记录shader编译失败类
struct ShaderCompileErrFileRecord
{
private:
    std::mutex                   m_mtxErrorLock;
    std::unordered_set<uint32_t> m_setUniqueFileName;
    FILE*                        m_pfShaderCompileFileLog = nullptr;

public:
    ShaderCompileErrFileRecord()
    {
        ASSERT(!m_pfShaderCompileFileLog);
        m_pfShaderCompileFileLog = fopen("materialcompile.log", "wb");
    }

    ~ShaderCompileErrFileRecord()
    {
        if (m_pfShaderCompileFileLog)
        {
            fclose(m_pfShaderCompileFileLog);
            m_pfShaderCompileFileLog = nullptr;
            m_setUniqueFileName.clear();
        }
    }

    void Recoder(const char* pcszErrorInfo)
    {
        ASSERT(pcszErrorInfo);
        if (!m_pfShaderCompileFileLog)
        {
            return;
        }
        uint32_t uHashCode = KSTR_HELPER::GetHashCodeForString32Bit(pcszErrorInfo);
        if (m_setUniqueFileName.find(uHashCode) == m_setUniqueFileName.end())
        {
            m_setUniqueFileName.insert(uHashCode);
            m_mtxErrorLock.lock();
            fprintf(m_pfShaderCompileFileLog, "%s\r\n", pcszErrorInfo);
#ifdef JX3_X3D
            fflush(m_pfShaderCompileFileLog);
#endif
            m_mtxErrorLock.unlock();
        }
    }
};

extern ShaderCreateFileRecord     g_shaderCompileRecord;
extern ShaderCompileErrFileRecord g_shaderCompileErrRecord;
#endif // _WIN32
