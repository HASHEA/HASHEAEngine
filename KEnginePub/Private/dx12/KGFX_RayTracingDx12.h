#pragma once
#include "KEnginePub/Public/IGFX_Public.h"
#include "KGFX_Dx12Header.h"
#include "KGFX_DescriptorDX12.h"
#include "KGFX_ShaderResourceDx12.h"
#include <vector>
#include <unordered_map>
#include <d3d12shader.h>

namespace gfx
{
    class RayTracingBindingTable;
    class RayTracingShaderDx12;
    class RayTracingRootSignature;
    class RayTracingShaderBindingTableDx12;

    struct RayTracingShaderIdentifier
    {
        uint64_t Data[4] = { ~0ull, ~0ull, ~0ull, ~0ull };

        static const RayTracingShaderIdentifier Null;

        bool operator==(const RayTracingShaderIdentifier& Other)const
        {
            return Data[0] == Other.Data[0]
                && Data[1] == Other.Data[1]
                && Data[2] == Other.Data[2]
                && Data[3] == Other.Data[3];
        }

        bool operator!=(const RayTracingShaderIdentifier& Other)const
        {
            return !(*this == Other);
        }

        bool IsValid()const
        {
            return *this != RayTracingShaderIdentifier();
        }

        void SetData(const void* pData)
        {
            memcpy(Data, pData, sizeof(Data));
        }
    };

    struct RayTracingBindings
    {
        uint32_t ConstanBuffers = 0;
        uint32_t ShaderResourceViews = 0;
        uint32_t UnorderedResourceViews = 0;
        uint32_t Samplers = 0;
        uint32_t ShaderResourceStartIndex = 0;
        uint32_t UnorderedResourceStartIndex = 0;
        uint32_t SamplersStartIndex = 0;
    };

    enum class RayTracingBindingType
    {
        Global,
        Local
    };

    struct RayTracingSignatureDesc
    {
        RayTracingBindings Binding;
        RayTracingBindingType Type;
    };

    class RayTracingRootSignature
    {
    public:
        RayTracingRootSignature() = default;
        ~RayTracingRootSignature();

        ID3D12RootSignature* GetDeviceRootSignature() { return m_pRootSignature; }
        bool Init(const RayTracingSignatureDesc& Desc);
        void Destroy();

        uint32_t SRVBindSlot() { return m_ShaderResourceViewSlot; }
        uint32_t UAVBindSlot() { return m_ShaderResourceViewSlot; }
        uint32_t CBVBindSlot() { return m_ConstantBufferSlot; }
        uint32_t SamplerBindSlot() { return m_SamplerViewSlot; }

    private:
        ID3D12RootSignature* m_pRootSignature = nullptr;
        uint32_t m_RootSignatureSize = 0;
        uint32_t m_ConstantBufferSlot = 0;
        uint32_t m_ShaderResourceViewSlot = 0;
        uint32_t m_UnorderedResourceViewSlot = 0;
        uint32_t m_SamplerViewSlot = 0;
    };

    class RayTracingDescriptorCacheDx12
    {
    public:
        RayTracingDescriptorCacheDx12() = default;
        ~RayTracingDescriptorCacheDx12();

        void SetGPUDescriptorHeap(DescriptorHeapReference ResourceHeap);
        uint32_t Allocate(uint32_t NumDescriptors);
        uint32_t AllocateDescriptorTable(const D3D12_CPU_DESCRIPTOR_HANDLE* pDescriptors, uint32_t NumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type);
        void CopyDescriptors(uint32_t BaseIndex, const D3D12_CPU_DESCRIPTOR_HANDLE* pDescriptors, uint32_t NumDescriptors);

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_CPUBaseHandle;
        DescriptorTable m_ResourceTable;
    };

    class RayTracingPipelineCacheDx12
    {
    public:
        RayTracingPipelineCacheDx12();
        ~RayTracingPipelineCacheDx12();

        bool Init();

        struct Key
        {
            uint64_t ShaderHash = 0;
            uint32_t MaxAttributeSizeInBytes = 0;
            uint32_t MaxPayloadSizeInBytes = 0;
            ID3D12RootSignature* pLocalRootSignature = nullptr;
            ID3D12RootSignature* pGlobalRootSignature = nullptr;

