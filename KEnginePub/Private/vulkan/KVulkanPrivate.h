#pragma once

#include "KEnginePub/Public/IGFX_Public.h"
#include "KBase/Public/KBasePub.h"
#include "KBase/Public/math/KMathPublic.h"
#include "KBase/Public/io/KByteStream.h"
#include "KBase/Public/io/KMetaData.h"
#include "KEnginePub/Public/IKHeader.h"
#include "Engine/KUniqueString.h"
#include "KEnginePub/Public/IKTexture.h"
#include "../IKShaderreflector.h"
#include "../comm/KGFX_GraphicDevice.h"
#include <string>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace gfx
{
    enum class KCommandBufferStates
    {
        Initial,
        Recording,
        Executable,
        Pending,
        Invalid,
    };

    class KVulkanRenderPass;
    class KVulkanDescriptorPool;
    class KVulkanDescriptorSet;

    struct KDescriptorPoolContainer
    {
        std::set<KVulkanDescriptorSet*> m_setAlloced;
        KVulkanDescriptorPool* m_pDescriptorPool;
        KVulkanDescriptorPool* GetDescriptorPool();
        void AddAlloced(KVulkanDescriptorSet* p);
        void Remove(KVulkanDescriptorSet* p);
        void Clear();
        KDescriptorPoolContainer();
        ~KDescriptorPoolContainer();
    };

    struct KSpecializationMapEntry
    {
        uint32_t uConstantID;
        uint32_t uOffset;
        size_t   size;
    };
    gfx::ShaderStageType GetShaderTypeFromName(const char* pShaderName);

    struct KShaderInfo
    {
        gfx::ShaderStageType     eShaderStage = gfx::ShaderStageType::AllGraphics;
        std::string              strGroupkey;
        std::string              strSpvPath;
        std::string              strScPath;
        KUniqueStr               ustrShaderSource;
        NSKBase::tagFileLocation sIncludedShaderLoc;
        std::string              strShaderDef;
        std::string              strMacro;
        std::string              strEntryPoint = "main";
        uint32_t                 uPushConstantsSize = 0;
        uint32_t                 uPushConstantsOffset = 0;
        uint32_t                 szShaderContentHash = 0;
        int32_t                  nMaxBinding = -1;
        BOOL                     bFromCachedShaderFile = FALSE;

        BOOL MakeGroupKey();
        BOOL MakeSpvPath();
        BOOL MakeScPath();
    };

    struct IKStageSamplerDef
    {
        virtual ~IKStageSamplerDef() {}
        virtual BOOL                SetSamplerDef(const char* pSamplerDef) = 0;
        virtual const char* GetSamplerName() = 0;
        virtual void                SetSamplerName(const char* pName) = 0;
        virtual gfx::KSamplerState* GetSamplerState() = 0;
    };

    class KShaderStage : public KGfxRef
    {
    public:
        virtual BOOL               LoadShader(const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, gfx::ShaderStageType shaderType, gfx::IShaderReflector* pReflector) = 0;
        virtual BOOL               CreateShader(uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector) = 0;
        virtual BOOL               ReCreateShader(uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector) = 0;
        virtual BOOL               SetSpecializationMapEntry(uint32_t uStageSpecializationMapEntryCount, KSpecializationMapEntry pMapEntry[], void* pSpecializationData, uint32_t uSpecializationDataSize) = 0;
        virtual const char* GetShaderName() = 0;
        virtual const char* GetShaderCacheFilePath() = 0;
        virtual uint32_t           GetPushConstantsSize() = 0;
        virtual KShaderInfo* GetShaderInfo() = 0;
        virtual uint32_t           GetSamplerDefCount() = 0;
        virtual IKStageSamplerDef* GetSamplerDef(uint32_t i) = 0;
        virtual IKStageSamplerDef* GetSamplerDef(const char* pcszName) = 0;
        virtual void               ClearSamplerDef() = 0;
        virtual BOOL               AddSamplerDef(const char* pSamplerDef) = 0;
        virtual BOOL               AddSamplerDef(const char* pSamplerDefName, gfx::KSamplerState* pSamplerState) = 0;

        virtual void               SetShaderContent(const char* strMacro, const char* strHeader, const char* strBody) = 0;
        virtual void               SetShaderBody(std::string&& strBody) = 0;
        virtual const std::string& GetShaderContent() = 0;
        virtual const std::string& GetShaderMacro() = 0;
        virtual const std::string& GetShaderHeader() = 0;
        virtual const std::string& GetShaderBody() = 0;
        virtual void               ClearShaderContent() = 0;

        virtual void               SetMaterialID(int id) = 0;
        virtual int                GetMaterialID() = 0;
        virtual void               SetShaderFileLoadFromCache() = 0;
        virtual BOOL               IsShaderFileLoadFromCache() = 0;
        virtual void               SetShaderFileSaveData(std::string&& strData) = 0;
        virtual const std::string& GetShaderFileSaveData() = 0;
        virtual void               SetEntryPoint(const char* szEntryPoint) = 0;
        virtual const char* GetEntryPoint() = 0;
        virtual ~KShaderStage() {}
        virtual void* MoveOutShaderModule() = 0;
    };

    //**********************************************************************************
    // query heap
    //**********************************************************************************
    enum KQueryType
    {
        QUERY_TYPE_TIMESTAMP = 0,
        QUERY_TYPE_PIPELINE_STATISTICS,
        QUERY_TYPE_OCCLUSION,
        QUERY_TYPE_COUNT,
    };

    struct KQueryDesc
    {
        uint32_t uIndex;
    };

    struct KQueryHeapDesc
    {
        KQueryType eType;
        uint32_t   uQueryCount;
        uint32_t   uNodeIndex;
    };

    struct KQueryHeap
    {
        KQueryHeapDesc desc;
    };

    struct KCommandBufferKey
    {
        // KModelRenderGroupKey.id, or any object pointer as key
        uint64_t         id;
        uint32_t         mainCommandId;
        KRenderUsageType renderUsage;
        uint64_t         option;
        int              renderArea[4];
        KCommandBufferKey(uint64_t _id, uint32_t _mainCommandId, KRenderUsageType _renderUsage, uint64_t _option = 0, int renderArea0 = 0, int renderArea1 = 0, int renderArea2 = 0, int renderArea3 = 0)
        {
            id = _id;
            mainCommandId = _mainCommandId;
            renderUsage = _renderUsage;
            option = _option;
            renderArea[0] = renderArea0;
            renderArea[1] = renderArea1;
            renderArea[2] = renderArea2;
            renderArea[3] = renderArea3;
        }
        KCommandBufferKey(const KCommandBufferKey& other)
        {
            id = other.id;
            mainCommandId = other.mainCommandId;
            renderUsage = other.renderUsage;
            option = other.option;
            for (int i = 0; i < 4; i++)
            {
                renderArea[i] = other.renderArea[i];
            }
        }
        inline KCommandBufferKey& operator=(const KCommandBufferKey& other)
        {
            id = other.id;
            renderUsage = other.renderUsage;
            option = other.option;
            for (int i = 0; i < 4; i++)
            {
                renderArea[i] = other.renderArea[i];
            }
            return *this;
        }
        inline bool operator<(const KCommandBufferKey& other) const
        {
            if (id < other.id)
            {
                return true;
            }
            else if (id == other.id && renderUsage < other.renderUsage)
            {
                return true;
            }
            else if (id == other.id && renderUsage == other.renderUsage && mainCommandId < other.mainCommandId)
            {
                return true;
            }
            else if (id == other.id && renderUsage == other.renderUsage && mainCommandId == other.mainCommandId && option < other.option)
            {
                return true;
            }
            else if (id == other.id && renderUsage == other.renderUsage && mainCommandId == other.mainCommandId && option == other.option && (renderArea[0] + renderArea[1] + renderArea[2] + renderArea[3]) < (other.renderArea[0] + other.renderArea[1] + other.renderArea[2] + other.renderArea[3]))
            {
                return true;
            }
            else
            {
                return false;
            }
        };
    };

    class KProgramUniformTexture
    {
    public:
        union {
            struct
            {
                int16_t m_nLayoutBindingVs;
                int16_t m_nLayoutBindingFs;
                int16_t m_nLayoutBindingCs;
                int16_t m_nLayoutBindingGs;
                int16_t m_nLayoutBindingTc;
                int16_t m_nLayoutBindingTe;
            };
            int16_t m_nLayoutBinding[6] = { -1, -1, -1, -1, -1, -1 };
        };

        union {
            struct
            {
                int16_t m_nSpaceVS;
                int16_t m_nSpaceFs;
                int16_t m_nSpaceCs;
                int16_t m_nSpaceGS;
                int16_t m_nSpaceTC;
                int16_t m_nSpaceTE;
            };
            int16_t m_nSpace[6] = { -1, -1, -1, -1, -1, -1 };
        };


        uint32_t             m_uNameHash = 0;
        uint32_t             m_uArrayCount = 0;
        const_pool_str       m_szName = nullptr;
        gfx::enumUniformType m_UniformType;
        TextureType          m_eTextureType = TextureType::Count;

        BOOL Save(KByteBufferStream& byteStream);
        BOOL Load(KByteBufferStream& byteStream);
    };

    class KProgramUniform
    {
    public:
        uint32_t                 m_uNameHash = 0;
        const_pool_str           m_szBlockName;
        const_pool_str           m_szName;
        // gfx::enumShaderStageFlag m_ShaderType;
        gfx::enumUniformType     m_UniformType = (gfx::enumUniformType)0;
        gfx::enumUniformBaseType m_UniformBaseType = (gfx::enumUniformBaseType)0;

        // 如果是vec类型，这里表示有多少个子项
        uint8_t  m_uVectorSize = 0;
        // 如果是矩阵类型，这里多列E
        uint8_t  m_uMatcol = 0;
        // 如果是矩阵类型，这里多行
        uint8_t  m_uMatrow = 0;
        // 如果是数组，这是数组的长度
        uint16_t m_uArrayCount = 0;

        uint32_t m_uByteSize = 0;
        uint16_t m_nOffset = 0;

        KProgramUniform()
        {
            m_uVectorSize = 0;
            m_uMatcol = 0;
            m_uMatrow = 0;
            m_uArrayCount = 0;
            m_nOffset = 0;
            m_szName = nullptr;
            m_szBlockName = nullptr;
        }
        ~KProgramUniform()
        {
        }

        BOOL Save(KByteBufferStream& byteStream);

        BOOL Load(KByteBufferStream& byteStream);
    };

    struct KProgramUniformComparetor
    {
        bool operator()(const gfx::KProgramUniform* a, const gfx::KProgramUniform* b) const
        {
            if (a->m_nOffset < b->m_nOffset)
                return true;
            else
                return false;
        }
    };

    struct KPushContantsRangeMap
    {
        gfx::ShaderStageType shadertype;
        uint32_t             offset;
        uint32_t             toRange;
        KPushContantsRangeMap()
        {
            shadertype = gfx::ShaderStageType::AllGraphics;
            offset = 0;
            toRange = 0;
        }
        BOOL Save(KByteBufferStream& byteStream) const;

        BOOL Load(KByteBufferStream& byteStream);
    };

    class KProgramUniformBlock
    {
    public:
        const_pool_str m_szName;

        union {
            struct
            {
                int16_t m_nLayoutBindingVs;
                int16_t m_nLayoutBindingFs;
                int16_t m_nLayoutBindingCs;
                int16_t m_nLayoutBindingGs;
                int16_t m_nLayoutBindingTc;
                int16_t m_nLayoutBindingTe;
            };
            int16_t m_nLayoutBinding[6] = { -1, -1, -1, -1, -1, -1 };
        };

        union {
            struct
            {
                int16_t m_nSpaceVS;
                int16_t m_nSpaceFS;
                int16_t m_nSpaceCS;
                int16_t m_nSpaceGS;
                int16_t m_nSpaceTC;
                int16_t m_nSpaceTE;
            };
            int16_t m_nSpace[6] = { -1, -1, -1, -1, -1, -1 };
        };

        uint32_t m_block16bytesAlignMemoryForGpu = 0;

        union {
            struct
            {
                int16_t m_n32BitValuesVs;
                int16_t m_n32BitValuesFs;
                int16_t m_n32BitValuesCs;
                int16_t m_n32BitValuesGs;
                int16_t m_n32BitValuesTc;
                int16_t m_n32BitValuesTe;
            };
            int16_t m_n32BitValues[6] = { -1, -1, -1, -1, -1, -1 };
        };
        gfx::enumUniformType  m_UniformType;
        uint32_t m_UniformScopeType;
        uint32_t m_globalUboId;
        KProgramUniformBlock();
        ~KProgramUniformBlock();
        BOOL                                                            Save(KByteBufferStream& byteStream);
        BOOL                                                            Load(KByteBufferStream& byteStream);
        std::set<gfx::KProgramUniform*, gfx::KProgramUniformComparetor> m_Uniforms;
        std::vector<gfx::KPushContantsRangeMap>                         m_vecPushConstantsRangeMap;
    };

    ///[[deprecated("KGFX_ProgramUniformSampler")]]
    class KProgramUniformSampler
    {
    public:
        KProgramUniformSampler()
        {
            m_SamplerState.bNeedShaderInit = true;
        }
        virtual ~KProgramUniformSampler();
        // static gfx::enumShaderStageFlag shaderTypes[6] = {
        //     gfx::SHADER_STAGE_VERTEX_BIT,
        //     gfx::SHADER_STAGE_FRAGMENT_BIT,
        //     gfx::SHADER_STAGE_COMPUTE_BIT,
        //     gfx::SHADER_STAGE_GEOMETRY_BIT,
        //     gfx::SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        //     gfx::SHADER_STAGE_TESSELLATION_EVALUATION_BIT
        // };

    public:
        union {
            struct
            {
                int16_t m_nLayoutBindingVs;
                int16_t m_nLayoutBindingFs;
                int16_t m_nLayoutBindingCs;
                int16_t m_nLayoutBindingGs;
                int16_t m_nLayoutBindingTc;
                int16_t m_nLayoutBindingTe;
            };
            int16_t m_nLayoutBinding[6] = { -1, -1, -1, -1, -1, -1 };
        };

        union {
            struct
            {
                int16_t m_nSpaceVS;
                int16_t m_nSpaceFs;
                int16_t m_nSpaceCs;
                int16_t m_nSpaceGS;
                int16_t m_nSpaceTC;
                int16_t m_nSpaceTE;
            };
            int16_t m_nSpace[6] = { -1, -1, -1, -1, -1, -1 };
        };

        uint32_t             m_uNameHash = 0;
        const_pool_str       m_szName = nullptr;
        gfx::enumUniformType m_UniformType = gfx::UBO_UNIFORM;
        gfx::IKGFX_Sampler* m_pSampler = nullptr;
        gfx::KSamplerState   m_SamplerState;

        //BOOL Save(KByteBufferStream& byteStream);
        //BOOL Load(KByteBufferStream& byteStream, BOOL bByBuildTool);
    };
}
