#include "DX12RayTracing.h"
#include "DX12Buffer.h"
#include "DX12CommandBuffer.h"
#include "DX12Context.h"
#include "DX12Shader.h"
#include "DX12DescriptorSetLayout.h"
#include "Base/hlog.h"

#include "D3D12MemAlloc.h"

namespace RHI
{
	// ==========================================
	// Helper: Create a committed buffer via D3D12MA
	// ==========================================
	static bool _create_committed_buffer(
		D3D12MA::Allocator* allocator,
		uint64_t size,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES initialState,
		D3D12_HEAP_TYPE heapType,
		ComPtr<ID3D12Resource>& outResource,
		D3D12MA::Allocation*& outAllocation)
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = flags;

		D3D12MA::ALLOCATION_DESC allocDesc{};
		allocDesc.HeapType = heapType;

		HRESULT hr = allocator->CreateResource(
			&allocDesc,
			&desc,
			initialState,
			nullptr,
			&outAllocation,
			IID_PPV_ARGS(&outResource));

		if (FAILED(hr))
		{
			HLogError("DX12RayTracing: Failed to create buffer. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	// ==========================================
	// DX12BLAS Implementation
	// ==========================================
	DX12BLAS::~DX12BLAS()
	{
		destroy();
	}

	bool DX12BLAS::create(const DX12BLASCreateDesc& desc, ID3D12Device5* device, D3D12MA::Allocator* allocator)
	{
		m_allocator = allocator;
		m_allowUpdate = desc.allowUpdate;

		// Build geometry descriptors
		m_geometryDescs.resize(desc.geometries.size());
		for (size_t i = 0; i < desc.geometries.size(); ++i)
		{
			const auto& geom = desc.geometries[i];
			auto& d3dGeom = m_geometryDescs[i];

			d3dGeom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			d3dGeom.Flags = geom.isOpaque
				? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
				: D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

			auto& triangles = d3dGeom.Triangles;
			auto* vb = static_cast<DX12Buffer*>(geom.vertexBuffer.get());
			triangles.VertexBuffer.StartAddress = vb->get_resource()->GetGPUVirtualAddress() + geom.vertexOffset;
			triangles.VertexBuffer.StrideInBytes = geom.vertexStride;
			triangles.VertexCount = geom.vertexCount;
			triangles.VertexFormat = ash_to_dxgi_format(geom.vertexFormat);

			if (geom.indexBuffer)
			{
				auto* ib = static_cast<DX12Buffer*>(geom.indexBuffer.get());
				triangles.IndexBuffer = ib->get_resource()->GetGPUVirtualAddress() + geom.indexOffset;
				triangles.IndexCount = geom.indexCount;
				triangles.IndexFormat = (geom.indexType == ASH_INDEX_TYPE_UINT16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
			}

			if (geom.transformBuffer)
			{
				auto* tb = static_cast<DX12Buffer*>(geom.transformBuffer.get());
				triangles.Transform3x4 = tb->get_resource()->GetGPUVirtualAddress() + geom.transformOffset;
			}
		}

		// Get prebuild info
		m_buildInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		m_buildInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		m_buildInputs.NumDescs = (UINT)m_geometryDescs.size();
		m_buildInputs.pGeometryDescs = m_geometryDescs.data();
		m_buildInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		if (desc.preferFastTrace)
			m_buildInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		if (desc.allowUpdate)
			m_buildInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
		device->GetRaytracingAccelerationStructurePrebuildInfo(&m_buildInputs, &prebuildInfo);

		m_sizes.resultSize = prebuildInfo.ResultDataMaxSizeInBytes;
		m_sizes.buildScratchSize = prebuildInfo.ScratchDataSizeInBytes;
		m_sizes.updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

		// Create result buffer
		if (!_create_committed_buffer(allocator, m_sizes.resultSize,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			D3D12_HEAP_TYPE_DEFAULT, m_resultBuffer, m_resultAllocation))
		{
			return false;
		}

		// Create scratch buffer
		uint64_t scratchSize = (std::max)(m_sizes.buildScratchSize, m_sizes.updateScratchSize);
		if (!_create_committed_buffer(allocator, scratchSize,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_HEAP_TYPE_DEFAULT, m_scratchBuffer, m_scratchAllocation))
		{
			return false;
		}

		return true;
	}

	bool DX12BLAS::build(DX12CommandBuffer* cmdBuffer, bool update)
	{
		if (!m_resultBuffer || !m_scratchBuffer)
		{
			HLogError("DX12BLAS: Cannot build - buffers not created");
			return false;
		}

		auto* cmdList = cmdBuffer->get_command_list();

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
		buildDesc.Inputs = m_buildInputs;
		buildDesc.DestAccelerationStructureData = m_resultBuffer->GetGPUVirtualAddress();
		buildDesc.ScratchAccelerationStructureData = m_scratchBuffer->GetGPUVirtualAddress();

		if (update && m_isBuilt && m_allowUpdate)
		{
			buildDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			buildDesc.SourceAccelerationStructureData = m_resultBuffer->GetGPUVirtualAddress();
		}

		cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		// UAV barrier so subsequent operations can read the BLAS
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = m_resultBuffer.Get();
		cmdList->ResourceBarrier(1, &barrier);

		m_isBuilt = true;
		return true;
	}

	void DX12BLAS::destroy()
	{
		if (m_resultAllocation) { m_resultAllocation->Release(); m_resultAllocation = nullptr; }
		if (m_scratchAllocation) { m_scratchAllocation->Release(); m_scratchAllocation = nullptr; }
		m_resultBuffer.Reset();
		m_scratchBuffer.Reset();
		m_geometryDescs.clear();
		m_isBuilt = false;
	}

	D3D12_GPU_VIRTUAL_ADDRESS DX12BLAS::get_gpu_address() const
	{
		return m_resultBuffer ? m_resultBuffer->GetGPUVirtualAddress() : 0;
	}

	// ==========================================
	// DX12TLAS Implementation
	// ==========================================
	DX12TLAS::~DX12TLAS()
	{
		destroy();
	}

	bool DX12TLAS::create(const DX12TLASCreateDesc& desc, ID3D12Device5* device, D3D12MA::Allocator* allocator)
	{
		m_allocator = allocator;
		m_device = device;
		m_maxInstanceCount = desc.maxInstanceCount;
		m_allowUpdate = desc.allowUpdate;

		// Get prebuild info
		m_buildInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		m_buildInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		m_buildInputs.NumDescs = m_maxInstanceCount;
		m_buildInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		if (desc.preferFastTrace)
			m_buildInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		if (desc.allowUpdate)
			m_buildInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
		device->GetRaytracingAccelerationStructurePrebuildInfo(&m_buildInputs, &prebuildInfo);

		m_sizes.resultSize = prebuildInfo.ResultDataMaxSizeInBytes;
		m_sizes.buildScratchSize = prebuildInfo.ScratchDataSizeInBytes;
		m_sizes.updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

		// Create result buffer
		if (!_create_committed_buffer(allocator, m_sizes.resultSize,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			D3D12_HEAP_TYPE_DEFAULT, m_resultBuffer, m_resultAllocation))
		{
			return false;
		}

		// Create scratch buffer
		uint64_t scratchSize = (std::max)(m_sizes.buildScratchSize, m_sizes.updateScratchSize);
		if (!_create_committed_buffer(allocator, scratchSize,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_HEAP_TYPE_DEFAULT, m_scratchBuffer, m_scratchAllocation))
		{
			return false;
		}

		// Create instance buffer (upload heap for CPU writes)
		uint64_t instanceBufferSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_maxInstanceCount;
		if (!_create_committed_buffer(allocator, instanceBufferSize,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_HEAP_TYPE_UPLOAD, m_instanceBuffer, m_instanceAllocation))
		{
			return false;
		}

		return true;
	}

	bool DX12TLAS::update_instances(const std::vector<DX12TLASInstanceDesc>& instances)
	{
		if (instances.size() > m_maxInstanceCount)
		{
			HLogError("DX12TLAS: Instance count {} exceeds max {}", instances.size(), m_maxInstanceCount);
			return false;
		}

		// Map instance buffer and write D3D12_RAYTRACING_INSTANCE_DESC data
		D3D12_RAYTRACING_INSTANCE_DESC* mappedData = nullptr;
		HRESULT hr = m_instanceBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
		if (FAILED(hr))
		{
			HLogError("DX12TLAS: Failed to map instance buffer");
			return false;
		}

		for (size_t i = 0; i < instances.size(); ++i)
		{
			const auto& inst = instances[i];
			auto& d3dInst = mappedData[i];

			memcpy(d3dInst.Transform, inst.transform, sizeof(float) * 12);
			d3dInst.InstanceID = inst.instanceID & 0x00FFFFFF;
			d3dInst.InstanceMask = inst.instanceMask & 0xFF;
			d3dInst.InstanceContributionToHitGroupIndex = inst.instanceContributionToHitGroupIndex & 0x00FFFFFF;
			d3dInst.Flags = inst.flags;

			// Get BLAS GPU address
			if (inst.blasBuffer)
			{
				auto* dx12Buf = static_cast<DX12Buffer*>(inst.blasBuffer.get());
				d3dInst.AccelerationStructure = dx12Buf->get_resource()->GetGPUVirtualAddress();
			}
			else
			{
				d3dInst.AccelerationStructure = 0;
			}
		}

		m_instanceBuffer->Unmap(0, nullptr);

		// Update num descs for the actual instance count
		m_buildInputs.NumDescs = (UINT)instances.size();
		return true;
	}

	bool DX12TLAS::build(DX12CommandBuffer* cmdBuffer, bool update)
	{
		if (!m_resultBuffer || !m_scratchBuffer || !m_instanceBuffer)
		{
			HLogError("DX12TLAS: Cannot build - buffers not created");
			return false;
		}

		auto* cmdList = cmdBuffer->get_command_list();

		m_buildInputs.InstanceDescs = m_instanceBuffer->GetGPUVirtualAddress();

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
		buildDesc.Inputs = m_buildInputs;
		buildDesc.DestAccelerationStructureData = m_resultBuffer->GetGPUVirtualAddress();
		buildDesc.ScratchAccelerationStructureData = m_scratchBuffer->GetGPUVirtualAddress();

		if (update && m_isBuilt && m_allowUpdate)
		{
			buildDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			buildDesc.SourceAccelerationStructureData = m_resultBuffer->GetGPUVirtualAddress();
		}

		cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		// UAV barrier
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = m_resultBuffer.Get();
		cmdList->ResourceBarrier(1, &barrier);

		m_isBuilt = true;
		return true;
	}

	void DX12TLAS::destroy()
	{
		if (m_resultAllocation) { m_resultAllocation->Release(); m_resultAllocation = nullptr; }
		if (m_scratchAllocation) { m_scratchAllocation->Release(); m_scratchAllocation = nullptr; }
		if (m_instanceAllocation) { m_instanceAllocation->Release(); m_instanceAllocation = nullptr; }
		m_resultBuffer.Reset();
		m_scratchBuffer.Reset();
		m_instanceBuffer.Reset();
		m_isBuilt = false;
	}

	D3D12_GPU_VIRTUAL_ADDRESS DX12TLAS::get_gpu_address() const
	{
		return m_resultBuffer ? m_resultBuffer->GetGPUVirtualAddress() : 0;
	}

	// ==========================================
	// DX12ShaderBindingTable Implementation
	// ==========================================
	DX12ShaderBindingTable::~DX12ShaderBindingTable()
	{
		destroy();
	}

	bool DX12ShaderBindingTable::_create_sbt_buffer(
		ID3D12Device* device, D3D12MA::Allocator* allocator,
		const void* data, uint64_t size,
		ComPtr<ID3D12Resource>& outBuffer, D3D12MA::Allocation*& outAllocation)
	{
		if (!_create_committed_buffer(allocator, size,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_HEAP_TYPE_UPLOAD, outBuffer, outAllocation))
		{
			return false;
		}

		// Copy data
		void* mapped = nullptr;
		HRESULT hr = outBuffer->Map(0, nullptr, &mapped);
		if (FAILED(hr))
		{
			HLogError("DX12ShaderBindingTable: Failed to map SBT buffer");
			return false;
		}
		memcpy(mapped, data, size);
		outBuffer->Unmap(0, nullptr);
		return true;
	}

	bool DX12ShaderBindingTable::create(
		ID3D12StateObjectProperties* pipelineProps,
		ID3D12Device* device,
		D3D12MA::Allocator* allocator,
		const std::wstring& rayGenShaderName,
		const std::vector<std::wstring>& missShaderNames,
		const std::vector<std::wstring>& hitGroupNames,
		const std::vector<std::wstring>& callableShaderNames)
	{
		const uint32_t shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		const uint32_t sbtAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		const uint32_t recordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

		// Ray Gen (single record, no local root arguments for simplicity)
		m_rayGenRecordSize = _align(shaderIdSize, recordAlignment);
		{
			std::vector<uint8_t> sbtData(m_rayGenRecordSize, 0);
			void* shaderId = pipelineProps->GetShaderIdentifier(rayGenShaderName.c_str());
			if (!shaderId)
			{
				HLogError("DX12ShaderBindingTable: RayGen shader identifier not found: {}", std::string(rayGenShaderName.begin(), rayGenShaderName.end()));
				return false;
			}
			memcpy(sbtData.data(), shaderId, shaderIdSize);

			if (!_create_sbt_buffer(device, allocator, sbtData.data(), sbtData.size(), m_rayGenBuffer, m_rayGenAllocation))
				return false;
		}

		// Miss shaders
		m_missRecordSize = _align(shaderIdSize, recordAlignment);
		m_missRecordCount = (uint32_t)missShaderNames.size();
		if (m_missRecordCount > 0)
		{
			uint64_t tableSize = _align(m_missRecordSize * m_missRecordCount, sbtAlignment);
			std::vector<uint8_t> sbtData(tableSize, 0);
			for (uint32_t i = 0; i < m_missRecordCount; ++i)
			{
				void* shaderId = pipelineProps->GetShaderIdentifier(missShaderNames[i].c_str());
				if (!shaderId)
				{
					HLogError("DX12ShaderBindingTable: Miss shader identifier not found");
					return false;
				}
				memcpy(sbtData.data() + i * m_missRecordSize, shaderId, shaderIdSize);
			}
			if (!_create_sbt_buffer(device, allocator, sbtData.data(), sbtData.size(), m_missBuffer, m_missAllocation))
				return false;
		}

		// Hit Groups
		m_hitGroupRecordSize = _align(shaderIdSize, recordAlignment);
		m_hitGroupRecordCount = (uint32_t)hitGroupNames.size();
		if (m_hitGroupRecordCount > 0)
		{
			uint64_t tableSize = _align(m_hitGroupRecordSize * m_hitGroupRecordCount, sbtAlignment);
			std::vector<uint8_t> sbtData(tableSize, 0);
			for (uint32_t i = 0; i < m_hitGroupRecordCount; ++i)
			{
				void* shaderId = pipelineProps->GetShaderIdentifier(hitGroupNames[i].c_str());
				if (!shaderId)
				{
					HLogError("DX12ShaderBindingTable: HitGroup shader identifier not found");
					return false;
				}
				memcpy(sbtData.data() + i * m_hitGroupRecordSize, shaderId, shaderIdSize);
			}
			if (!_create_sbt_buffer(device, allocator, sbtData.data(), sbtData.size(), m_hitGroupBuffer, m_hitGroupAllocation))
				return false;
		}

		// Callable shaders
		m_callableRecordSize = _align(shaderIdSize, recordAlignment);
		m_callableRecordCount = (uint32_t)callableShaderNames.size();
		if (m_callableRecordCount > 0)
		{
			uint64_t tableSize = _align(m_callableRecordSize * m_callableRecordCount, sbtAlignment);
			std::vector<uint8_t> sbtData(tableSize, 0);
			for (uint32_t i = 0; i < m_callableRecordCount; ++i)
			{
				void* shaderId = pipelineProps->GetShaderIdentifier(callableShaderNames[i].c_str());
				if (!shaderId)
				{
					HLogError("DX12ShaderBindingTable: Callable shader identifier not found");
					return false;
				}
				memcpy(sbtData.data() + i * m_callableRecordSize, shaderId, shaderIdSize);
			}
			if (!_create_sbt_buffer(device, allocator, sbtData.data(), sbtData.size(), m_callableBuffer, m_callableAllocation))
				return false;
		}

		return true;
	}

	void DX12ShaderBindingTable::destroy()
	{
		if (m_rayGenAllocation) { m_rayGenAllocation->Release(); m_rayGenAllocation = nullptr; }
		if (m_missAllocation) { m_missAllocation->Release(); m_missAllocation = nullptr; }
		if (m_hitGroupAllocation) { m_hitGroupAllocation->Release(); m_hitGroupAllocation = nullptr; }
		if (m_callableAllocation) { m_callableAllocation->Release(); m_callableAllocation = nullptr; }
		m_rayGenBuffer.Reset();
		m_missBuffer.Reset();
		m_hitGroupBuffer.Reset();
		m_callableBuffer.Reset();
	}

	D3D12_DISPATCH_RAYS_DESC DX12ShaderBindingTable::get_dispatch_desc(uint32_t width, uint32_t height, uint32_t depth) const
	{
		D3D12_DISPATCH_RAYS_DESC desc{};

		// Ray Gen
		if (m_rayGenBuffer)
		{
			desc.RayGenerationShaderRecord.StartAddress = m_rayGenBuffer->GetGPUVirtualAddress();
			desc.RayGenerationShaderRecord.SizeInBytes = m_rayGenRecordSize;
		}

		// Miss
		if (m_missBuffer)
		{
			desc.MissShaderTable.StartAddress = m_missBuffer->GetGPUVirtualAddress();
			desc.MissShaderTable.SizeInBytes = m_missRecordSize * m_missRecordCount;
			desc.MissShaderTable.StrideInBytes = m_missRecordSize;
		}

		// Hit Group
		if (m_hitGroupBuffer)
		{
			desc.HitGroupTable.StartAddress = m_hitGroupBuffer->GetGPUVirtualAddress();
			desc.HitGroupTable.SizeInBytes = m_hitGroupRecordSize * m_hitGroupRecordCount;
			desc.HitGroupTable.StrideInBytes = m_hitGroupRecordSize;
		}

		// Callable
		if (m_callableBuffer)
		{
			desc.CallableShaderTable.StartAddress = m_callableBuffer->GetGPUVirtualAddress();
			desc.CallableShaderTable.SizeInBytes = m_callableRecordSize * m_callableRecordCount;
			desc.CallableShaderTable.StrideInBytes = m_callableRecordSize;
		}

		desc.Width = width;
		desc.Height = height;
		desc.Depth = depth;

		return desc;
	}

	// ==========================================
	// DX12RayTracingPipeline Implementation
	// ==========================================
	DX12RayTracingPipeline::~DX12RayTracingPipeline()
	{
		destroy();
	}

	bool DX12RayTracingPipeline::create(const DX12RayTracingPipelineDesc& desc, ID3D12Device5* device)
	{
		// Build state object using CD3DX12_STATE_OBJECT_DESC pattern manually
		// We'll construct D3D12_STATE_OBJECT_DESC with subobjects

		std::vector<D3D12_STATE_SUBOBJECT> subobjects;
		// Temporary storage for structs that subobjects point to
		std::vector<D3D12_DXIL_LIBRARY_DESC> dxilLibDescs;
		std::vector<std::vector<D3D12_EXPORT_DESC>> exportDescsStorage;
		std::vector<D3D12_HIT_GROUP_DESC> hitGroupDescs;
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
		D3D12_GLOBAL_ROOT_SIGNATURE globalRSDesc{};
		D3D12_LOCAL_ROOT_SIGNATURE localRSDesc{};

		// Reserve storage to prevent reallocation invalidating pointers
		dxilLibDescs.reserve(desc.shaderLibraries.size());
		exportDescsStorage.reserve(desc.shaderLibraries.size());
		hitGroupDescs.reserve(desc.hitGroups.size());
		subobjects.reserve(
			desc.shaderLibraries.size() +
			desc.hitGroups.size() +
			4); // shader config + pipeline config + root sigs

		// 1. DXIL Libraries
		for (const auto& lib : desc.shaderLibraries)
		{
			D3D12_DXIL_LIBRARY_DESC libDesc{};
			auto bytecode = lib.shader->get_d3d12_bytecode();
			libDesc.DXILLibrary = bytecode;

			std::vector<D3D12_EXPORT_DESC> exports;
			for (const auto& name : lib.exportNames)
			{
				D3D12_EXPORT_DESC exportDesc{};
				exportDesc.Name = name.c_str();
				exportDesc.ExportToRename = nullptr;
				exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
				exports.push_back(exportDesc);
			}
			exportDescsStorage.push_back(std::move(exports));

			libDesc.NumExports = (UINT)exportDescsStorage.back().size();
			libDesc.pExports = exportDescsStorage.back().data();
			dxilLibDescs.push_back(libDesc);

			D3D12_STATE_SUBOBJECT sub{};
			sub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
			sub.pDesc = &dxilLibDescs.back();
			subobjects.push_back(sub);
		}

		// 2. Hit Groups
		for (const auto& hg : desc.hitGroups)
		{
			D3D12_HIT_GROUP_DESC hgDesc{};
			hgDesc.HitGroupExport = hg.name.c_str();
			hgDesc.Type = hg.type;
			hgDesc.ClosestHitShaderImport = hg.closestHitShader.empty() ? nullptr : hg.closestHitShader.c_str();
			hgDesc.AnyHitShaderImport = hg.anyHitShader.empty() ? nullptr : hg.anyHitShader.c_str();
			hgDesc.IntersectionShaderImport = hg.intersectionShader.empty() ? nullptr : hg.intersectionShader.c_str();
			hitGroupDescs.push_back(hgDesc);

			D3D12_STATE_SUBOBJECT sub{};
			sub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
			sub.pDesc = &hitGroupDescs.back();
			subobjects.push_back(sub);
		}

		// 3. Shader Config
		shaderConfig.MaxPayloadSizeInBytes = desc.maxPayloadSize;
		shaderConfig.MaxAttributeSizeInBytes = desc.maxAttributeSize;
		{
			D3D12_STATE_SUBOBJECT sub{};
			sub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
			sub.pDesc = &shaderConfig;
			subobjects.push_back(sub);
		}

		// 4. Pipeline Config
		pipelineConfig.MaxTraceRecursionDepth = desc.maxTraceRecursionDepth;
		{
			D3D12_STATE_SUBOBJECT sub{};
			sub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
			sub.pDesc = &pipelineConfig;
			subobjects.push_back(sub);
		}

		// 5. Global Root Signature
		if (desc.globalRootSignature)
		{
			globalRSDesc.pGlobalRootSignature = desc.globalRootSignature;
			D3D12_STATE_SUBOBJECT sub{};
			sub.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
			sub.pDesc = &globalRSDesc;
			subobjects.push_back(sub);
		}

		// 6. Local Root Signature (optional)
		if (desc.localRootSignature)
		{
			localRSDesc.pLocalRootSignature = desc.localRootSignature;
			D3D12_STATE_SUBOBJECT sub{};
			sub.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			sub.pDesc = &localRSDesc;
			subobjects.push_back(sub);
		}

		// Create State Object
		D3D12_STATE_OBJECT_DESC stateObjectDesc{};
		stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		stateObjectDesc.NumSubobjects = (UINT)subobjects.size();
		stateObjectDesc.pSubobjects = subobjects.data();

		HRESULT hr = device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_stateObject));
		if (FAILED(hr))
		{
			HLogError("DX12RayTracingPipeline: Failed to create state object. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		// Get properties for shader identifiers
		hr = m_stateObject->QueryInterface(IID_PPV_ARGS(&m_properties));
		if (FAILED(hr))
		{
			HLogError("DX12RayTracingPipeline: Failed to query state object properties. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		return true;
	}

	void DX12RayTracingPipeline::destroy()
	{
		m_properties.Reset();
		m_stateObject.Reset();
	}

	// ==========================================
	// DX12RayTracingRenderProgram Implementation
	// ==========================================
	DX12RayTracingRenderProgram::~DX12RayTracingRenderProgram()
	{
		destroy();
	}

	bool DX12RayTracingRenderProgram::create(const DX12RayTracingPipelineDesc& desc)
	{
		m_desc = desc;

		if (!_build_root_signature(desc))
			return false;

		if (!_build_pipeline(desc))
			return false;

		if (!_build_sbt(desc))
			return false;

		return true;
	}

	void DX12RayTracingRenderProgram::destroy()
	{
		if (m_sbt) { m_sbt->destroy(); m_sbt.reset(); }
		if (m_pipeline) { m_pipeline->destroy(); m_pipeline.reset(); }
		m_rootSignatureLayout.reset();
	}

	bool DX12RayTracingRenderProgram::_build_root_signature(const DX12RayTracingPipelineDesc& desc)
	{
		// If a global root signature is already provided, use it
		if (desc.globalRootSignature)
			return true;

		// Otherwise build from shader reflection
		// For RT, typically use a global root signature with descriptor tables
		auto* ctx = DX12Context::get();
		auto* device = ctx->get_device();

		// Build a simple global root signature with a CBV_SRV_UAV descriptor table + sampler table
		std::vector<D3D12_ROOT_PARAMETER> rootParams;
		std::vector<D3D12_DESCRIPTOR_RANGE> cbvSrvUavRanges;
		std::vector<D3D12_DESCRIPTOR_RANGE> samplerRanges;

		// SRV range (t0, space0) - unbounded for bindless
		{
			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			range.NumDescriptors = UINT_MAX; // unbounded
			range.BaseShaderRegister = 0;
			range.RegisterSpace = 0;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			cbvSrvUavRanges.push_back(range);
		}

		// UAV range (u0, space0)
		{
			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			range.NumDescriptors = UINT_MAX;
			range.BaseShaderRegister = 0;
			range.RegisterSpace = 0;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			cbvSrvUavRanges.push_back(range);
		}

		// CBV_SRV_UAV table
		{
			D3D12_ROOT_PARAMETER param{};
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			param.DescriptorTable.NumDescriptorRanges = (UINT)cbvSrvUavRanges.size();
			param.DescriptorTable.pDescriptorRanges = cbvSrvUavRanges.data();
			param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(param);
		}

		// CBV as root descriptor (b0)
		{
			D3D12_ROOT_PARAMETER param{};
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			param.Descriptor.ShaderRegister = 0;
			param.Descriptor.RegisterSpace = 0;
			param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(param);
		}

		// Sampler range
		{
			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			range.NumDescriptors = UINT_MAX;
			range.BaseShaderRegister = 0;
			range.RegisterSpace = 0;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			samplerRanges.push_back(range);
		}

		{
			D3D12_ROOT_PARAMETER param{};
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			param.DescriptorTable.NumDescriptorRanges = (UINT)samplerRanges.size();
			param.DescriptorTable.pDescriptorRanges = samplerRanges.data();
			param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(param);
		}

		D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
		rootSigDesc.NumParameters = (UINT)rootParams.size();
		rootSigDesc.pParameters = rootParams.data();
		rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		m_rootSignatureLayout = std::make_unique<DX12DescriptorSetLayout>();
		if (!m_rootSignatureLayout->init(device, rootSigDesc))
		{
			HLogError("DX12RayTracingRenderProgram: Failed to create root signature");
			return false;
		}

		return true;
	}

	bool DX12RayTracingRenderProgram::_build_pipeline(const DX12RayTracingPipelineDesc& desc)
	{
		auto* ctx = DX12Context::get();
		auto* device = ctx->get_device();

		DX12RayTracingPipelineDesc pipelineDesc = desc;
		if (!desc.globalRootSignature && m_rootSignatureLayout)
		{
			pipelineDesc.globalRootSignature = m_rootSignatureLayout->get_root_signature();
		}

		m_pipeline = std::make_unique<DX12RayTracingPipeline>();
		if (!m_pipeline->create(pipelineDesc, device))
		{
			HLogError("DX12RayTracingRenderProgram: Failed to create RT pipeline");
			return false;
		}

		return true;
	}

	bool DX12RayTracingRenderProgram::_build_sbt(const DX12RayTracingPipelineDesc& desc)
	{
		auto* ctx = DX12Context::get();
		auto* device = ctx->get_device();
		auto* allocator = ctx->get_d3d12ma_allocator();

		// Collect hit group names
		std::vector<std::wstring> hitGroupNames;
		for (const auto& hg : desc.hitGroups)
		{
			hitGroupNames.push_back(hg.name);
		}

		m_sbt = std::make_unique<DX12ShaderBindingTable>();
		if (!m_sbt->create(
			m_pipeline->get_properties(),
			device,
			allocator,
			desc.rayGenExport,
			desc.missExports,
			hitGroupNames,
			desc.callableExports))
		{
			HLogError("DX12RayTracingRenderProgram: Failed to create SBT");
			return false;
		}

		return true;
	}

	void DX12RayTracingRenderProgram::dispatch_rays(DX12CommandBuffer* cmdBuffer, uint32_t width, uint32_t height, uint32_t depth)
	{
		if (!m_pipeline || !m_sbt)
		{
			HLogError("DX12RayTracingRenderProgram: Pipeline or SBT not created");
			return;
		}

		auto* cmdList = cmdBuffer->get_command_list();

		// Set pipeline state
		cmdList->SetPipelineState1(m_pipeline->get_state_object());

		// Set root signature
		if (m_rootSignatureLayout)
		{
			cmdList->SetComputeRootSignature(m_rootSignatureLayout->get_root_signature());
		}

		// Get dispatch rays desc from SBT
		D3D12_DISPATCH_RAYS_DESC dispatchDesc = m_sbt->get_dispatch_desc(width, height, depth);

		// Dispatch rays
		cmdList->DispatchRays(&dispatchDesc);
	}
}
