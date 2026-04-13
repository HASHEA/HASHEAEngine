#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/RenderProgram.h"
#include "Graphics/Buffer.h"
#include "DX12RenderProgramBinder.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace D3D12MA { class Allocator; class Allocation; }

namespace RHI
{
	class DX12Buffer;
	class DX12CommandBuffer;
	class DX12DescriptorHeapManager;
	class DX12Shader;
	class DX12DescriptorSetLayout;

	// ==========================================
	// Acceleration Structure Sizes
	// ==========================================
	struct DX12AccelerationStructureSizes
	{
		uint64_t resultSize = 0;
		uint64_t buildScratchSize = 0;
		uint64_t updateScratchSize = 0;
	};

	// ==========================================
	// Acceleration Structure Creation Desc
	// ==========================================
	struct DX12BLASGeometryDesc
	{
		std::shared_ptr<Buffer> vertexBuffer;
		uint32_t vertexCount = 0;
		uint32_t vertexStride = 0;
		uint64_t vertexOffset = 0;
		AshFormat vertexFormat = ASH_FORMAT_R32G32B32A32_SFLOAT;

		std::shared_ptr<Buffer> indexBuffer;
		uint32_t indexCount = 0;
		uint64_t indexOffset = 0;
		AshIndexType indexType = ASH_INDEX_TYPE_UINT32;

		std::shared_ptr<Buffer> transformBuffer; // optional 3x4 row-major transform
		uint64_t transformOffset = 0;

		bool isOpaque = true;
	};

	struct DX12BLASCreateDesc
	{
		std::vector<DX12BLASGeometryDesc> geometries;
		bool allowUpdate = false;
		bool preferFastTrace = true;
	};

	struct DX12TLASInstanceDesc
	{
		float transform[3][4] = {};        // 3x4 row-major
		uint32_t instanceID = 0;           // 24-bit instance ID
		uint32_t instanceMask = 0xFF;      // 8-bit mask
		uint32_t instanceContributionToHitGroupIndex = 0; // 24-bit
		uint32_t flags = 0;                // D3D12_RAYTRACING_INSTANCE_FLAGS
		std::shared_ptr<Buffer> blasBuffer; // buffer containing the BLAS
	};

	struct DX12TLASCreateDesc
	{
		uint32_t maxInstanceCount = 0;
		bool allowUpdate = false;
		bool preferFastTrace = true;
	};

	// ==========================================
	// Bottom Level Acceleration Structure (BLAS)
	// ==========================================
	class DX12BLAS
	{
	public:
		DX12BLAS() = default;
		~DX12BLAS();

		bool create(const DX12BLASCreateDesc& desc, ID3D12Device5* device, D3D12MA::Allocator* allocator);
		bool build(DX12CommandBuffer* cmdBuffer, bool update = false);
		void destroy();

		ID3D12Resource* get_result_buffer() const { return m_resultBuffer.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS get_gpu_address() const;
		const DX12AccelerationStructureSizes& get_sizes() const { return m_sizes; }

	private:
		ComPtr<ID3D12Resource> m_resultBuffer;
		ComPtr<ID3D12Resource> m_scratchBuffer;
		D3D12MA::Allocation* m_resultAllocation = nullptr;
		D3D12MA::Allocation* m_scratchAllocation = nullptr;
		D3D12MA::Allocator* m_allocator = nullptr;

		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_geometryDescs;
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_buildInputs{};
		DX12AccelerationStructureSizes m_sizes{};
		bool m_allowUpdate = false;
		bool m_isBuilt = false;
	};

	// ==========================================
	// Top Level Acceleration Structure (TLAS)
	// ==========================================
	class DX12TLAS
	{
	public:
		DX12TLAS() = default;
		~DX12TLAS();

		bool create(const DX12TLASCreateDesc& desc, ID3D12Device5* device, D3D12MA::Allocator* allocator);
		bool update_instances(const std::vector<DX12TLASInstanceDesc>& instances);
		bool build(DX12CommandBuffer* cmdBuffer, bool update = false);
		void destroy();

		ID3D12Resource* get_result_buffer() const { return m_resultBuffer.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS get_gpu_address() const;
		const DX12AccelerationStructureSizes& get_sizes() const { return m_sizes; }

	private:
		ComPtr<ID3D12Resource> m_resultBuffer;
		ComPtr<ID3D12Resource> m_scratchBuffer;
		ComPtr<ID3D12Resource> m_instanceBuffer;
		D3D12MA::Allocation* m_resultAllocation = nullptr;
		D3D12MA::Allocation* m_scratchAllocation = nullptr;
		D3D12MA::Allocation* m_instanceAllocation = nullptr;
		D3D12MA::Allocator* m_allocator = nullptr;
		ID3D12Device5* m_device = nullptr;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_buildInputs{};
		DX12AccelerationStructureSizes m_sizes{};
		uint32_t m_maxInstanceCount = 0;
		bool m_allowUpdate = false;
		bool m_isBuilt = false;
	};

	// ==========================================
	// Shader Binding Table (SBT)
	// ==========================================
	struct DX12ShaderRecord
	{
		void* shaderIdentifier = nullptr;
		uint32_t shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		void* localRootArguments = nullptr;
		uint32_t localRootArgumentsSize = 0;
	};