            bool operator==(const Key& Other)const
            {
                return ShaderHash == Other.ShaderHash
                    && MaxAttributeSizeInBytes == Other.MaxAttributeSizeInBytes
                    && MaxPayloadSizeInBytes == Other.MaxPayloadSizeInBytes
                    && pLocalRootSignature == Other.pLocalRootSignature
                    && pGlobalRootSignature == Other.pGlobalRootSignature;             
            }
        };

        struct KeyHash
        {
            size_t operator()(const Key& key)const
            {
                return key.ShaderHash;
            }
        };

        enum class CollectionType
        {
            Unknown,
            RayGen,
            Miss,
            HitGroup,
            Callable
        };

        struct Entry
        {
            Entry() = default;
            ~Entry();
            Entry(Entry&& Other) = default;

            Entry(const Entry&) = delete;
            Entry& operator=(const Entry&) = delete;
            Entry& operator=(Entry&&) = delete;

            const LPCWSTR GetPrimaryExportName()
            {
                return vecExportNames[0].c_str();
            }

            D3D12_EXISTING_COLLECTION_DESC GetCollectionDesc()
            {
                D3D12_EXISTING_COLLECTION_DESC ResultCollectionDesc{};
                ResultCollectionDesc.pExistingCollection = pStateObject;
                return ResultCollectionDesc;
            }

            RayTracingShaderDx12* pShader = nullptr;
            CollectionType Type = CollectionType::Unknown;
            static constexpr uint32_t MaxExport = 4;
            std::vector<std::wstring> vecExportNames;
            RayTracingShaderIdentifier ShaderIdentifier;
            ID3D12StateObject* pStateObject = nullptr;
        };

        void CompileTask(Entry& InEntry, Key InKey);
        Entry* GetOrCompileShaderToStateObject(RayTracingShaderDx12* pRayTracingShader, ID3D12RootSignature* pGlobalRootSignature, uint32_t MaxAttributeSizeInBytes, uint32_t MaxPayloadSizeInBytes, CollectionType Type);     

    private:
        std::unordered_map<Key, Entry*, KeyHash> m_mapCaches;
        ID3D12RootSignature* m_pDefaultRootSignature = nullptr;
    };

    struct RayTracingPipelineStateInitializer
    {
        std::vector< IRayTracingShader*> vecRayGenShaders;
        std::vector< IRayTracingShader*> vecMissShaders;
        std::vector< IRayTracingShader*> vecHitGroupShaders;
        std::vector< IRayTracingShader*> vecCallableShaders;
        uint32_t MaxAttributeSizeInBytes;
        uint32_t MaxPayloadSizeInBytes;
    };

    class RayTracingShaderReflector
    {
        friend class RayTracingShaderDx12;
        friend class RayTracingPipelineStateDx12;
    public:
        struct ParamItem
        {
            uint32_t Offset;
            uint32_t Size;
        };

        RayTracingShaderReflector() = default;
        ~RayTracingShaderReflector();

        bool BuildReflection(void* pProgram, ShaderStageType ShaderType);

    private:
        bool ParseCBuffer(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc);
        bool ParseTexture(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc);
        bool ParseUAV(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc);
        bool ParseSampler(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc);
        void CombineResourceToIndex();

        KGFX_ShaderUniformBlockDX12* GetUniformBlock(const_pool_str Name);
        KGFX_ShaderUniformTextureDX12* GetUniformTextures(const_pool_str Name);
        KGFX_ShaderUniformSamplerDX12* GetUniformSamplers(const_pool_str Name);

