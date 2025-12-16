#pragma once
#ifdef _WIN32
#include <filesystem>
#include "KGFX_RefPtr.h"
#include "KGFX_DX12Header.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <d3d12shader.h>
#include "KGFX_Dx12Healper.h"
#include "robin-hood-hashing-3.11.5/include/robin_hood.h"
namespace gfx
{
    class KGFX_ShaderReflectorDx12;
    class KGFX_ShaderFile;
    /// <summary>
    /// 材质使用的CBUF的固定的名字
    /// </summary>
    constexpr const char* PER_MTL_UBO_NAME_DX12 = ("MaterialLocalParams");

    /// <summary>
    /// pushConst使用的固定的名字
    /// </summary>
    constexpr const char* PUSH_CONSTANTS_UBO_NAME_DX12 = ("cbufPushConsts");

    /// <summary>
    /// 是specializationConst使用的固定的名字
    /// </summary>
    constexpr const char* SPECIAL_CONST_NAME_DX12 = ("SpecializationConsts");

    constexpr uint32_t PUSH_CONST_REGISTER_DX12 = 12; // 64 bytes

    constexpr uint32_t SPECIAL_CONST_REGISTER_DX12 = 13; // 64 bytes
    /**
     *
     */
    class KGFX_ShaderTechItemDX12
    {
    public:
        KGFX_ShaderTechItemDX12() = default;
        ~KGFX_ShaderTechItemDX12();

        KGFX_ShaderTechItemDX12(const KGFX_ShaderTechItemDX12& other) = delete;
        KGFX_ShaderTechItemDX12& operator=(const KGFX_ShaderTechItemDX12& other) = delete;
        KGFX_ShaderTechItemDX12(KGFX_ShaderTechItemDX12&& other) noexcept = delete;
        KGFX_ShaderTechItemDX12& operator=(KGFX_ShaderTechItemDX12&& other) noexcept = delete;

        std::filesystem::path m_UserShaderPath = {};
        std::filesystem::path m_MainShaderPath = {};
        std::unordered_map<std::string, std::string> m_TechMacroDXC = {};
        std::string m_EntryPoint = {};
        std::string m_Key = {};
        gfx::ShaderStageType m_ShageStage = {};
        KGFX_ShaderFile* m_pMainShaderFile = nullptr;

        /**
         * 会从文本池中查询对应的文件是否存在，存在的话不会触发读取行为的
         * @return
         */
        bool LoadShader();
    };



    struct KGFX_ShaderUniformBlockDX12
    {
        const_pool_str m_szName = nullptr;
        uint32_t m_block16bytesAlignMemoryForGpu = 0;
        ShaderOffset m_CBufBinds = {};
        gfx::enumUniformType  m_UniformType = {};
        uint32_t m_uArrayCount = 1;
    };

    struct KGFX_ShaderUniformTextureDX12
    {
        const_pool_str       m_szName = nullptr;
        uint32_t             m_uArrayCount = 0;
        ShaderOffset m_TextureBinds = {};
        gfx::enumUniformType m_UniformType = {};
        TextureType          m_eTextureType = TextureType::Count;
    };

    struct KGFX_ShaderUniformSamplerDX12
    {
        const_pool_str m_szName = nullptr;
        ShaderOffset m_SamplerBinds = {};
    };

    struct KGFX_ShaderAccelerationStructureDX12
    {
        const_pool_str m_szName = nullptr;
        ShaderOffset m_AccelerationStructureBinds = {};
    };

    struct KGFX_ProgramUniformBlockDX12
    {
        KGFX_ShaderUniformBlockDX12* m_pCBVStageRefl = nullptr;
        std::array<ShaderOffset, gfx::CalculateGraphicsAndComputeShaderStageTypeCount()> m_CBufBinds = {};
        const_pool_str GetName() const
        {
            if (m_pCBVStageRefl)
            {
                return m_pCBVStageRefl->m_szName;
            }
            assert(false);
            return nullptr;
        }

        gfx::enumUniformType GetType() const
        {
            if (m_pCBVStageRefl)
            {
                return m_pCBVStageRefl->m_UniformType;
            }
            assert(false);
            return {};
        }

        int GetBufArrSize() const
        {
            if (m_pCBVStageRefl)
            {
                return m_pCBVStageRefl->m_uArrayCount;
            }
            assert(false);
            return {};
        }

