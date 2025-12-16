////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KGFX_RenderActionEncoder.cpp
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#include "KBase/Public/core_base_macro.h"
#include "IKGFX_RenderActionEncoder.h"
#include "Engine/KGLog.h"

namespace gfx
{
    KGFX_RenderActionEncoder::~KGFX_RenderActionEncoder()
    {
        ASSERT(!m_pBuffer);

        SAFE_FREE(m_pBuffer);
    }

    bool KGFX_RenderActionEncoder::Init(int nBufferSize)
    {
        bool bResult = false;

        KGLOG_ASSERT_EXIT(!m_bInitialized);
        KGLOG_ASSERT_EXIT(nBufferSize > 0);
        m_nBufferSize = nBufferSize;
        m_pBuffer     = (unsigned char*)malloc(m_nBufferSize);
        KGLOG_ASSERT_EXIT(m_pBuffer);

        KGLogPrintf(KGLOG_INFO, "[KGFX] KGFX_RenderActionEncoder Init, buffer size:%d", nBufferSize);

        m_bInitialized = true;
        bResult        = true;
    Exit0:
        return true;
    }

    void KGFX_RenderActionEncoder::UnInit()
    {
        m_bInitialized = false;
        SAFE_FREE(m_pBuffer);
    }

    void* KGFX_RenderActionEncoder::WriteAction(int nSize)
    {
        void* pResult = nullptr;

        KGLOG_ASSERT_EXIT(m_bInitialized);
        KGLOG_ASSERT_EXIT(m_bWriting);
        KGLOG_ASSERT_EXIT(nSize > 0);

        if (nSize > m_sActionWriter.GetUnWriteSize())
        {
            KGLogPrintf(KGLOG_ERR, "[KGFX] KGFX_RenderActionEncoder WriteReference, buffer overflow, write size:%d, buffer left size:%d", nSize, (int)m_sActionWriter.GetUnWriteSize());
            goto Exit0;
        }

        pResult = m_sActionWriter.ReferenceWrite(nSize);
        KGLOG_ASSERT_EXIT(pResult);

    Exit0:
        return pResult;
    }

    ERenderAction KGFX_RenderActionEncoder::PeekActionType()
    {
        ERenderAction eResult = ERenderAction::Invalid;

        KGLOG_ASSERT_EXIT(m_bInitialized);
        KGLOG_ASSERT_EXIT(!m_bWriting);

        if (m_sActionReader.GetUnReadSize() < sizeof(ERenderAction))
        {
            KGLogPrintf(KGLOG_ERR, "[KGFX] KGFX_RenderActionEncoder PeekActionType, buffer overflow, buffer left size:%d", (int)m_sActionReader.GetUnReadSize());
            goto Exit0;
        }

        {
            ERenderAction* pActionType = (ERenderAction*)m_sActionReader.PeekRead(sizeof(ERenderAction));
            KGLOG_ASSERT_EXIT(pActionType);

            eResult = *pActionType;
        }

    Exit0:
        return eResult;
    }

    void* KGFX_RenderActionEncoder::ReadAction(int nSize)
    {
        void* pResult = nullptr;

        KGLOG_ASSERT_EXIT(m_bInitialized);
        KGLOG_ASSERT_EXIT(!m_bWriting);
        KGLOG_ASSERT_EXIT(nSize > 0);

        if (nSize > m_sActionReader.GetUnReadSize())
        {
            KGLogPrintf(KGLOG_ERR, "[KGFX] KGFX_RenderActionEncoder ReadAction, buffer overflow, read size:%d, buffer left size:%d", nSize, (int)m_sActionReader.GetUnReadSize());
            goto Exit0;
        }

        pResult = m_sActionReader.ReferenceRead(nSize);
        KGLOG_ASSERT_EXIT(pResult);

    Exit0:
        return pResult;
    }
} // namespace gfx