	class DX12ShaderBindingTable
	{
	public:
		DX12ShaderBindingTable() = default;
		~DX12ShaderBindingTable();

		bool create(ID3D12StateObjectProperties* pipelineProps,
			ID3D12Device* device,
			D3D12MA::Allocator* allocator,
			const std::wstring& rayGenShaderName,
			const std::vector<std::wstring>& missShaderNames,
			const std::vector<std::wstring>& hitGroupNames,
			const std::vector<std::wstring>& callableShaderNames = {});

		void destroy();

		D3D12_DISPATCH_RAYS_DESC get_dispatch_desc(uint32_t width, uint32_t height, uint32_t depth = 1) const;

	private:
		uint32_t _align(uint32_t size, uint32_t alignment) const
		{
			return (size + (alignment - 1)) & ~(alignment - 1);
		}

		bool _create_sbt_buffer(ID3D12Device* device, D3D12MA::Allocator* allocator,
			const void* data, uint64_t size,
			ComPtr<ID3D12Resource>& outBuffer, D3D12MA::Allocation*& outAllocation);

	private:
		ComPtr<ID3D12Resource> m_rayGenBuffer;
		ComPtr<ID3D12Resource> m_missBuffer;
		ComPtr<ID3D12Resource> m_hitGroupBuffer;
		ComPtr<ID3D12Resource> m_callableBuffer;

		D3D12MA::Allocation* m_rayGenAllocation = nullptr;
		D3D12MA::Allocation* m_missAllocation = nullptr;
		D3D12MA::Allocation* m_hitGroupAllocation = nullptr;
		D3D12MA::Allocation* m_callableAllocation = nullptr;

		uint32_t m_rayGenRecordSize = 0;
		uint32_t m_missRecordSize = 0;
		uint32_t m_hitGroupRecordSize = 0;
		uint32_t m_callableRecordSize = 0;

		uint32_t m_missRecordCount = 0;
		uint32_t m_hitGroupRecordCount = 0;
		uint32_t m_callableRecordCount = 0;
	};

	// ==========================================
	// RT Pipeline State Object
	// ==========================================
	struct DX12RayTracingPipelineDesc
	{
		// Shader libraries (DXIL lib_6_6 bytecodes)
		struct ShaderLib
		{
			std::shared_ptr<DX12Shader> shader;
			std::vector<std::wstring> exportNames;
		};
		std::vector<ShaderLib> shaderLibraries;

		// Hit groups
		struct HitGroup
		{
			std::wstring name;
			std::wstring closestHitShader;
			std::wstring anyHitShader;
			std::wstring intersectionShader;
			D3D12_HIT_GROUP_TYPE type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
		};
		std::vector<HitGroup> hitGroups;

		// Ray gen, miss, callable export names
		std::wstring rayGenExport;
		std::vector<std::wstring> missExports;
		std::vector<std::wstring> callableExports;

		uint32_t maxPayloadSize = 32;
		uint32_t maxAttributeSize = 8; // D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES (= 32) but typical triangle = 8 (barycentrics)
		uint32_t maxTraceRecursionDepth = 1;

		ID3D12RootSignature* globalRootSignature = nullptr;
		ID3D12RootSignature* localRootSignature = nullptr; // optional
	};

	class DX12RayTracingPipeline
	{
	public:
		DX12RayTracingPipeline() = default;
		~DX12RayTracingPipeline();

		bool create(const DX12RayTracingPipelineDesc& desc, ID3D12Device5* device);
		void destroy();

		ID3D12StateObject* get_state_object() const { return m_stateObject.Get(); }
		ID3D12StateObjectProperties* get_properties() const { return m_properties.Get(); }

	private:
		ComPtr<ID3D12StateObject> m_stateObject;
		ComPtr<ID3D12StateObjectProperties> m_properties;
	};

	// ==========================================
	// DX12 Ray Tracing Render Program
	// ==========================================
	class DX12RayTracingRenderProgram : public IRayTracingRenderProgram
	{
	public:
		DX12RayTracingRenderProgram() = default;
		~DX12RayTracingRenderProgram();

		bool create(const DX12RayTracingPipelineDesc& desc);
		void destroy();

		void dispatch_rays(DX12CommandBuffer* cmdBuffer, uint32_t width, uint32_t height, uint32_t depth = 1);

		DX12RayTracingPipeline* get_pipeline() { return m_pipeline.get(); }
		DX12ShaderBindingTable* get_sbt() { return m_sbt.get(); }

	private:
		bool _build_root_signature(const DX12RayTracingPipelineDesc& desc);
		bool _build_pipeline(const DX12RayTracingPipelineDesc& desc);
		bool _build_sbt(const DX12RayTracingPipelineDesc& desc);

	private:
		std::unique_ptr<DX12RayTracingPipeline> m_pipeline;
		std::unique_ptr<DX12ShaderBindingTable> m_sbt;
		std::unique_ptr<DX12DescriptorSetLayout> m_rootSignatureLayout;
		DX12RenderProgramBinder m_binder;
		DX12RayTracingPipelineDesc m_desc;
	};
}