        int GetBufSize() const
        {
            return m_pCBVStageRefl->m_block16bytesAlignMemoryForGpu;
        }
    };

    struct KGFX_ProgramUniformTextureDX12
    {
        KGFX_ShaderUniformTextureDX12* m_pResStageRefl = nullptr;
        std::array<ShaderOffset, gfx::CalculateGraphicsAndComputeShaderStageTypeCount()> m_TexBinds = {};
        inline const_pool_str GetName() const
        {
            if (m_pResStageRefl)
            {
                return m_pResStageRefl->m_szName;
            }
            return nullptr;
        }

        gfx::enumUniformType GetType() const
        {
            if (m_pResStageRefl)
            {
                return m_pResStageRefl->m_UniformType;
            }
            assert(false);
            return {};
        }

        int GetTexArrSize() const
        {
            if (m_pResStageRefl)
            {
                return m_pResStageRefl->m_uArrayCount;
            }
            assert(false);
            return {};
        }
    };

    struct KGFX_ProgramUniformSamplerDX12
    {
        KGFX_ShaderUniformSamplerDX12* m_pSamplerStageRefl = nullptr;
        std::array<ShaderOffset, gfx::CalculateGraphicsAndComputeShaderStageTypeCount()> m_SamplerBinds = {};
        inline const_pool_str GetName() const
        {
            if (m_pSamplerStageRefl)
            {
                return m_pSamplerStageRefl->m_szName;
            }
            return nullptr;
        }
    };

    struct KGFX_ProgramUniformAccelerationStructureDX12
    {
        KGFX_ShaderAccelerationStructureDX12* m_pAccelerationStructureStageRefl = nullptr;
        std::array<ShaderOffset, gfx::CalculateGraphicsAndComputeShaderStageTypeCount()> m_AccelerationStructureBinds = {};
        inline const_pool_str GetName() const
        {
            if (m_pAccelerationStructureStageRefl)
            {
                return m_pAccelerationStructureStageRefl->m_szName;
            }
            return nullptr;
        }
    };

    struct UBOParamItem
    {
        const char* szName;
        uint32_t m_uOffset;
        uint32_t m_uByteSize;
    };

    struct KGFX_ProgramSpecialConstDX12
    {
        using SpecialConstReflPtr = std::map<const_pool_str, UBOParamItem>*;
        SpecialConstReflPtr m_SpecialConstBinds = {};
        uint32_t m_SpecialConstSize = {};
    };

    struct KGFX_ProgramMtlConstDX12
    {
        using SpecialConstReflPtr = std::map<const_pool_str, UBOParamItem>*;
        SpecialConstReflPtr m_MtlConstBinds = {};
    };

    struct KGFX_ProgramPushConstDX12
    {
        uint32_t m_uPushConstBufferSize = 0;
    };

    /**
     * 用来记录每个satge的shader反射结果
     * 不持有任何需要额外释放的资源
     */
    class KGFX_ShaderReflectorDx12
    {
        friend class KGFX_ProgramBinderDx12;
        friend class KGFX_GraphicsProgramDx12;
        friend class KGFX_ComputeProgramDx12;
        friend class KGFX_ShaderDx12;
        friend class KGFX_ProgramReflectorDx12;
    public:
        KGFX_ShaderReflectorDx12();
        ~KGFX_ShaderReflectorDx12();
        KGFX_ShaderReflectorDx12(const KGFX_ShaderReflectorDx12& other) = delete;
        KGFX_ShaderReflectorDx12& operator=(const KGFX_ShaderReflectorDx12& other) = delete;

        KGFX_ShaderReflectorDx12(KGFX_ShaderReflectorDx12&& other) noexcept = delete;
        KGFX_ShaderReflectorDx12& operator=(KGFX_ShaderReflectorDx12&& other) noexcept = delete;

        BOOL BuildReflection(void* pProgram, ShaderStageType shaderType);

    private:
        BOOL ParseVertexAttribute(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc);
        bool ParseCBuffer(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc);
        bool ParseUAV(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc);
        bool ParseTexture(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc);
        bool ParseSampler(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc);
        bool ParseAccelerationStructure(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc);
        void Clear();

