////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IKGFX_RenderActionEncoder.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "KBase/Public/memory/KBufferReader.h"
#include "KBase/memory/KBufferWriter.h"
#include "IKGFX_RenderAction.h"

namespace gfx
{
    class KGFX_RenderActionEncoder
    {
    public:
        KGFX_RenderActionEncoder() = default;
        ~KGFX_RenderActionEncoder();

    private:
        int            m_nBufferSize{0};
        unsigned char* m_pBuffer{nullptr};

        NSKBase::KBufferReader m_sActionReader;
        NSKBase::KBufferWriter m_sActionWriter;

    private:
        bool m_bInitialized{false};
        bool m_bWriting{true};

    public:
        bool Init(int nBufferSize);
        void UnInit();

    public:
        void SetWriting(bool bWriting) { m_bWriting = bWriting; }

    public:
        // Write
        void*         WriteAction(int nSize);
        // Read
        ERenderAction PeekActionType();
        void*         ReadAction(int nSize);
    };
} // namespace gfx