    private:
        std::map< const_pool_str, ParamItem> m_mapMaterialConstant;
        std::map< const_pool_str, ParamItem> m_mapEngineBindlessIndexConstant;
        std::map< const_pool_str, ParamItem> m_mapMaterialBindlessIndexConstant;
        std::map< const_pool_str, ParamItem> m_mapCommonBindlessIndexConstant;
        std::map< const_pool_str, uint32_t> m_mapResourceNameToIndex;
        KRayTracingUniformBlockInfo* m_MaterialUniformBlockInfo = nullptr;
        KRayTracingUniformBlockInfo* m_MaterialLocalUniformBlockInfo = nullptr;
        KRayTracingUniformBlockInfo* m_EngineLocalUniformBlockInfo = nullptr;
        KRayTracingUniformBlockInfo* m_CommonUniformBlockInfo = nullptr;
        std::vector<KGFX_ShaderUniformBlockDX12*> m_vecUniformBlocks = {};
        std::vector<KGFX_ShaderUniformTextureDX12*> m_vecUniformTextures = {};
        std::vector<KGFX_ShaderUniformSamplerDX12*> m_vecUniformSamplers = {};
        uint32_t m_MaterialBindlessIndexConstantBufferSize = 0;
        uint32_t m_CommonBindlessIndexConstantBufferSize = 0;
        uint32_t m_MaterialConstantBufferSize = 0;
        uint32_t m_EngineBindlessIndexConstantBufferSize = 0;
        uint8_t m_BindingSpace = 0;
        RayTracingBindingType m_BindingType = RayTracingBindingType::Global;
        RayTracingBindings m_Bindings = {};
    };

    class RayTracingShaderDx12 : public IRayTracingShader
    {
        friend class RayTracingPipelineStateDx12;
    public:
        RayTracingShaderDx12() = default;
        virtual ~RayTracingShaderDx12();
        virtual bool Create(const KRayTracingShaderCreateDesc& ShaderCreateDesc);
        virtual uint64_t GetHash();
        virtual enumRayTracingShaderType GetType()const;
        virtual KRayTracingUniformBlockInfo* GetLocalMaterialBindlessIDUniformBlockInfo();
        virtual KRayTracingUniformBlockInfo* GetCommonUniformBlockInfo();
        virtual KRayTracingUniformBlockInfo* GetLocalEngineBindlessIDUniformBlockInfo();
        virtual KRayTracingUniformBlockInfo* GetLocalMaterialParamUniformBlockInfo();

    public:
        void* GetShaderByteCode() { return m_pCompiledShader->GetBufferPointer(); }
        uint32_t GetShaderCodeSize() { return (uint32_t)m_pCompiledShader->GetBufferSize(); }
        RayTracingRootSignature* GetRootSignature(){ return m_pRootSigature; }

        std::wstring m_EntryPoint;
        std::wstring m_AnyHitEntryPoint;
        std::wstring m_IntersectionEntryPoint;
        enumRayTracingShaderType m_ShaderType;
        uint64_t m_Hash;

    private:
        bool LoadRayTracingShader(ShaderStageType eShaderStage, const char* szShaderPath, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro, const char* szFileDefMacro);
        bool CompileDXC(KGFX_ShaderTechItemDX12* pShaderTechItem);

    private:
        ID3DBlob* m_pCompiledShader = nullptr;
        uint64_t m_HashCode = 0;
        RayTracingShaderReflector* m_pReflector = nullptr;
        RayTracingRootSignature* m_pRootSigature = nullptr;
    };

    struct RayTracingShaderDx12Library
    {
        std::vector< RayTracingShaderDx12*> vecRayTracingShaders;
        std::vector< RayTracingShaderIdentifier> vecRayTracingShaderIdentifiers;
    };

    class RayTracingPipelineStateDx12 : public KRayTracingProgram
    {
    public:
        struct BindInfoCache
        {
            uint32_t StarIndex = 0;
            uint32_t ResourceCount = 0;
            uint64_t Hash = 0;

            bool operator==(const BindInfoCache& Other)
            {
                return Other.ResourceCount == ResourceCount && Other.Hash == Hash;
            }

            bool operator!=(const BindInfoCache& Other)
            {
                return !(*this == Other);
            }
        };

        struct BindlessBindInfo
        {
            std::vector<uint64_t> vecHandles;
            std::vector<uint32_t> vecBindlessIndexs;
            BindlessHeapType Type;

            BindlessBindInfo(IKGFX_BufferView* pBufferView)
            {
                vecHandles.push_back(pBufferView->GetNativeHandle());
                vecBindlessIndexs.push_back(pBufferView->GetBindlessHandle());
                Type = BindlessHeapType::Standard;
            }