        //反射出来的 shader里面数据结构
        std::vector<RefPtr<KProgramAttribute>> m_vecAttribute = {};
        std::vector<RefPtr<KGFX_ShaderUniformBlockDX12>> m_vecUniformBlock = {};
        std::vector<RefPtr<KGFX_ShaderUniformTextureDX12>> m_vecUniformTexture = {};
        std::vector<RefPtr<KGFX_ShaderUniformSamplerDX12>> m_vecUniformSampler = {};
        std::vector<RefPtr<KGFX_ShaderAccelerationStructureDX12>> m_vecAccelerationStructure = {};

        KProgramAttribute* GetAttribute(const_pool_str szName) const;
        KGFX_ShaderUniformBlockDX12* GetBufRefl(const_pool_str szName) const;
        KGFX_ShaderUniformTextureDX12* GetTexRefl(const_pool_str szName) const;
        KGFX_ShaderUniformSamplerDX12* GetSamplerRefl(const_pool_str szName) const;
        KGFX_ShaderAccelerationStructureDX12* GetAccelerationStructureRefl(const_pool_str szName) const;

        /**
         * 材质的CBUF
         */
        uint32_t m_uMtlBufferSize = 0;
        std::map<const_pool_str, UBOParamItem> m_mapMtlUBOParamMapping = {};

        uint32_t m_uSpecializationConstBufferSize = 0;
        std::map<const_pool_str, UBOParamItem> m_mapSpecializationConstsParamMapping = {};

        uint32_t m_uPushConstBufferSize = 0;
        std::map<const_pool_str, UBOParamItem> m_mapPushConstsParamMapping = {};
        ShaderStageType m_ShaderStage = {};
    };

    /**
     * 单个stage的shader的编译结果和反射结果
     * 会缓存进池子
     */
    class KGFX_ShaderDx12 final : public KGfxRef
    {
        friend class KGFX_ProgramReflectorDx12;
    public:
        KGFX_ShaderDx12();
        ~KGFX_ShaderDx12() override;

        BOOL LoadShaderDXC(ShaderStageType eShaderStage, const char* key, const char* ShaderPath, const char* szEntryName,
            const KUniqueStr& sUserShaderPath, const char* szUserDefMacro, const char* szFileDefMacro);

        int32_t AddRef() override;

        int32_t GetRef() override;

        int32_t Release() override;

        const char* GetKey() const;

        void SetKey(const char* pKey);

        ShaderStageType GetShaderStage() const;

        ID3DBlob* GetCompliledShader() const;

        uint64_t GetHashCode() const;

        KGFX_ShaderReflectorDx12* GetShaderRefl();
    private:
        BOOL ComplieDXC();

        std::string m_szKey = {};
        /**
         * 这个似乎可以编译完就释放，节约内存
         */
        KGFX_ShaderTechItemDX12 m_pShaderTechItem = {};
        ID3DBlob* m_pCompiledShader = nullptr;
        RefPtr<KGFX_ShaderReflectorDx12> m_pReflector = nullptr;
        uint64_t m_uHashCode = 0;
    };

    struct ShaderResourceVisibleHash
    {
        size_t operator()(const ShaderResourceVisible& key) const
        {
            size_t hash = 0;
            HashCombine(hash, (uint32_t)key.slotType);
            HashCombine(hash, key.slot.bindingSpaceIndex);
            HashCombine(hash, key.slot.bindingSlotIndex);
            HashCombine(hash, (uint32_t)key.stageType);
            return hash;
        }
    };

    /**
     * 用来记录整个pipeline的反射结果，会包含材质的cbuf的创建
     * 持有多个stage的shader的引用计数
     */
    class KGFX_ProgramReflectorDx12 :public KGfxRef, public KGFX_DelayReleaseObject
    {
    public:
        struct BindIndex
        {
            enum BindType :uint8_t
            {
                Normal = 0,
                PushConst = 1,
                SpecialConst = 2,
            };


            union SlotUnion
            {
                uint8_t slots[8];
                uint64_t Slot = std::numeric_limits<uint64_t>::max();
            };

            SlotUnion index = {};

            void SetSlot(int stage, uint8_t value)
            {
                assert(stage < 8);
                assert(value < std::numeric_limits<uint8_t>::max());
                index.slots[stage] = value;
            }

            uint8_t GetSlot(int stage) const
            {
                assert(stage < 8);
                return index.slots[stage];
            }

            uint8_t& operator[](int stage)
            {
                assert(stage < 8);
                return index.slots[stage];
            }

