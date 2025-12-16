#include "KVulkanPrivate.h"
#include "KVulkanDefine.h"
#include "GFXVulkan.h"

namespace gfx
{
    /////////////////////////////////////////////////////////////////////////
    KPipeline::KPipeline()
    {
        static std::atomic<uint32_t> uCreateIdSeed{0};
        m_uCreateId = ++uCreateIdSeed;
    }

    KPipeline::~KPipeline()
    {
    }

    /////////////////////////////////////////////////////////////////////////
    KGraphicsPipeline::KGraphicsPipeline()
    {
    }

    KGraphicsPipeline::~KGraphicsPipeline()
    {
    }

    enumForProcessType KGraphicsPipeline::GetType() const
    {
        return FOR_GRPAHIC;
    }

    /////////////////////////////////////////////////////////////////////////
    KComputePipeline::KComputePipeline()
    {
    }

    KComputePipeline::~KComputePipeline()
    {
    }

    enumForProcessType KComputePipeline::GetType() const
    {
        return FOR_COMPUTE;
    }

    /////////////////////////////////////////////////////////////////////////
    KGraphicContext::KGraphicContext()
    {
        m_nRef = 1;
    }

    KGraphicContext::~KGraphicContext()
    {
    }

    int32_t KGraphicContext::AddRef()
    {
        m_nRef++;
        return m_nRef;
    }

    int32_t KGraphicContext::GetRef()
    {
        return m_nRef;
    }

    int32_t KGraphicContext::Release()
    {
        m_nRef--;
        return m_nRef;
    }

    thread_local uint32_t _thread_id = 0;

    uint32_t GetRenderThreadid()
    {
        return _thread_id;
    }

    void SetRenderThreadid(uint32_t id)
    {
        _thread_id = id;
    }

    KWindow::KWindow()
    {
        m_uId = gfx::CONTEXT_COUNT;
        m_szWindowName[0] = '\0';

        m_uWidth = 0;
        m_uHeight = 0;
        m_uSwapChainWidth = 0;
        m_uSwapChainHeight = 0;
        m_window = 0;
        m_bWindowInvalidated = false;
    }

    void KWindow::DestroyWindowA()
    {
#ifdef _WIN32
        if (m_window)
        {
            ::DestroyWindow(m_window);
        }
#endif
    }



    gfx::KProgramUniformBlock::KProgramUniformBlock()
    {
        m_block16bytesAlignMemoryForGpu = 0;

        m_nLayoutBindingVs = -1;
        m_nLayoutBindingFs = -1;
        m_nLayoutBindingCs = -1;
        m_nLayoutBindingGs = -1;
        m_nLayoutBindingTc = -1;
        m_nLayoutBindingTe = -1;

        m_nSpaceVS = -1;
        m_nSpaceFS = -1;
        m_nSpaceCS = -1;
        m_nSpaceGS = -1;
        m_nSpaceTC = -1;
        m_nSpaceTE = -1;

        m_n32BitValuesVs = 0;
        m_n32BitValuesFs = 0;
        m_n32BitValuesCs = 0;
        m_n32BitValuesGs = 0;
        m_n32BitValuesTc = 0;
        m_n32BitValuesTe = 0;


        m_UniformScopeType = 0;
        m_szName = nullptr;
        m_UniformType = gfx::UBO_UNIFORM;
        m_globalUboId = 0;
    }

    gfx::KProgramUniformBlock::~KProgramUniformBlock()
    {
        for (auto pUniform : m_Uniforms)
        {
            SAFE_DELETE(pUniform);
        }
    }

    gfx::KProgramUniformSampler::~KProgramUniformSampler()
    {
        // 不用删除，GFXDevice Unit 时会统一释放
        m_pSampler = nullptr;
    }

