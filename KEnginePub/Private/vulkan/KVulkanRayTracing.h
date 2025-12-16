/**********************************************************
 * File: KVulkanRayTracing.h
 * Author: yizhou hu
 * Date: 2025/1/6
 * Description:
 **********************************************************/
#pragma once
#include "../IGFX_Private.h"
#include "KVulkanFunc.h"
#include "KVulkanDefine.h"
#include "KVulkanPrivate.h"
#include <vector>

namespace gfx
{
	constexpr uint32_t k_max_bindless_smapled_texture = 2048;
	class IKSpecializationConstantContainer;
	class KVulkanDescriptorSet;
	enum enumForProcessType : uint8_t;
    
    struct KVulkanRayTracingShaderResource : public IShaderReflector
    {
        BOOL BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType) override;
        IKGFX_CombinedShaderResult* GetCombindShaderResult() override;
        KRayTracingUniformBlockInfo* m_pLocalMaterialBindlessIDCbufferBlock = nullptr;
        KRayTracingUniformBlockInfo* m_pLocalEngineBindlessIDCbufferBlock = nullptr;
        KRayTracingUniformBlockInfo* m_pLocalMaterialParamCbufferBlock = nullptr;
        KRayTracingUniformBlockInfo* m_pCommonBindlessIDCbufferBlock = nullptr;
        VkPipelineShaderStageCreateInfo m_vkShaderStageCreatInfo{};
        ERTShaderSubType m_subType = E_RT_TYPE_DEFAULT;
        VkShaderModule m_shaderModule = VK_NULL_HANDLE;
        std::unordered_map<const_pool_str, KRayTracingUniformBlockInfo*> m_mapUniformBlocks;
        uint32_t m_uMaxSet = 0;
        ~KVulkanRayTracingShaderResource();
        uint32_t m_uStorageImageCount = 0;
        uint32_t m_uSampledImageCount = 0;
        uint32_t m_uStorageBufferCount = 0;
        uint32_t m_uUniformBufferCount = 0;
        uint32_t m_uSamplerCount = 0;
        uint32_t m_uAccelerationStructureCount = 0;
    };
	struct KVulkanRayTracingShader : public IRayTracingShader
	{
		KVulkanRayTracingShader();
		~KVulkanRayTracingShader();
        bool Create(const KRayTracingShaderCreateDesc& ci) override;
      
        virtual auto GetHash()-> uint64_t override;
		auto IsReady() const->BOOL;
        KRayTracingUniformBlockInfo* GetLocalMaterialBindlessIDUniformBlockInfo() override;
        KRayTracingUniformBlockInfo* GetLocalEngineBindlessIDUniformBlockInfo() override;
        KRayTracingUniformBlockInfo* GetLocalMaterialParamUniformBlockInfo() override;
        KRayTracingUniformBlockInfo* GetCommonUniformBlockInfo() override;
        const VkPipelineShaderStageCreateInfo& GetCreateInfo(ERTShaderSubType _type = ERTShaderSubType::E_RT_TYPE_DEFAULT);
        KVulkanLayout* GetDesciptorSetLayout();
        inline enumRayTracingShaderType GetType() const override
        {
            return m_type;
        };
       
        inline bool IsSubTypePresent(ERTShaderSubType _type)
        {
            bool bRet = false;
            for (auto _subData : m_vecShaderResouces)
            {
                if (_subData->m_subType == _type)
                {
                    if (_subData->m_shaderModule != VK_NULL_HANDLE)
                    {
                        bRet = true;
                    }
                    return bRet;
                }
            }
            return bRet;
        };
        KVulkanRayTracingShaderResource* GetShaderResource(uint32_t index)
        {
            return m_vecShaderResouces[index];
        }
    private:
        std::vector<KVulkanRayTracingShaderResource*> m_vecShaderResouces;
        KVulkanLayout* m_pLayout = nullptr;
	private:
		bool m_bIsReady = FALSE;
		uint64_t m_uUid = 0;
        enumRayTracingShaderType m_type = enumRayTracingShaderType::KRT_ST_MAX_ENUM;

      
    };
	struct RayTracingPipelineDesc
	{
        std::vector<IRayTracingShader*> vecRayGenShaders;
        std::vector<IRayTracingShader*> vecMissShaders;
        std::vector<IRayTracingShader*> vecHitShaders;
        std::vector<IRayTracingShader*> vecCallableShaders;
        uint32_t                        uMaxPayloadSize = 0;
        uint32_t                        uAttributeSize = 0;
		uint32_t						uMaxRayRecursionDepth = 0;
	};



	class KVulkanRayTracingPipeline 
	{
	public:
		KVulkanRayTracingPipeline();
		virtual ~KVulkanRayTracingPipeline();
		virtual bool Create(const RayTracingPipelineDesc& pDesc, IKSpecializationConstantContainer* pSpecializationInfo = nullptr);
		virtual auto Destroy()->BOOL;
		VkPipeline GetPipeline() const;
        VkPipelineLayout GetPipelineLayout() const;
		enumForProcessType GetType() const;
	public:
		virtual auto GetShaderGroupCount()->uint32_t;
        KVulkanDescriptorSet* GetDescriptorSet()
        {
            return m_pDescriptorSet;
        }
    public:
        const std::unordered_map<const_pool_str, KRayTracingUniformBlockInfo*>& GetUniformMap()
        {
            return RayGen.Shaders[0]->GetShaderResource(0)->m_mapUniformBlocks;
        }
        uint32_t GetShaderGroupHandleSize() const;
        uint32_t GetShaderGroupHandleSizeAligned() const;
    private:
        struct ShaderData
        {
            std::vector<KVulkanRayTracingShader*> Shaders;
            std::vector<uint8_t> ShaderHandles;
        };
        ShaderData RayGen;
        ShaderData Miss;
        ShaderData HitGroup;
        ShaderData Callable;
	private:
        VkPipelineLayout m_pPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pPipeline = VK_NULL_HANDLE;
		uint32_t m_uShaderGroupCount = 0;
    private:
        uint32_t m_uGroupHandleSize = 0;
        uint32_t m_uGroupHandleSizeAligned = 0;
    private:
        gfx::KDescriptorPoolContainer m_DescriptorPoolContainer;
        gfx::KVulkanDescriptorSet* m_pDescriptorSet = nullptr;
    public:
        const KVulkanRayTracingPipeline::ShaderData& GetShaderData(enumRayTracingShaderType eShaderType) const;
	};

	class KVulkanRayTracingProgram :public KRayTracingProgram
	{
	public:
		auto Create(const RayTracingProgramDesc& rtpDC)->bool override;
        virtual auto Destroy() -> void override;

	public:
        bool Apply(IKGFX_RenderContext* pRenderCtx);

        bool BeginBind(IKGFX_RenderContext* pRenderCtx) override;

        bool AddBindlessUAV(IKGFX_BufferView* pUAV) override;

        bool AddBindlessUAV(IKGFX_TextureView* pTexView) override;

        bool AddBindlessSRV(IKGFX_BufferView* pSRV) override;

        bool AddBindlessSRV(IKGFX_TextureView* pTexView) override;

        bool AddBindlessCBV(IKGFX_BufferView* pBufView) override;

        bool AddBindlessSampler(IKGFX_Sampler* pSampler) override;

        bool AddBindlessRayTracingScene(KRayTracingScene* pRTScene) override;

        bool AddBindCBV(const_pool_str pName, IKGFX_BufferView* pCBV) override;

        bool EndBind(IKGFX_RenderContext* commandBuffer) override;
	public:
		auto GetVulkanPipeline()->KVulkanRayTracingPipeline*;
	private:
		KVulkanRayTracingPipeline* m_pVulkanRayTracingPipeline = nullptr;

       // std::vector<VkDescriptorSet> m_vecRayTracingDescriptorSets;
    };

	struct VKBLASBuildData
	{
		std::vector<VkAccelerationStructureGeometryKHR> segmentsInfos;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> offsetInfos;
		VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo_{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,nullptr,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR };
		VkAccelerationStructureBuildSizesInfoKHR sizesInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	};

	class KVulkanCommandBuffer;

	class KVulkanRayTracingGeometry : public KRayTracingGeomery
	{
	public:
		static constexpr uint32_t uIndicesPerPrimitive = 3;//only support triangle meshes
		KVulkanRayTracingGeometry() = default;
		virtual ~KVulkanRayTracingGeometry();
		virtual auto Destroy() -> void override;
		/// <summary>
		/// initialize the geometry
		/// </summary>
		/// <param name="gdc">create desc</param>
		/// <param name="commandBuffer"> commandbuffer (may not use in vulkan)</param>
		/// <returns></returns>
		virtual auto Init(const RayTracingGeomeryCreateDesc& gdc, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL;


		/// <summary>
		/// update the geometry info, and make build data, for vulkan, do not build here one by one, make a batch building for better performance
		/// </summary>
		/// <param name="updateParam">update params</param>
		/// <param name="commandBuffer">commandbuffer (may not use in vulkan)</param>
		/// <returns></returns>
		virtual auto Update(const RayTracingGeomeryUpdateParams& updateParam, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL;


	public:
		/// <summary>
		/// fetch the build data for batch building
		/// </summary>
		/// <returns></returns>
		auto FetchBuildData() ->const VKBLASBuildData&;

		/// <summary>
		/// try to compact a static geometry
		/// </summary>
		/// <param name="commandBuffer">cb</param>
		/// <param name="uSizeAfterCompaction">the actual size of this geo, normally query by query pool after actually building</param>
		/// <returns></returns>
		auto CompactAccelerationStructure(IKGFX_RenderContext* commandBuffer, uint64_t uSizeAfterCompaction) -> void;

	private:
		RayTracingGeomeryCreateDesc geometryDesc{};
		VKBLASBuildData				blasBuildData{};
		VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
		IKGFX_Buffer* accelerationStructureBuffer = nullptr;
		const char* name = nullptr;

		friend class KVulkanRayTracingScene;
	};

	//TLAS
	struct VKTLASBuildData
	{
		VkAccelerationStructureGeometryKHR Geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		VkAccelerationStructureBuildGeometryInfoKHR GeometryInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos = nullptr;
		VkAccelerationStructureBuildSizesInfoKHR SizesInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	};
	class KVulkanRayTracingScene : public KRayTracingScene
	{
	public:
		KVulkanRayTracingScene() = default;
		virtual ~KVulkanRayTracingScene();
		// Inherited via KRayTracingScene
		virtual auto Create(const RayTracingSceneCreateDesc& RTCDC, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL;
		virtual auto Destroy() -> void override;
		/// <summary>
		/// update the geometry info, and update the build data
		/// if count > maxcount, may need a fully rebuild.
		/// </summary>
		/// <param name="params"></param>
		/// <param name="commandBuffer"></param>
		/// <returns></returns>
		virtual auto Update(const RayTracingSceneUpdateParams& params, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL;
	public:
		/// <summary>
		/// fetch the build data for batch building
		/// </summary>
		/// <returns></returns>
		auto FetchBuildData() ->const VKTLASBuildData&;


		/// <summary>
		/// create a instance of blas, which normally represent an actual object in the rt scene !
		/// </summary>
		/// <param name="intanceId"> the instance id, ref in shader</param>
		/// <param name="hitGroupId"> the shader group id, decide which shader to call when hit</param>
		/// <param name="pTransformMat">the transform mat, decide the transform of the instance in the scene, need row major matrix input</param>
		/// <returns></returns>
		auto CreateInstance(const RayTracingInstance& instance)->VkAccelerationStructureInstanceKHR;

        auto GetAcceleration() const -> VkAccelerationStructureKHR
		{
			return tlas;
		};

        uint32_t GetBindlessHandle() override
        {
            return m_uBindlessHandle;
        };
	private:
		VKTLASBuildData tlasBuildData{};
		RayTracingSceneCreateDesc rtSceneDesc{};
		VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
		std::vector<VkAccelerationStructureInstanceKHR> instances;
		IKGFX_Buffer* accelerationStructureBuffer = nullptr;
		IKGFX_Buffer* instanceBuffer = nullptr;
		uint32_t uCurrentNumInstances = 0;
		const char* name = nullptr;
        uint32_t m_uBindlessHandle = UINT32_MAX;

	};
	class KVulkanStagingBuffer;
	class KVulkanShaderBindingTable : public KShaderBindingTable
	{
	public:
		KVulkanShaderBindingTable() = default;
		virtual ~KVulkanShaderBindingTable();
		// Inherited via KShaderBindingTable
		virtual auto Create(const ShaderBindingTableDesc& SBTDC)->BOOL override;

        virtual auto Destroy() -> void override;
	public:
		auto GetRayGenShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData) -> void;
		auto GetHitShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData)  -> void;
		auto GetMissShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData)  -> void;
		auto GetCallableShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData)  -> void;
	
	private:
        struct KVulkanShaderTableAllocation
        {
            uint32_t handleCount = 0;
            bool bUseLocalRecord = false;
            // Host memory copy
            std::vector<uint8_t> hostBuffer;
            // GPU Local memory copy
            IKGFX_Buffer* localBuffer = nullptr;
            VkStridedDeviceAddressRegionKHR region{};
            bool bIsDirty = true;
        };
        KVulkanShaderTableAllocation miss{};
        KVulkanShaderTableAllocation hitGroup{};
        KVulkanShaderTableAllocation callable{};
        KVulkanShaderTableAllocation rayGen{};
    private:
       
        KVulkanShaderTableAllocation& _GetAlloc(enumRayTracingShaderType sType);
        void _ReleaseLocalBuffer(KVulkanShaderTableAllocation& _alloc);
    private:
        bool _SetBindingsOnShaderBindingTable(const KRayTracingShaderBinding& binding, KVulkanRayTracingPipeline* pipeline, enumRayTracingShaderType sType);
    public:
        bool SetShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline, enumRayTracingShaderType sType) override;
        bool SetHitGroupBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) override;
        bool SetMissShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) override;
        bool SetRayGenShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) override;
        bool SetCallableShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) override;
        bool CommitShaderBindingTable(IKGFX_RenderContext* commandBuffer) override;
    };


	struct VulkanRayTracingProxy : public IKRayTracingProxy
	{
        VulkanRayTracingProxy();
        ~VulkanRayTracingProxy() {};
		// Inherited via IKRayTracingProxy
		virtual auto InitRHIRayTracingGeometry(const RayTracingGeomeryCreateDesc& createDesc, KRayTracingGeomery* pRHIGeometry)->bool override;
		/// <summary>
		/// build or update a batch of geometry
		/// </summary>
		/// <param name="updateBatch"></param>
		/// <param name="commandBuffer"></param>
		/// <returns></returns>
		virtual auto CommitRHIRayTracingGeometries(const RayTracingGeometryUpdateBatch& updateBatch, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL override;
		// Inherited via IKRayTracingProxy
		virtual auto CreateRHIRayTracingScene(const RayTracingSceneCreateDesc& createDesc)->KRayTracingScene* override;
		virtual auto CommitRHIRayTracingScene(const RayTracingSceneUpdateParams& updateParam, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL override;
	

		// Inherited via IKRayTracingProxy
		virtual auto CommitRayTracingProgram(const RayTracingProgramDesc& rtpDC)->KRayTracingProgram* override;
        virtual auto TraceRay(IRayTracingShader* pRayGenShader, IRayTracingShader* pMissShader, IRayTracingShader* pCallableShader, KRayTracingProgram* rayTracingProgram, KShaderBindingTable* shaderBindingTable,
            uint32_t width, uint32_t height, IKGFX_RenderContext* commandBuffer) -> bool override;
		// Inherited via IKRayTracingProxy
		virtual auto CreateRHIShaderBindingTable(const ShaderBindingTableDesc& SBTDC, KRayTracingProgram* program)->KShaderBindingTable* override;

		// Inherited via IKRayTracingProxy
		virtual auto CreateRayTracingShader(const KRayTracingShaderCreateDesc& ci)->IRayTracingShader* override;

        // 通过 IKRayTracingProxy 继承
        auto CreateRHIRayTracingGeomtry() -> KRayTracingGeomery* override;
    };
}