            uint8_t GetCount() const
            {
                return index.slots[7];
            }

            BindType GetBindType()
            {
                return static_cast<BindType>(index.slots[6]);
            }

            uint8_t operator[](int stage) const
            {
                assert(stage < 8);
                return index.slots[stage];
            }


            bool operator==(const BindIndex& other) const noexcept
            {
                return index.Slot == other.index.Slot;
            }

            bool operator!=(const BindIndex& other) const noexcept
            {
                return !(*this == other);
            }

            bool operator()(int stage) const
            {
                return index.slots[stage] != std::numeric_limits<uint8_t>::max();
            }
        };


        using ShaderNameToSlot = std::unordered_map<const_pool_str, ShaderResourceVisible>;
        KGFX_ProgramReflectorDx12() = default;
        ~KGFX_ProgramReflectorDx12() override;
        KGFX_ProgramReflectorDx12(const KGFX_ProgramReflectorDx12& other) = delete;
        KGFX_ProgramReflectorDx12& operator=(const KGFX_ProgramReflectorDx12& other) = delete;
        KGFX_ProgramReflectorDx12(KGFX_ProgramReflectorDx12&& other) noexcept = delete;
        KGFX_ProgramReflectorDx12& operator=(KGFX_ProgramReflectorDx12&& other) noexcept = delete;

        int32_t Release() override;

        void Init(std::vector<KGFX_ShaderDx12*>& vecShaderStages);

        void Reset();

        void SetKey(std::string_view szkey);

        const std::string& GetKey();

        bool CombineAllStageRefl();

        const std::vector<KGFX_ShaderDx12*>& GetShaderCode();

        std::vector<KGFX_ProgramUniformBlockDX12>& GetAllBufRefl();

        std::vector<KGFX_ProgramUniformTextureDX12>& GetAllResRefl();

        std::vector<KGFX_ProgramUniformSamplerDX12>& GetAllSamplerRefl();

        std::vector< KGFX_ProgramUniformAccelerationStructureDX12>& GetAllAccelerationStructureRefl();

        const KGFX_ProgramSpecialConstDX12& GetSpecialConstRefl() const;

        const KGFX_ProgramMtlConstDX12& GetMtlConstRefl() const;

        IKGFX_ConstBuffer* GetMtlCbuf() const;

        ID3D12RootSignature* GetRootSignature() const;

        uint64_t GetRootSignatureHash() const;

        bool SetPerMtlValue(const char* name, const void* data, uint32_t size);

        bool BuildRootSignature();

        const std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& GetShaderStageSRVCount() const;
        const std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& GetShaderStageUAVCount() const;
        const std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& GetShaderStageSampleCount() const;
        const std::array<unsigned short, 6>& GetShaderStageSRVBaseIndex() const;
        const std::array<unsigned short, 6>& GetShaderStageUAVBaseIndex() const;
        const std::array<unsigned short, 6>& GetShaderStageSamplerIndex() const;

        const KGFX_ProgramPushConstDX12& GetPushConstRefl() const;

        int GetSRVStageStartIndex(ShaderStageType eStage) const;

        int GetSRVStageBaseIndex(ShaderStageType eStage) const;

        int GetUAVStageStartIndex(ShaderStageType eStage) const;

        int GetUAVStageBaseIndex(ShaderStageType eStage) const;

        int GetSamplerStageStartIndex(ShaderStageType eStage) const;

        int GetSamplerStageBaseIndex(ShaderStageType eStage) const;

    public:
        bool m_bMtlCbufNeedUpdate = false;
        int m_ShaderRootConstBufferCount = 0;

        struct const_pool_str_hash
        {
            size_t operator()(const_pool_str key) const noexcept
            {
                uintptr_t x = reinterpret_cast<uintptr_t>(key);
                // 64-bit finalizer (MurmurHash3 mix)
                x ^= x >> 33;
                x *= 0xff51afd7ed558ccdULL;
                x ^= x >> 33;
                x *= 0xc4ceb9fe1a85ec53ULL;
                x ^= x >> 33;
                return static_cast<size_t>(x);
            }
        };

        struct const_pool_str_hash_equal
        {
            bool operator()(const_pool_str key1, const_pool_str key2) const
            {
                return reinterpret_cast<uint64_t>(key1) == reinterpret_cast<uint64_t>(key2);
            }
        };