    BOOL KProgramUniformTexture::Save(KByteBufferStream& byteStream)
    {
        byteStream.WriteShort(m_nLayoutBindingVs);
        byteStream.WriteShort(m_nLayoutBindingFs);
        byteStream.WriteShort(m_nLayoutBindingCs);
        byteStream.WriteUInt(m_uNameHash);
        byteStream.WriteUInt(m_uArrayCount);
        byteStream.WriteString(m_szName);
        byteStream.WriteUChar(m_UniformType);
        byteStream.WriteUChar((uint8_t)m_eTextureType);
        if (DrvOption::bDebugShaderReflector)
        {
            printf("KProgramUniformTexture = %s bindingvs:%d, bindingfs:%d, bindingcs:%d, arraycount:%d, UniformType:%d, m_eTextureType: %d \r\n", m_szName, m_nLayoutBindingVs, m_nLayoutBindingFs, m_nLayoutBindingCs, m_uArrayCount, m_UniformType, (int32_t)m_eTextureType);
        }
        return true;
    }

    BOOL KProgramUniformTexture::Load(KByteBufferStream& byteStream)
    {
        byteStream.ReadShort(m_nLayoutBindingVs);
        byteStream.ReadShort(m_nLayoutBindingFs);
        byteStream.ReadShort(m_nLayoutBindingCs);
        byteStream.ReadUInt(m_uNameHash);
        byteStream.ReadUInt(m_uArrayCount);
        char _szName[MAX_PATH];
        byteStream.ReadString(_szName, MAX_PATH);
        m_szName = GetParamNameByPool(_szName);
        byteStream.ReadUChar((uint8_t&)m_UniformType);
        byteStream.ReadUChar((uint8_t&)m_eTextureType);
        return true;
    }

    BOOL KProgramUniformBlock::Save(KByteBufferStream& byteStream)
    {
        byteStream.WriteShort(m_nLayoutBindingVs);
        byteStream.WriteShort(m_nLayoutBindingFs);
        byteStream.WriteShort(m_nLayoutBindingCs);
        byteStream.WriteString(m_szName);
        byteStream.WriteUInt(m_block16bytesAlignMemoryForGpu);
        byteStream.WriteUChar(m_UniformType);

        if (DrvOption::bDebugShaderReflector)
        {
            printf("KProgramUniformBlock= %s, bindingvs:%d, bindingfs:%d, bindingcs:%d, size:%d, m_UniformType:%d\r\n", m_szName, m_nLayoutBindingVs, m_nLayoutBindingFs, m_nLayoutBindingCs, m_block16bytesAlignMemoryForGpu, (int32_t)m_UniformType);
        }

        uint32_t uCount = (uint32_t)m_Uniforms.size();
        byteStream.WriteUInt(uCount);
        for (auto it : m_Uniforms)
        {
            gfx::KProgramUniform* pUniform = it;
            pUniform->Save(byteStream);
        }

        uCount = (uint32_t)m_vecPushConstantsRangeMap.size();
        byteStream.WriteUInt(uCount);
        for (auto it : m_vecPushConstantsRangeMap)
        {
            const KPushContantsRangeMap& range = it;
            range.Save(byteStream);
        }

        byteStream.WriteUInt(m_UniformScopeType);
        byteStream.WriteUInt(m_globalUboId);

        return true;
    }

    BOOL KProgramUniformBlock::Load(KByteBufferStream& byteStream)
    {
        byteStream.ReadShort(m_nLayoutBindingVs);
        byteStream.ReadShort(m_nLayoutBindingFs);
        byteStream.ReadShort(m_nLayoutBindingCs);
        char _szName[MAX_PATH];
        byteStream.ReadString(_szName, MAX_PATH);
        m_szName = GetParamNameByPool(_szName);
        byteStream.ReadUInt(m_block16bytesAlignMemoryForGpu);
        byteStream.ReadUChar((uint8_t&)m_UniformType);
        uint32_t uCount = 0;
        byteStream.ReadUInt(uCount);
        for (uint32_t i = 0; i < uCount; ++i)
        {
            gfx::KProgramUniform* pUniform = new gfx::KProgramUniform;
            pUniform->Load(byteStream);
            m_Uniforms.insert(pUniform);
        }

        byteStream.ReadUInt(uCount);
        for (uint32_t i = 0; i < uCount; ++i)
        {
            KPushContantsRangeMap rangemap;
            rangemap.Load(byteStream);
            m_vecPushConstantsRangeMap.push_back(rangemap);
        }
        byteStream.ReadUInt((uint32_t&)m_UniformScopeType);
        byteStream.ReadUInt((uint32_t&)m_globalUboId);
        return true;
    }