            BindlessBindInfo(IKGFX_BufferView** ppBufferView, uint32_t NumBuffer)
            {
                for (uint32_t i = 0; i < NumBuffer; i++)
                {
                    vecHandles.push_back((*ppBufferView + i)->GetNativeHandle());
                    vecBindlessIndexs.push_back((*ppBufferView + i)->GetBindlessHandle());
                }
                Type = BindlessHeapType::Standard;
            }

            BindlessBindInfo(IKGFX_TextureView* pTextureView)
            {
                vecHandles.push_back(pTextureView->GetNativeHandle());
                vecBindlessIndexs.push_back(pTextureView->GetBindlessHandle());
                Type = BindlessHeapType::Standard;
            }

            BindlessBindInfo(IKGFX_TextureView** ppTextureView, uint32_t NumTexture)
            {
                for (uint32_t i = 0; i < NumTexture; i++)
                {
                    vecHandles.push_back((*ppTextureView + i)->GetNativeHandle());
                    vecBindlessIndexs.push_back((*ppTextureView + i)->GetBindlessHandle());
                }
                Type = BindlessHeapType::Standard;
            }

            BindlessBindInfo(IKGFX_Sampler* pSampler)
            {
                vecHandles.push_back(pSampler->GetNativeHandle());
                vecBindlessIndexs.push_back(pSampler->GetBindlessView()->GetBindlessHandle());
                Type = BindlessHeapType::Sampler;
            }
        };

    public:
        RayTracingPipelineStateDx12() = default;
        virtual ~RayTracingPipelineStateDx12();
        virtual bool Create(const RayTracingProgramDesc& rtpDC);
        virtual bool BeginBind(IKGFX_RenderContext* pRenderCtx);
        //virtual bool BindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView);
        //virtual bool BindUAV(const_pool_str pcszName, IKGFX_BufferView* pBufferUAV);
        //virtual bool BindUAV(const_pool_str pcszName, IKGFX_TextureView* pTextureUAV);
        //virtual bool BindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufferSRV);
        //virtual bool BindSRV(const_pool_str pcszName, IKGFX_TextureView* pTextureSRV);
        //virtual bool BindUAVArray(const_pool_str pcszName, uint32_t Num, IKGFX_BufferView** ppBufferViews);
        //virtual bool BindUAVArray(const_pool_str pcszName, uint32_t Num, IKGFX_TextureView** ppTextureViews);
        //virtual bool BindSRVArray(const_pool_str pcszName, uint32_t Num, IKGFX_BufferView** ppBufferViews);
        //virtual bool BindSRVArray(const_pool_str pcszName, uint32_t Num, IKGFX_TextureView** ppTextureViews);
        //virtual bool BindSampler(const_pool_str pcszName, IKGFX_Sampler* pSampler);
        virtual bool EndBind(IKGFX_RenderContext* pRenderCtx);
        virtual bool AddBindlessCBV(IKGFX_BufferView* pBufView);
        virtual bool AddBindlessUAV(IKGFX_BufferView* pUAV);
        virtual bool AddBindlessUAV(IKGFX_TextureView* pTexView);
        virtual bool AddBindlessSRV(IKGFX_BufferView* pSRV);
        virtual bool AddBindlessSRV(IKGFX_TextureView* pTexView);
        virtual bool AddBindlessSampler(IKGFX_Sampler* pSampler);
        virtual bool AddBindlessRayTracingScene(KRayTracingScene* pRTScene);