        std::unordered_map<const_pool_str, BindIndex> m_ShaderNameToCPUBind = {};
        std::unordered_map<const_pool_str, BindIndex> m_ShaderNameToCPUSampler = {};
        std::unordered_map<const_pool_str, BindIndex> m_ShaderRootCBV = {};
        std::vector<int> m_CPUBindToDescriptorTableOffset = {};
        std::vector<int> m_SamplerBindToDescriptorTableOffset = {};

        uint32_t m_CbufCount = 0;
        uint32_t m_SRVCount = 0;
        uint32_t m_UAVCount = 0;
        uint32_t m_SamplerCount = 0;

        /// 反射信息的解析也需要放到这里来
        /// <summary>
        /// 用于记录shader中资源的名字和slot的映射关系
        /// shader编译反射之后就固定了
        /// </summary>
        std::array<ShaderNameToSlot, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderNameToSlot = {};
    private:


        std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderStageSRVCount = {};
        std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderStageUAVCount = {};
        std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderStageSampleCount = {};

        std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderStageSRVBaseIndex = {};
        std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderStageUAVBaseIndex = {};
        std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()> m_ShaderStageSamplerIndex = {};


        IKGFX_ConstBuffer* m_pMtlCbuf = nullptr;
        inline static std::unordered_map<uint64_t, ID3D12RootSignature*> m_RootSignatureHashTable{};
        std::vector<KGFX_ShaderDx12*> m_vecShaderStages = {};
        ID3D12RootSignature* m_RootSignature = nullptr;
        uint64_t m_uRootSignatureHash = 0;
        std::string m_szKey = {};

        std::vector<KGFX_ProgramUniformBlockDX12> m_vecUniformBlock = {};
        std::vector<KGFX_ProgramUniformTextureDX12> m_vecUniformTexture = {};
        std::vector<KGFX_ProgramUniformSamplerDX12> m_vecUniformSampler = {};
        std::vector<KGFX_ProgramUniformAccelerationStructureDX12> m_vecUniformAccelerationStructure = {};
        KGFX_ProgramSpecialConstDX12 m_SpecialConstRefl = {};
        KGFX_ProgramMtlConstDX12 m_MtlConst = {};
        KGFX_ProgramPushConstDX12 m_PushConst = {};
    };


    class KGFX_ShaderResourcePoolDx12
    {
    public:
        KGFX_ShaderResourcePoolDx12();
        ~KGFX_ShaderResourcePoolDx12();
        KGFX_ShaderResourcePoolDx12(const KGFX_ShaderResourcePoolDx12& other) = delete;
        KGFX_ShaderResourcePoolDx12& operator=(const KGFX_ShaderResourcePoolDx12& other) = delete;
        KGFX_ShaderResourcePoolDx12(KGFX_ShaderResourcePoolDx12&& other) noexcept = delete;
        KGFX_ShaderResourcePoolDx12& operator=(KGFX_ShaderResourcePoolDx12&& other) noexcept = delete;

        bool RequestFromTechFileDXC(
            const char* szTechFilePathName,
            const char* szTechName,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char* szUserDefMacro,
            KGFX_ProgramReflectorDx12** pReflector
        );

        BOOL RemoveShader(const std::string& pKey);
        BOOL RemoveProgram(const std::string& pKey);
        std::mutex m_PoolLock;
    private:
        std::string GetShaderKey(
            ShaderStageType eShaderStage,
            const char* szShaderFilePath,
            const char* szEntryPointName,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char* szUserDefMacro,
            const char* szFileDefMacro
        ) const;


        std::string GetTechKey(const char* szTechFilePathName, const char* szTechName, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro) const;

        std::mutex m_shaderLock = {};
        /**
         * 这个存储每个stage对应shader的编译结果和每个shader的反射结果
         */
        std::unordered_map<std::string, KGFX_ShaderDx12*> m_mapShader = {};

        /**
         * 这个存储将shader组装成为一个完整的pipeline的反射结果，这个会持有m_mapShader中的引用计数，需要先释放
         */
        std::unordered_map<std::string, KGFX_ProgramReflectorDx12*> m_mapTechRefl = {};
    };

    void KGFX_CreateShaderPoolDx12();
    void KGFX_DestroyShaderPoolDx12();
    KGFX_ShaderResourcePoolDx12* KGFX_GetShaderPoolDx12();
};

#endif