    BOOL KPushContantsRangeMap::Save(KByteBufferStream& byteStream) const
    {
        byteStream.WriteUChar((uint8_t)shadertype);
        byteStream.WriteUInt(offset);
        byteStream.WriteUInt(toRange);

        if (DrvOption::bDebugShaderReflector)
        {
            printf("KPushContantsRangeMap = shadertype:%d offset:%d toRange:%d \r\n", shadertype, offset, toRange);
        }

        return true;
    }

    BOOL KPushContantsRangeMap::Load(KByteBufferStream& byteStream)
    {
        byteStream.ReadUChar((uint8_t&)shadertype);
        byteStream.ReadUInt(offset);
        byteStream.ReadUInt(toRange);
        return true;
    }

    BOOL KProgramUniform::Save(KByteBufferStream& byteStream)
    {
        byteStream.WriteUInt(m_uNameHash);
        byteStream.WriteString(m_szName);
        byteStream.WriteUChar((uint8_t)m_UniformType);
        byteStream.WriteUChar((uint8_t)m_UniformBaseType);
        byteStream.WriteUChar(m_uVectorSize);
        byteStream.WriteUChar(m_uMatcol);
        byteStream.WriteUChar(m_uMatrow);
        byteStream.WriteUShort(m_uArrayCount);
        byteStream.WriteUInt(m_uByteSize);
        byteStream.WriteUShort(m_nOffset);

        if (DrvOption::bDebugShaderReflector)
        {
            printf("KProgramUniform = name:%s m_UniformType:%d m_UniformBaseType:%d m_uVectorSize:%d m_uMatcol:%d m_uMatrow:%d m_uArrayCount:%d m_uByteSize:%d m_nOffset:%d \r\n", m_szName, m_UniformType, m_UniformBaseType, m_uVectorSize, m_uMatcol, m_uMatrow, m_uArrayCount, m_uByteSize, m_nOffset);
        }

        return true;
    }

    BOOL KProgramUniform::Load(KByteBufferStream& byteStream)
    {
        byteStream.ReadUInt(m_uNameHash);
        char _szName[MAX_PATH];
        byteStream.ReadString(_szName, MAX_PATH);
        m_szName = GetParamNameByPool(_szName);
        byteStream.ReadUChar((uint8_t&)m_UniformType);
        byteStream.ReadUChar((uint8_t&)m_UniformBaseType);
        byteStream.ReadUChar(m_uVectorSize);
        byteStream.ReadUChar(m_uMatcol);
        byteStream.ReadUChar(m_uMatrow);
        byteStream.ReadUShort(m_uArrayCount);
        byteStream.ReadUInt(m_uByteSize);
        byteStream.ReadUShort(m_nOffset);
        return true;
    }

    BOOL KProgramAttribute::Save(KByteBufferStream& byteStream)
    {
        byteStream.WriteString(szName);
        byteStream.WriteUChar(type);
        byteStream.WriteBool(bInstanceData);
        byteStream.WriteUInt(uSize);
        byteStream.WriteUChar(fmt);
        byteStream.WirteInt(nLocation);
        if (DrvOption::bDebugShaderReflector)
        {
            printf("KProgramAttribute = name:%s, type:%d, bInstanceData:%d, uSize:%d, fmt:%d nLocation:%d\r\n", szName, type, bInstanceData, uSize, fmt, nLocation);
        }
        return true;
    }

    BOOL KProgramAttribute::Load(KByteBufferStream& byteStream)
    {
        char _szName[MAX_PATH];
        byteStream.ReadString(_szName, MAX_PATH);
        szName = GetParamNameByPool(_szName);
        byteStream.ReadUChar((uint8_t&)type);
        byteStream.ReadBool(bInstanceData);
        byteStream.ReadUInt(uSize);
        byteStream.ReadUChar((uint8_t&)fmt);
        byteStream.ReadInt(nLocation);
        return true;
    }
}