        virtual bool AddBindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView);
        virtual bool AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufView);
        virtual bool AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView);
        virtual bool AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pBufView);
        virtual bool AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pTexView);


        void Apply(IKGFX_RenderContext* pRenderContext, const RayTracingShaderDx12* pRayGenShader, RayTracingShaderBindingTableDx12* pShaderBindingTable);

    public:
        ID3D12StateObject* GetD3D12StateObject();

    private:
        template<typename View>
        void BindResource(const_pool_str pcszName, View* pView);
        template<typename View>
        void BindResource(const_pool_str pcszName, uint32_t Num, View** ppViews);

        void BindToBindless();
        void CompairBindInfo();
        void Destroy();

    public:
        RayTracingShaderDx12Library m_RayGenShaders;
        RayTracingShaderDx12Library m_MissShaders;
        RayTracingShaderDx12Library m_HitGroupShaders;
        RayTracingShaderDx12Library m_CallableShaders;

        ID3D12StateObject* m_pStateObject = nullptr;
        ID3D12StateObjectProperties* m_pPipelineProperties = nullptr;
        std::vector<BindlessBindInfo> m_vecBindlessBindInfos;
        std::vector< IKGFX_BufferView*> m_vecBufferBindViews;
        std::vector< uint64_t> m_vecBindInfos;
        BindInfoCache m_BindInfoCache = {};
        bool m_bCacheDirty = true;
    };

    struct RayTracingShaderBindingTableInitializer
    {
        uint32_t LocalBindingDataSize = 0;
        uint32_t NumShaderSlotsPerGeometrySegment = 1;
        uint32_t NumGeometrySegments = 0;
        uint32_t NumMissShaderSlots = 1;
        uint32_t NumCallableShaderSlots = 0;
    };

    class RayTracingShaderBindingTableDx12 : public KShaderBindingTable
    {
    public:
        RayTracingShaderBindingTableDx12() = default;
        virtual ~RayTracingShaderBindingTableDx12();
        virtual BOOL Create(const ShaderBindingTableDesc& Initializer);
        virtual void Destroy();
        D3D12_DISPATCH_RAYS_DESC GetDispatchDesc();
        RayTracingDescriptorCacheDx12& GetDescriptorCache() { return *m_pDescriptorCache; }

    public:
        virtual bool SetShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline, enumRayTracingShaderType sType);
        virtual bool SetHitGroupBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline);
        virtual bool SetMissShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline);
        virtual bool SetRayGenShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline);
        virtual bool SetCallableShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline);
        virtual bool CommitShaderBindingTable(IKGFX_RenderContext* commandBuffer);

    private:
        void SetShaderBindingTable(const KRayTracingShaderBinding& binding, RayTracingShaderIdentifier& Identifier);
        void WriteData(uint32_t Offset, const void* pData, uint32_t Size);
        void SetShaderIndentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier);
        void SetRayGenShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier);
        void SetHitGroupShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier);
        void SetMissShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier);
        void SetCallableShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier);
        void SetBindingData(uint32_t Offset, uint32_t OffsetInRootSignature, const void* pData, uint32_t Size);
        void SetRayGenShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size);
        void SetHitGroupShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size);
        void SetMissShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size);
        void SetCallableShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size);

    private:
        static constexpr uint32_t ShaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        uint32_t m_NumRayGenRecords = 0;
        uint32_t m_NumMissRecords = 0;
        uint32_t m_NumHitRecords = 0;
        uint32_t m_NumCallableRecords = 0;
        uint32_t m_MissShaderTableOffset = 0;
        uint32_t m_HitGroupShaderTableOffset = 0;
        uint32_t m_CallableShaderTableOffset = 0;
        uint32_t m_LocalRecordStride = 0;

        std::vector<uint8_t> m_vecShaderBindingTableData;
        IKGFX_Buffer* m_pShaderBindingTableBuffer = nullptr;
        RayTracingDescriptorCacheDx12* m_pDescriptorCache = nullptr;
    };

    struct RayTracingAccelerationStructureSize
    {
        uint64_t ResultSize = 0;
        uint64_t BuildScratchSize = 0;
        uint64_t UpdateScratchSize = 0;
    };

    class KGFX_RayTracingGeometryDx12 : public KRayTracingGeomery
    {
    public:
        static constexpr uint32_t uIndicesPerPrimitive = 3;
        KGFX_RayTracingGeometryDx12() = default;
        virtual ~KGFX_RayTracingGeometryDx12();

        virtual void Destroy() override;
        virtual BOOL Create(const RayTracingGeomeryCreateDesc& InGeometryCreateDesc, IKGFX_RenderContext* pRenderContext = nullptr);
        virtual BOOL Update(const RayTracingGeomeryUpdateParams& InUpdateParams, RayTracingAccelerationStructureSize& OutPreBuildSizeInfo, IKGFX_RenderContext* pRenderContext = nullptr);

    public:
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& GetRayTracingGeometryDesc() { return m_vecGeometryDescs; }
        RayTracingAccelerationStructureSize GetAccelerationStructureSize() { return m_SizeInfo; }
        IKGFX_Buffer* GetBottomAccelerationStructureBuffer()const { return m_pBottomAccelerationStructureBuffer; }

    private:
        RayTracingGeomeryCreateDesc m_GeometryCreateDesc{};
        IKGFX_Buffer* m_pBottomAccelerationStructureBuffer = nullptr;
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_vecGeometryDescs;
        RayTracingAccelerationStructureSize m_SizeInfo{};
        uint64_t m_AccelerationStructureCompactedSize = 0;
    };

    class RayTracingSceneDx12 : public KRayTracingScene
    {
    public:
        RayTracingSceneDx12() = default;
        virtual ~RayTracingSceneDx12();
        virtual uint32_t GetBindlessHandle();
        virtual BOOL Create(const RayTracingSceneCreateDesc& SceneCreateDesc, IKGFX_RenderContext* pRenderContext = nullptr);
        virtual BOOL Update(const RayTracingSceneUpdateParams& SceneUpdateParam, IKGFX_RenderContext* pRenderContext = nullptr);
        virtual void Destroy();

    public:
        IKGFX_Buffer* GetInstanceBuffer() { return m_pInstanceBuffer; }
        IKGFX_Buffer* GetTopAccelerationStructureBuffer() { return m_pTopAccelerationStructureBuffer; }
        gfx::IKGFX_BufferView* getTopAccelerationStructureBufferView() { return m_pTopAccelerationStructureView; }
        RayTracingShaderBindingTableDx12* FindOrCreateShaderBindingTable(const RayTracingPipelineStateDx12* pPipeline);

    private:
        RayTracingSceneCreateDesc m_SceneCreateDesc{};
        IKGFX_Buffer* m_pTopAccelerationStructureBuffer = nullptr;
        gfx::IKGFX_BufferView* m_pTopAccelerationStructureView = nullptr;
        IKGFX_Buffer* m_pInstanceBuffer = nullptr;
        RayTracingAccelerationStructureSize m_SizeInfo{};
        std::map< const RayTracingPipelineStateDx12*, RayTracingShaderBindingTableDx12*> m_mapShaderTables;
    };

    struct RayTracingDx12Proxy : public IKRayTracingProxy
    {
        RayTracingDx12Proxy() = default;
        virtual ~RayTracingDx12Proxy() {};

        virtual KRayTracingProgram* CommitRayTracingProgram(const RayTracingProgramDesc& rtpDC);
        virtual KRayTracingGeomery* CreateRHIRayTracingGeomtry();
        virtual bool InitRHIRayTracingGeometry(const RayTracingGeomeryCreateDesc& GeometryCreateDesc, KRayTracingGeomery* pRHIGeometry) override;
        virtual BOOL CommitRHIRayTracingGeometries(const RayTracingGeometryUpdateBatch& GeometryUpdateBatch, IKGFX_RenderContext* pRenderContext = nullptr) override;
        virtual KRayTracingScene* CreateRHIRayTracingScene(const RayTracingSceneCreateDesc& SceneCreateDesc) override;
        virtual BOOL CommitRHIRayTracingScene(const RayTracingSceneUpdateParams& SceneUpdateParam, IKGFX_RenderContext* pRenderContext = nullptr) override;
        KShaderBindingTable* CreateRHIShaderBindingTable(const ShaderBindingTableDesc& SBTDC, KRayTracingProgram* pProgram);
        virtual bool TraceRay(IRayTracingShader* pRayGenShader, IRayTracingShader* pMissShader, IRayTracingShader* pCallableShader, KRayTracingProgram* rayTracingProgram, KShaderBindingTable* pShaderBindingTable, uint32_t Width, uint32_t Height, IKGFX_RenderContext* pRenderContext);
        virtual IRayTracingShader* CreateRayTracingShader(const KRayTracingShaderCreateDesc& ShaderDesc);
    };
}
