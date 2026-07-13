#include "DX12RenderProgram.h"
#include "Graphics/RasterizerConvention.h"
#include "DX12Context.h"
#include "DX12CommandBuffer.h"
#include "DX12DescriptorHeap.h"
#include "DX12RenderPass.h"
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace RHI
{
	namespace
	{
		static constexpr const char* k_dx12_root_constants_name = "AshRootConstants";
		static constexpr uint32_t k_dx12_max_root_constants_dwords = 64;

		static uint32_t get_vertex_format_size(AshVertexComponentFormat format)
		{
			switch (format)
			{
			case Float: return 4;
			case Float2: return 8;
			case Float3: return 12;
			case Float4: return 16;
			case Mat4: return 64;
			case Byte: return 1;
			case Byte4N: return 4;
			case UByte: return 1;
			case UByte4N: return 4;
			case Short2: return 4;
			case Short2N: return 4;
			case Short4: return 8;
			case Short4N: return 8;
			case Uint: return 4;
			case Uint2: return 8;
			case Uint4: return 16;
			default: return 0;
			}
		}

		static const char* descriptor_range_type_to_string(D3D12_DESCRIPTOR_RANGE_TYPE type)
		{
			switch (type)
			{
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return "SRV";
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return "UAV";
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return "CBV";
			case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: return "Sampler";
			default: return "Unknown";
			}
		}

		static bool pending_bind_matches_range_type(AshResourceViewType viewType, D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
		{
			switch (rangeType)
			{
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
				return viewType == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
				return viewType == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
				return viewType == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_CBV;
			case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
				return viewType == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SAMPLER;
			default:
				return false;
			}
		}

		static bool descriptor_range_uses_sampler_heap(D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
		{
			return rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		}

		static D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type_for_range(D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
		{
			return descriptor_range_uses_sampler_heap(rangeType) ?
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		}

		static bool is_root_constants_cbuffer_name(const std::string& name)
		{
			return name == k_dx12_root_constants_name || name == "RootConstants";
		}

		static D3D12_INPUT_CLASSIFICATION vertex_binding_input_class(const VertexInputCreation& vi, uint16_t binding)
		{
			for (uint32_t s = 0; s < vi.num_vertex_streams; ++s)
			{
				if (vi.vertex_streams[s].binding == binding)
				{
					return vi.vertex_streams[s].input_rate == AshVertexInputRate::PerInstance
						? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
						: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				}
			}
			return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		}

		static bool resolve_explicit_vertex_semantic(const VertexAttribute& attr, const char** out_name, UINT* out_index, const char* debug_name)
		{
			if (attr.semantic_name[0] != '\0')
			{
				*out_name = attr.semantic_name;
				*out_index = attr.semantic_index;
				return true;
			}
			if (attr.semantic != AshVertexSemantic::Unspecified)
			{
				switch (attr.semantic)
				{
				case AshVertexSemantic::Position: *out_name = "POSITION"; *out_index = 0; return true;
				case AshVertexSemantic::Normal: *out_name = "NORMAL"; *out_index = 0; return true;
				case AshVertexSemantic::Tangent: *out_name = "TANGENT"; *out_index = 0; return true;
				case AshVertexSemantic::TexCoord0: *out_name = "TEXCOORD"; *out_index = 0; return true;
				case AshVertexSemantic::TexCoord1: *out_name = "TEXCOORD"; *out_index = 1; return true;
				case AshVertexSemantic::Color0: *out_name = "COLOR"; *out_index = 0; return true;
				default: break;
				}
				HLogError(
					"DX12 render program '{}' has explicit vertex input with unknown AshVertexSemantic value.",
					debug_name ? debug_name : "DX12GraphicsRenderProgram");
				return false;
			}
			HLogError(
				"DX12 render program '{}' explicit vertex attribute at location {} has no semantic binding information.",
				debug_name ? debug_name : "DX12GraphicsRenderProgram",
				static_cast<unsigned>(attr.location));
			return false;
		}

		static bool merge_root_constants_info(
			const DX12ShaderBindingInfo& binding,
			DX12RootConstantsInfo& outRootConstants,
			const char* debugName)
		{
			const uint32_t alignedByteSize = (binding.byteSize + 3u) & ~3u;
			const uint32_t dwordCount = alignedByteSize / 4u;
			if (dwordCount > k_dx12_max_root_constants_dwords)
			{
				HLogError(
					"DX12 render program '{}' root constants cbuffer '{}' is {} bytes, exceeding the D3D12 root constant budget of {} bytes.",
					debugName ? debugName : "DX12RenderProgram",
					binding.name.c_str(),
					alignedByteSize,
					k_dx12_max_root_constants_dwords * 4u);
				return false;
			}

			if (!outRootConstants.enabled)
			{
				outRootConstants.enabled = true;
				outRootConstants.rootIndex = 0;
				outRootConstants.shaderRegister = binding.bindPoint;
				outRootConstants.registerSpace = binding.bindSpace;
				outRootConstants.byteSize = binding.byteSize;
				outRootConstants.dwordCount = dwordCount;
				outRootConstants.name = binding.name;
				return true;
			}

			if (outRootConstants.shaderRegister != binding.bindPoint ||
				outRootConstants.registerSpace != binding.bindSpace ||
				outRootConstants.byteSize != binding.byteSize)
			{
				HLogError(
					"DX12 render program '{}' found incompatible root constants cbuffer '{}' across shader stages.",
					debugName ? debugName : "DX12RenderProgram",
					binding.name.c_str());
				return false;
			}

			return true;
		}

		template <typename BindingContainer>
		static bool merge_binding_infos(
			const BindingContainer& bindings,
			D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
			std::vector<DX12RootParamMapping>& outMappings,
			std::unordered_map<std::string, DX12ProgramBindingInfo>& outBindingInfos,
			const char* debugName)
		{
			for (const auto& binding : bindings)
			{
				auto existingIt = outBindingInfos.find(binding.name);
				if (existingIt != outBindingInfos.end())
				{
					const DX12ProgramBindingInfo& existing = existingIt->second;
				if (existing.rangeType != rangeType || existing.descriptorCount != binding.bindCount)
				{
					HLogError(
						"DX12 render program '{}' found incompatible reflected binding '{}'.",
						debugName ? debugName : "DX12RenderProgram",
						binding.name.c_str());
					return false;
				}
				if (existing.baseRegister != binding.bindPoint || existing.registerSpace != binding.bindSpace)
				{
					HLogError(
						"DX12 render program '{}' found incompatible reflected binding '{}'.",
						debugName ? debugName : "DX12RenderProgram",
							binding.name.c_str());
						return false;
					}
					continue;
				}

				DX12RootParamMapping mapping{};
				mapping.name = binding.name;
				mapping.rootIndex = static_cast<uint32_t>(outMappings.size());
				mapping.rangeType = rangeType;
				mapping.numDescriptors = binding.bindCount > 0 ? binding.bindCount : 1u;
				outMappings.push_back(mapping);

				DX12ProgramBindingInfo bindingInfo{};
				bindingInfo.rootIndex = mapping.rootIndex;
				bindingInfo.rangeType = rangeType;
				bindingInfo.descriptorCount = mapping.numDescriptors;
				bindingInfo.baseRegister = binding.bindPoint;
				bindingInfo.registerSpace = binding.bindSpace;
				outBindingInfos.emplace(binding.name, bindingInfo);
			}

			return true;
		}

		static bool validate_pending_bind(
			const char* debugName,
			const DX12PendingBind& bind,
			const std::unordered_map<std::string, DX12ProgramBindingInfo>& bindingInfos,
			std::unordered_set<std::string>& outBoundNames)
		{
			auto bindingIt = bindingInfos.find(bind.name);
			if (bindingIt == bindingInfos.end())
			{
				return true;
			}

			const DX12ProgramBindingInfo& bindingInfo = bindingIt->second;
			if (!pending_bind_matches_range_type(bind.viewType, bindingInfo.rangeType))
			{
				HLogError(
					"DX12 render program '{}' binding '{}' type mismatch. reflected={}, provided={}.",
					debugName ? debugName : "DX12RenderProgram",
					bind.name.c_str(),
					descriptor_range_type_to_string(bindingInfo.rangeType),
					static_cast<uint32_t>(bind.viewType));
				return false;
			}

			const uint32_t providedCount = bind.isArray ? static_cast<uint32_t>(bind.descriptorHandles.size()) : 1u;
			if (providedCount == 0 || providedCount > bindingInfo.descriptorCount)
			{
				HLogError(
					"DX12 render program '{}' binding '{}' exceeded reflected array size. provided={}, reflected={}.",
					debugName ? debugName : "DX12RenderProgram",
					bind.name.c_str(),
					providedCount,
					bindingInfo.descriptorCount);
				return false;
			}

			outBoundNames.insert(bind.name);
			return true;
		}

		static bool commit_descriptor_binds(
			ID3D12Device* device,
			DX12DescriptorHeapManager& heapMgr,
			const std::vector<DX12PendingBind>& pendingBinds,
			const std::unordered_map<std::string, DX12ProgramBindingInfo>& bindingInfos,
			std::vector<DX12CommittedDescriptorTable>& outCommittedTables)
		{
			ASH_PROFILE_SCOPE_NC("DX12RenderProgram::CommitDescriptorBinds", AshEngine::Profile::Color::Descriptor);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(pendingBinds.size()));

			struct PendingDescriptorTable
			{
				D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				uint32_t descriptorCount = 0;
				std::vector<DX12DescriptorHandle> sourceHandles{};
			};

			std::unordered_map<uint32_t, PendingDescriptorTable> pendingTables{};
			for (const DX12PendingBind& bind : pendingBinds)
			{
				auto bindingIt = bindingInfos.find(bind.name);
				if (bindingIt == bindingInfos.end())
				{
					continue;
				}

				const DX12ProgramBindingInfo& bindingInfo = bindingIt->second;
				const uint32_t descriptorCount = bind.isArray ? static_cast<uint32_t>(bind.descriptorHandles.size()) : 1u;
				PendingDescriptorTable& table = pendingTables[bindingInfo.rootIndex];
				table.heapType = descriptor_heap_type_for_range(bindingInfo.rangeType);
				table.descriptorCount = std::max(
					table.descriptorCount,
					bindingInfo.descriptorOffset + descriptorCount);
			}

			for (auto& [rootIndex, table] : pendingTables)
			{
				(void)rootIndex;
				table.sourceHandles.resize(table.descriptorCount);
			}

			for (const DX12PendingBind& bind : pendingBinds)
			{
				auto bindingIt = bindingInfos.find(bind.name);
				if (bindingIt == bindingInfos.end())
				{
					continue;
				}

				const DX12ProgramBindingInfo& bindingInfo = bindingIt->second;
				const uint32_t descriptorCount = bind.isArray ? static_cast<uint32_t>(bind.descriptorHandles.size()) : 1u;
				PendingDescriptorTable& table = pendingTables[bindingInfo.rootIndex];
				if (bind.isArray)
				{
					std::copy(
						bind.descriptorHandles.begin(),
						bind.descriptorHandles.end(),
						table.sourceHandles.begin() + bindingInfo.descriptorOffset);
				}
				else
				{
					table.sourceHandles[bindingInfo.descriptorOffset] = bind.descriptorHandle;
				}
			}

			std::vector<uint32_t> rootIndices{};
			rootIndices.reserve(pendingTables.size());
			for (const auto& [rootIndex, table] : pendingTables)
			{
				(void)table;
				rootIndices.push_back(rootIndex);
			}
			std::sort(rootIndices.begin(), rootIndices.end());

			bool committed = true;
			for (uint32_t rootIndex : rootIndices)
			{
				PendingDescriptorTable& table = pendingTables[rootIndex];
				if (table.sourceHandles.empty())
				{
					continue;
				}
				DX12DescriptorHandle gpuHandle{};
				if (!heapMgr.find_or_create_shader_visible_table(
					device,
					table.heapType,
					table.sourceHandles.data(),
					static_cast<uint32_t>(table.sourceHandles.size()),
					gpuHandle))
				{
					HLogError(
						"DX12 render program failed to allocate/cache {} shader-visible {} descriptors for binding '{}'.",
						static_cast<uint32_t>(table.sourceHandles.size()),
						table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? "Sampler" : "CBV/SRV/UAV",
						"descriptor table");
					committed = false;
					break;
				}

				outCommittedTables.push_back({ rootIndex, gpuHandle.gpuHandle });
			}
			return committed;
		}

		static void apply_committed_descriptor_tables(
			ID3D12GraphicsCommandList4* cmdList,
			const std::vector<DX12CommittedDescriptorTable>& committedTables,
			bool graphicsPipeline)
		{
			ASH_PROFILE_SCOPE_NC("DX12RenderProgram::ApplyDescriptorTables", AshEngine::Profile::Color::Descriptor);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(committedTables.size()));
			for (const DX12CommittedDescriptorTable& table : committedTables)
			{
				if (table.rootIndex == UINT32_MAX || table.gpuHandle.ptr == 0)
				{
					continue;
				}

				if (graphicsPipeline)
				{
					cmdList->SetGraphicsRootDescriptorTable(table.rootIndex, table.gpuHandle);
				}
				else
				{
					cmdList->SetComputeRootDescriptorTable(table.rootIndex, table.gpuHandle);
				}
			}
		}

		template <typename SetRootConstantsFn>
		static void apply_root_constants(
			const DX12RootConstantsInfo& rootConstants,
			const std::vector<uint8_t>& constDataBlock,
			SetRootConstantsFn&& setRootConstants)
		{
			if (!rootConstants.enabled)
			{
				return;
			}

			std::array<uint32_t, k_dx12_max_root_constants_dwords> dwords{};
			const uint32_t dwordCount = std::min<uint32_t>(rootConstants.dwordCount, static_cast<uint32_t>(dwords.size()));
			if (!constDataBlock.empty())
			{
				std::memcpy(dwords.data(), constDataBlock.data(), std::min<size_t>(constDataBlock.size(), static_cast<size_t>(dwordCount) * sizeof(uint32_t)));
			}

			setRootConstants(rootConstants.rootIndex, dwordCount, dwords.data());
		}
	}

	DX12GraphicsRenderProgram::~DX12GraphicsRenderProgram()
	{
		destroy();
	}

	bool DX12GraphicsRenderProgram::create(const GraphicProgramCreateDesc& desc)
	{
		destroy();
		m_desc = desc;
		m_renderState.rasterization = desc.pipeline.rasterization;
		m_renderState.depth_stencil = desc.pipeline.depth_stencil;
		m_renderState.blend_state = desc.pipeline.blend_state;
		m_renderState.primitive_topology = desc.pipeline.primitiveTopology;
		if (desc.pipeline.viewport)
		{
			m_renderState.set_viewport_state(*desc.pipeline.viewport);
		}
		else
		{
			m_renderState.clear_viewport_state();
		}

		m_binder.set_heap_manager(&DX12Context::get()->get_descriptor_heaps());

		m_shaders.clear();
		for (uint32_t i = 0; i < desc.pipeline.shaders.stages_count; ++i)
		{
			const auto& stage = desc.pipeline.shaders.stages[i];
			if (stage.shader)
			{
				auto shader = std::dynamic_pointer_cast<DX12Shader>(stage.shader);
				if (!shader)
				{
					HLogError("DX12GraphicsRenderProgram: Non-DX12 shader provided in pipeline '{}'.", desc.pipeline.name ? desc.pipeline.name : "DX12GraphicsRenderProgram");
					return false;
				}
				m_shaders.push_back(shader);
			}
		}

		if (!_cache_reflection_data())
		{
			return false;
		}
		if (!_build_root_signature())
		{
			return false;
		}
		if (!_build_pso())
		{
			return false;
		}

		m_needsRebuild = false;
		return true;
	}

	bool DX12GraphicsRenderProgram::destroy()
	{
		m_pipeline.reset();
		m_rootSignatureLayout.reset();
		m_shaders.clear();
		m_rootParamMappings.clear();
		m_bindingInfos.clear();
		m_boundResourceNames.clear();
		m_vertexInputs.clear();
		m_rootConstants = {};
		m_constDataBlock.clear();
		m_committedDescriptorTables.clear();
		m_binder.clear();
		m_needsRebuild = true;
		return true;
	}

	bool DX12GraphicsRenderProgram::apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall)
	{
		if (fnRenderStateDefineCall)
		{
			fnRenderStateDefineCall(&m_renderState);
		}
		m_needsRebuild = true;
		return true;
	}

	bool DX12GraphicsRenderProgram::set_const_data_block(uint32_t size, const void* data)
	{
		if (size > 0 && !data)
		{
			return false;
		}

		if (m_rootConstants.enabled && size > m_rootConstants.byteSize)
		{
			HLogError(
				"DX12 render program '{}' received a const data block of {} bytes, exceeding root constants cbuffer '{}' size {} bytes.",
				m_desc.pipeline.name ? m_desc.pipeline.name : "DX12GraphicsRenderProgram",
				size,
				m_rootConstants.name.c_str(),
				m_rootConstants.byteSize);
			return false;
		}

		m_constDataBlock.resize(size);
		if (size > 0)
		{
			std::memcpy(m_constDataBlock.data(), data, size);
		}
		return true;
	}

	bool DX12GraphicsRenderProgram::apply(std::shared_ptr<CommandBuffer> cb)
	{
		ASH_PROFILE_SCOPE_NC("DX12GraphicsRenderProgram::Apply", AshEngine::Profile::Color::Pipeline);
		if (m_desc.pipeline.name)
		{
			ASH_PROFILE_SCOPE_TEXT(m_desc.pipeline.name, std::strlen(m_desc.pipeline.name));
		}
		if (m_needsRebuild)
		{
			if (!_build_pso())
			{
				return false;
			}
			m_needsRebuild = false;
		}
		if (!m_pipeline || !m_rootSignatureLayout)
		{
			return false;
		}

		auto* dx12Cb = static_cast<DX12CommandBuffer*>(cb.get());
		auto* cmdList = dx12Cb->get_command_list();
		cmdList->SetPipelineState(m_pipeline->get_pso());
		cmdList->SetGraphicsRootSignature(m_rootSignatureLayout->get_root_signature());
		cmdList->IASetPrimitiveTopology(ash_to_d3d_primitive_topology(m_renderState.primitive_topology));
		apply_root_constants(
			m_rootConstants,
			m_constDataBlock,
			[cmdList](uint32_t rootIndex, uint32_t dwordCount, const uint32_t* data)
			{
				cmdList->SetGraphicsRoot32BitConstants(rootIndex, dwordCount, data, 0);
			});
		_apply_bindings(dx12Cb);
		return true;
	}

	IRenderProgramBinder& DX12GraphicsRenderProgram::begin_bind()
	{
		return m_binder.begin_bind();
	}

	bool DX12GraphicsRenderProgram::end_bind()
	{
		ASH_PROFILE_SCOPE_NC("DX12GraphicsRenderProgram::EndBind", AshEngine::Profile::Color::Descriptor);
		m_binder.finish_binding();
		if (!_validate_binding_state())
		{
			m_binder.clear();
			m_committedDescriptorTables.clear();
			return false;
		}

		auto* device = DX12Context::get()->get_device();
		auto& heapMgr = DX12Context::get()->get_descriptor_heaps();
		m_committedDescriptorTables.clear();
		m_committedDescriptorTables.reserve(m_binder.get_pending_binds().size() + m_binder.get_pending_sampler_binds().size());
		const bool committed =
			commit_descriptor_binds(device, heapMgr, m_binder.get_pending_binds(), m_bindingInfos, m_committedDescriptorTables) &&
			commit_descriptor_binds(device, heapMgr, m_binder.get_pending_sampler_binds(), m_bindingInfos, m_committedDescriptorTables);
		m_binder.clear();
		if (!committed)
		{
			m_committedDescriptorTables.clear();
		}
		return committed;
	}

	bool DX12GraphicsRenderProgram::_cache_reflection_data()
	{
		m_rootParamMappings.clear();
		m_bindingInfos.clear();
		m_boundResourceNames.clear();
		m_vertexInputs.clear();

		for (const auto& shader : m_shaders)
		{
			const DX12ShaderReflectionData& reflection = shader->get_reflection_data();
			for (const DX12ShaderBindingInfo& cbuffer : reflection.cbuffers)
			{
				if (is_root_constants_cbuffer_name(cbuffer.name))
				{
					if (!merge_root_constants_info(cbuffer, m_rootConstants, m_desc.pipeline.name))
					{
						return false;
					}
					continue;
				}

				if (!merge_binding_infos(std::array<DX12ShaderBindingInfo, 1>{cbuffer}, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
				{
					return false;
				}
			}
			if (!merge_binding_infos(reflection.srvs, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
			{
				return false;
			}
			if (!merge_binding_infos(reflection.uavs, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
			{
				return false;
			}
			if (!merge_binding_infos(reflection.samplers, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
			{
				return false;
			}

			if (shader->get_stage() == ASH_SHADER_STAGE_VERTEX_BIT)
			{
				m_vertexInputs = reflection.vertexInputs;
			}
		}

		if (m_rootConstants.enabled)
		{
			for (DX12RootParamMapping& mapping : m_rootParamMappings)
			{
				++mapping.rootIndex;
			}
			for (auto& [name, bindingInfo] : m_bindingInfos)
			{
				(void)name;
				++bindingInfo.rootIndex;
			}
		}

		return true;
	}

	bool DX12GraphicsRenderProgram::_validate_binding_state()
	{
		m_boundResourceNames.clear();

		for (const DX12PendingBind& bind : m_binder.get_pending_binds())
		{
			if (!validate_pending_bind(m_desc.pipeline.name, bind, m_bindingInfos, m_boundResourceNames))
			{
				return false;
			}
		}

		for (const DX12PendingBind& bind : m_binder.get_pending_sampler_binds())
		{
			if (!validate_pending_bind(m_desc.pipeline.name, bind, m_bindingInfos, m_boundResourceNames))
			{
				return false;
			}
		}

		for (const auto& [name, bindingInfo] : m_bindingInfos)
		{
			if (m_boundResourceNames.find(name) == m_boundResourceNames.end())
			{
				HLogError(
					"DX12 render program '{}' requires reflected resource '{}' (rootIndex={}) but C++ did not bind it.",
					m_desc.pipeline.name ? m_desc.pipeline.name : "DX12GraphicsRenderProgram",
					name.c_str(),
					bindingInfo.rootIndex);
				return false;
			}
		}

		return true;
	}

	bool DX12GraphicsRenderProgram::_build_root_signature()
	{
		auto* device = DX12Context::get()->get_device();
		std::vector<D3D12_ROOT_PARAMETER> rootParams;
		std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
		rootParams.reserve(3u);
		ranges.reserve(m_bindingInfos.size());

		if (m_rootConstants.enabled)
		{
			D3D12_ROOT_PARAMETER rootConstantsParam{};
			rootConstantsParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			rootConstantsParam.Constants.Num32BitValues = m_rootConstants.dwordCount;
			rootConstantsParam.Constants.ShaderRegister = m_rootConstants.shaderRegister;
			rootConstantsParam.Constants.RegisterSpace = m_rootConstants.registerSpace;
			rootConstantsParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(rootConstantsParam);
		}

		struct DescriptorRangeBuildItem
		{
			std::string name{};
			D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			uint32_t descriptorCount = 1;
			uint32_t baseRegister = 0;
			uint32_t registerSpace = 0;
		};

		std::vector<DescriptorRangeBuildItem> rangeItems{};
		rangeItems.reserve(m_bindingInfos.size());
		for (const auto& [name, bindingInfo] : m_bindingInfos)
		{
			DescriptorRangeBuildItem item{};
			item.name = name;
			item.rangeType = bindingInfo.rangeType;
			item.descriptorCount = bindingInfo.descriptorCount;
			item.baseRegister = bindingInfo.baseRegister;
			item.registerSpace = bindingInfo.registerSpace;
			rangeItems.push_back(std::move(item));
		}
		std::sort(
			rangeItems.begin(),
			rangeItems.end(),
			[](const DescriptorRangeBuildItem& lhs, const DescriptorRangeBuildItem& rhs)
			{
				if (descriptor_range_uses_sampler_heap(lhs.rangeType) != descriptor_range_uses_sampler_heap(rhs.rangeType))
				{
					return !descriptor_range_uses_sampler_heap(lhs.rangeType);
				}
				if (lhs.registerSpace != rhs.registerSpace)
				{
					return lhs.registerSpace < rhs.registerSpace;
				}
				if (lhs.rangeType != rhs.rangeType)
				{
					return lhs.rangeType < rhs.rangeType;
				}
				if (lhs.baseRegister != rhs.baseRegister)
				{
					return lhs.baseRegister < rhs.baseRegister;
				}
				return lhs.name < rhs.name;
			});

		const auto add_descriptor_table = [&](bool samplerTable)
		{
			const size_t rangeStart = ranges.size();
			uint32_t descriptorOffset = 0;
			const uint32_t rootIndex = static_cast<uint32_t>(rootParams.size());
			for (const DescriptorRangeBuildItem& item : rangeItems)
			{
				if (descriptor_range_uses_sampler_heap(item.rangeType) != samplerTable)
				{
					continue;
				}

				D3D12_DESCRIPTOR_RANGE range{};
				range.RangeType = item.rangeType;
				range.NumDescriptors = item.descriptorCount > 0 ? item.descriptorCount : 1u;
				range.BaseShaderRegister = item.baseRegister;
				range.RegisterSpace = item.registerSpace;
				range.OffsetInDescriptorsFromTableStart = descriptorOffset;
				ranges.push_back(range);

				DX12ProgramBindingInfo& bindingInfo = m_bindingInfos[item.name];
				bindingInfo.rootIndex = rootIndex;
				bindingInfo.descriptorOffset = descriptorOffset;
				descriptorOffset += range.NumDescriptors;
			}

			const size_t rangeCount = ranges.size() - rangeStart;
			if (rangeCount == 0u)
			{
				return;
			}

			D3D12_ROOT_PARAMETER param{};
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeCount);
			param.DescriptorTable.pDescriptorRanges = ranges.data() + rangeStart;
			param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(param);
		};

		add_descriptor_table(false);
		add_descriptor_table(true);

		D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
		rootSigDesc.NumParameters = static_cast<UINT>(rootParams.size());
		rootSigDesc.pParameters = rootParams.empty() ? nullptr : rootParams.data();
		rootSigDesc.NumStaticSamplers = 0;
		rootSigDesc.pStaticSamplers = nullptr;
		rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		m_rootSignatureLayout = std::make_unique<DX12DescriptorSetLayout>();
		const bool initialized = m_rootSignatureLayout->init(device, rootSigDesc);
		if (initialized && m_rootSignatureLayout->get_root_signature())
		{
			std::string debugName = (m_desc.pipeline.name ? m_desc.pipeline.name : "DX12GraphicsRenderProgram");
			debugName += ".RootSignature";
			dx12_set_debug_name(m_rootSignatureLayout->get_root_signature(), debugName.c_str());
		}
		return initialized;
	}

	bool DX12GraphicsRenderProgram::_build_pso()
	{
		auto* device = DX12Context::get()->get_device();

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = m_rootSignatureLayout ? m_rootSignatureLayout->get_root_signature() : nullptr;

		for (const auto& shader : m_shaders)
		{
			switch (shader->get_stage())
			{
			case ASH_SHADER_STAGE_VERTEX_BIT:   psoDesc.VS = shader->get_d3d12_bytecode(); break;
			case ASH_SHADER_STAGE_FRAGMENT_BIT: psoDesc.PS = shader->get_d3d12_bytecode(); break;
			case ASH_SHADER_STAGE_GEOMETRY_BIT: psoDesc.GS = shader->get_d3d12_bytecode(); break;
			case ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT: psoDesc.HS = shader->get_d3d12_bytecode(); break;
			case ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: psoDesc.DS = shader->get_d3d12_bytecode(); break;
			default: break;
			}
		}

		const auto& blend = m_renderState.blend_state;
		psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
		psoDesc.BlendState.IndependentBlendEnable = blend.active_states > 1 ? TRUE : FALSE;
		for (uint32_t i = 0; i < 8; ++i)
		{
			const auto& src = i < blend.active_states ? blend.blend_states[i] : blend.blend_states[0];
			auto& dst = psoDesc.BlendState.RenderTarget[i];
			dst.BlendEnable = src.blend_enabled;
			dst.SrcBlend = ash_to_d3d12_blend(src.source_color);
			dst.DestBlend = ash_to_d3d12_blend(src.destination_color);
			dst.BlendOp = ash_to_d3d12_blend_op(src.color_operation);
			dst.SrcBlendAlpha = ash_to_d3d12_blend(src.source_alpha);
			dst.DestBlendAlpha = ash_to_d3d12_blend(src.destination_alpha);
			dst.BlendOpAlpha = ash_to_d3d12_blend_op(src.alpha_operation);
			dst.RenderTargetWriteMask = ash_to_d3d12_color_write_mask(src.color_write_mask);
		}

		const auto& raster = m_renderState.rasterization;
		psoDesc.RasterizerState.FillMode = ash_to_d3d12_fill_mode(raster.fill);
		psoDesc.RasterizerState.CullMode = ash_to_d3d12_cull_mode(raster.cull_mode);
		psoDesc.RasterizerState.FrontCounterClockwise =
			mesh_winding_to_d3d12_front_counter_clockwise_bool(raster.front) ? TRUE : FALSE;
		psoDesc.RasterizerState.DepthBias = 0;
		psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
		psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
		psoDesc.RasterizerState.DepthClipEnable = TRUE;
		psoDesc.RasterizerState.MultisampleEnable = FALSE;
		psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
		psoDesc.RasterizerState.ForcedSampleCount = 0;
		psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		const auto& ds = m_renderState.depth_stencil;
		psoDesc.DepthStencilState.DepthEnable = ds.depth_enable;
		psoDesc.DepthStencilState.DepthWriteMask = ds.depth_write_enable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.DepthFunc = ash_to_d3d12_comparison(ds.depth_comparison);
		psoDesc.DepthStencilState.StencilEnable = ds.stencil_enable;
		psoDesc.DepthStencilState.StencilReadMask = static_cast<UINT8>(ds.front.compare_mask);
		psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(ds.front.write_mask);
		psoDesc.DepthStencilState.FrontFace.StencilFailOp = ash_to_d3d12_stencil_op(ds.front.fail);
		psoDesc.DepthStencilState.FrontFace.StencilPassOp = ash_to_d3d12_stencil_op(ds.front.pass);
		psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = ash_to_d3d12_stencil_op(ds.front.depth_fail);
		psoDesc.DepthStencilState.FrontFace.StencilFunc = ash_to_d3d12_comparison(ds.front.compare);
		psoDesc.DepthStencilState.BackFace.StencilFailOp = ash_to_d3d12_stencil_op(ds.back.fail);
		psoDesc.DepthStencilState.BackFace.StencilPassOp = ash_to_d3d12_stencil_op(ds.back.pass);
		psoDesc.DepthStencilState.BackFace.StencilDepthFailOp = ash_to_d3d12_stencil_op(ds.back.depth_fail);
		psoDesc.DepthStencilState.BackFace.StencilFunc = ash_to_d3d12_comparison(ds.back.compare);

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> semanticStorage;
		const VertexInputCreation& explicit_vi = m_desc.pipeline.vertex_input;
		const bool use_explicit_vertex_layout = explicit_vi.num_vertex_attributes > 0 || explicit_vi.num_vertex_streams > 0;
		if (use_explicit_vertex_layout)
		{
			inputElements.reserve(explicit_vi.num_vertex_attributes);
			for (uint32_t i = 0; i < explicit_vi.num_vertex_attributes; ++i)
			{
				const VertexAttribute& attr = explicit_vi.vertex_attributes[i];
				if (attr.format == AshVertexComponentFormat::FormatCount)
				{
					HLogError(
						"DX12 render program '{}' explicit vertex attribute at location {} has invalid format.",
						m_desc.pipeline.name ? m_desc.pipeline.name : "DX12GraphicsRenderProgram",
						static_cast<unsigned>(attr.location));
					return false;
				}
				const char* sem_name = nullptr;
				UINT sem_index = 0;
				if (!resolve_explicit_vertex_semantic(attr, &sem_name, &sem_index, m_desc.pipeline.name ? m_desc.pipeline.name : "DX12GraphicsRenderProgram"))
				{
					return false;
				}
				D3D12_INPUT_ELEMENT_DESC element{};
				element.SemanticName = sem_name;
				element.SemanticIndex = sem_index;
				element.Format = ash_vertex_format_to_dxgi(attr.format);
				element.InputSlot = attr.binding;
				element.AlignedByteOffset = attr.offset;
				element.InputSlotClass = vertex_binding_input_class(explicit_vi, attr.binding);
				element.InstanceDataStepRate = element.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? 1u : 0u;
				inputElements.push_back(element);
			}
		}
		else if (!m_vertexInputs.empty())
		{
			uint32_t offset = 0;
			inputElements.reserve(m_vertexInputs.size());
			semanticStorage.reserve(m_vertexInputs.size());

			for (const DX12ShaderVertexInput& input : m_vertexInputs)
			{
				if (input.format == AshVertexComponentFormat::FormatCount)
				{
					HLogError(
						"DX12 render program '{}' could not map reflected vertex input '{}{}' to a supported format.",
						m_desc.pipeline.name ? m_desc.pipeline.name : "DX12GraphicsRenderProgram",
						input.semanticName.c_str(),
						input.semanticIndex);
					return false;
				}

				semanticStorage.push_back(input.semanticName);
				D3D12_INPUT_ELEMENT_DESC element{};
				element.SemanticName = semanticStorage.back().c_str();
				element.SemanticIndex = input.semanticIndex;
				element.Format = ash_vertex_format_to_dxgi(input.format);
				element.InputSlot = 0;
				element.AlignedByteOffset = offset;
				element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				element.InstanceDataStepRate = 0;
				inputElements.push_back(element);
				offset += get_vertex_format_size(input.format);
			}
		}
		psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
		psoDesc.InputLayout.pInputElementDescs = inputElements.empty() ? nullptr : inputElements.data();

		psoDesc.PrimitiveTopologyType = ash_to_d3d12_topology_type(m_renderState.primitive_topology);

		auto renderPass = m_desc.pipeline.render_pass;
		if (renderPass)
		{
			auto* dx12RP = static_cast<DX12RenderPass*>(renderPass.get());
			psoDesc.NumRenderTargets = dx12RP->get_color_attachment_count();
			for (uint32_t i = 0; i < psoDesc.NumRenderTargets; ++i)
			{
				psoDesc.RTVFormats[i] = ash_to_dxgi_format(dx12RP->get_color_attachment_format(i));
			}
			const AshFormat depthFmt = dx12RP->get_depth_stencil_format();
			psoDesc.DSVFormat = depthFmt != ASH_FORMAT_UNDEFINED ? ash_to_dxgi_format(depthFmt) : DXGI_FORMAT_UNKNOWN;
		}

		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;
		psoDesc.SampleMask = UINT_MAX;

		m_pipeline = std::make_unique<DX12Pipeline>();
		return m_pipeline->init_graphics(device, psoDesc, m_desc.pipeline.name);
	}

	void DX12GraphicsRenderProgram::_apply_bindings(DX12CommandBuffer* cmdBuf)
	{
		auto* cmdList = cmdBuf->get_command_list();
		apply_committed_descriptor_tables(cmdList, m_committedDescriptorTables, true);
	}

	DX12ComputeRenderProgram::~DX12ComputeRenderProgram()
	{
		destroy();
	}

	bool DX12ComputeRenderProgram::create(const ComputeProgramCreateDesc& desc)
	{
		destroy();
		m_desc = desc;
		m_binder.set_heap_manager(&DX12Context::get()->get_descriptor_heaps());

		for (uint32_t i = 0; i < desc.pipeline.shaders.stages_count; ++i)
		{
			const auto& stage = desc.pipeline.shaders.stages[i];
			if (stage.type == ASH_SHADER_STAGE_COMPUTE_BIT && stage.shader)
			{
				m_computeShader = std::dynamic_pointer_cast<DX12Shader>(stage.shader);
				break;
			}
		}

		if (!m_computeShader)
		{
			HLogError("DX12ComputeRenderProgram: No compute shader provided.");
			return false;
		}
		if (!_cache_reflection_data())
		{
			return false;
		}
		if (!_build_root_signature())
		{
			return false;
		}
		if (!_build_pso())
		{
			return false;
		}

		return true;
	}

	bool DX12ComputeRenderProgram::destroy()
	{
		m_pipeline.reset();
		m_rootSignatureLayout.reset();
		m_rootParamMappings.clear();
		m_bindingInfos.clear();
		m_boundResourceNames.clear();
		m_rootConstants = {};
		m_constDataBlock.clear();
		m_committedDescriptorTables.clear();
		m_computeShader.reset();
		m_binder.clear();
		return true;
	}

	bool DX12ComputeRenderProgram::set_const_data_block(uint32_t size, const void* data)
	{
		if (size > 0 && !data)
		{
			return false;
		}

		if (m_rootConstants.enabled && size > m_rootConstants.byteSize)
		{
			HLogError(
				"DX12 render program '{}' received a const data block of {} bytes, exceeding root constants cbuffer '{}' size {} bytes.",
				m_desc.pipeline.name ? m_desc.pipeline.name : "DX12ComputeRenderProgram",
				size,
				m_rootConstants.name.c_str(),
				m_rootConstants.byteSize);
			return false;
		}

		m_constDataBlock.resize(size);
		if (size > 0)
		{
			std::memcpy(m_constDataBlock.data(), data, size);
		}
		return true;
	}

	bool DX12ComputeRenderProgram::apply(std::shared_ptr<CommandBuffer> cb)
	{
		ASH_PROFILE_SCOPE_NC("DX12ComputeRenderProgram::Apply", AshEngine::Profile::Color::Pipeline);
		if (m_desc.pipeline.name)
		{
			ASH_PROFILE_SCOPE_TEXT(m_desc.pipeline.name, std::strlen(m_desc.pipeline.name));
		}
		if (!m_pipeline || !m_rootSignatureLayout)
		{
			return false;
		}

		auto* dx12Cb = static_cast<DX12CommandBuffer*>(cb.get());
		auto* cmdList = dx12Cb->get_command_list();
		cmdList->SetPipelineState(m_pipeline->get_pso());
		cmdList->SetComputeRootSignature(m_rootSignatureLayout->get_root_signature());
		apply_root_constants(
			m_rootConstants,
			m_constDataBlock,
			[cmdList](uint32_t rootIndex, uint32_t dwordCount, const uint32_t* data)
			{
				cmdList->SetComputeRoot32BitConstants(rootIndex, dwordCount, data, 0);
			});
		_apply_bindings(dx12Cb);
		return true;
	}

	IRenderProgramBinder& DX12ComputeRenderProgram::begin_bind()
	{
		return m_binder.begin_bind();
	}

	bool DX12ComputeRenderProgram::end_bind()
	{
		ASH_PROFILE_SCOPE_NC("DX12ComputeRenderProgram::EndBind", AshEngine::Profile::Color::Descriptor);
		m_binder.finish_binding();
		if (!_validate_binding_state())
		{
			m_binder.clear();
			m_committedDescriptorTables.clear();
			return false;
		}

		auto* device = DX12Context::get()->get_device();
		auto& heapMgr = DX12Context::get()->get_descriptor_heaps();
		m_committedDescriptorTables.clear();
		m_committedDescriptorTables.reserve(m_binder.get_pending_binds().size() + m_binder.get_pending_sampler_binds().size());
		const bool committed =
			commit_descriptor_binds(device, heapMgr, m_binder.get_pending_binds(), m_bindingInfos, m_committedDescriptorTables) &&
			commit_descriptor_binds(device, heapMgr, m_binder.get_pending_sampler_binds(), m_bindingInfos, m_committedDescriptorTables);
		m_binder.clear();
		if (!committed)
		{
			m_committedDescriptorTables.clear();
		}
		return committed;
	}

	bool DX12ComputeRenderProgram::_cache_reflection_data()
	{
		m_rootParamMappings.clear();
		m_bindingInfos.clear();
		m_boundResourceNames.clear();
		m_rootConstants = {};

		const DX12ShaderReflectionData& reflection = m_computeShader->get_reflection_data();
		for (const DX12ShaderBindingInfo& cbuffer : reflection.cbuffers)
		{
			if (is_root_constants_cbuffer_name(cbuffer.name))
			{
				if (!merge_root_constants_info(cbuffer, m_rootConstants, m_desc.pipeline.name))
				{
					return false;
				}
				continue;
			}

			if (!merge_binding_infos(std::array<DX12ShaderBindingInfo, 1>{cbuffer}, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
			{
				return false;
			}
		}
		if (!merge_binding_infos(reflection.srvs, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
		{
			return false;
		}
		if (!merge_binding_infos(reflection.uavs, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
		{
			return false;
		}
		if (!merge_binding_infos(reflection.samplers, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, m_rootParamMappings, m_bindingInfos, m_desc.pipeline.name))
		{
			return false;
		}

		if (m_rootConstants.enabled)
		{
			for (DX12RootParamMapping& mapping : m_rootParamMappings)
			{
				++mapping.rootIndex;
			}
			for (auto& [name, bindingInfo] : m_bindingInfos)
			{
				(void)name;
				++bindingInfo.rootIndex;
			}
		}

		return true;
	}

	bool DX12ComputeRenderProgram::_validate_binding_state()
	{
		m_boundResourceNames.clear();

		for (const DX12PendingBind& bind : m_binder.get_pending_binds())
		{
			if (!validate_pending_bind(m_desc.pipeline.name, bind, m_bindingInfos, m_boundResourceNames))
			{
				return false;
			}
		}

		for (const DX12PendingBind& bind : m_binder.get_pending_sampler_binds())
		{
			if (!validate_pending_bind(m_desc.pipeline.name, bind, m_bindingInfos, m_boundResourceNames))
			{
				return false;
			}
		}

		for (const auto& [name, bindingInfo] : m_bindingInfos)
		{
			if (m_boundResourceNames.find(name) == m_boundResourceNames.end())
			{
				HLogError(
					"DX12 render program '{}' requires reflected resource '{}' (rootIndex={}) but C++ did not bind it.",
					m_desc.pipeline.name ? m_desc.pipeline.name : "DX12ComputeRenderProgram",
					name.c_str(),
					bindingInfo.rootIndex);
				return false;
			}
		}

		return true;
	}

	bool DX12ComputeRenderProgram::_build_root_signature()
	{
		auto* device = DX12Context::get()->get_device();
		std::vector<D3D12_ROOT_PARAMETER> rootParams;
		std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
		rootParams.reserve(m_rootParamMappings.size() + (m_rootConstants.enabled ? 1u : 0u));
		ranges.reserve(m_rootParamMappings.size());

		if (m_rootConstants.enabled)
		{
			D3D12_ROOT_PARAMETER rootConstantsParam{};
			rootConstantsParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			rootConstantsParam.Constants.Num32BitValues = m_rootConstants.dwordCount;
			rootConstantsParam.Constants.ShaderRegister = m_rootConstants.shaderRegister;
			rootConstantsParam.Constants.RegisterSpace = m_rootConstants.registerSpace;
			rootConstantsParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(rootConstantsParam);
		}

		for (const DX12RootParamMapping& mapping : m_rootParamMappings)
		{
			const auto bindingIt = m_bindingInfos.find(mapping.name);
			if (bindingIt == m_bindingInfos.end())
			{
				continue;
			}
			const DX12ProgramBindingInfo& bindingInfo = bindingIt->second;

			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = mapping.rangeType;
			range.NumDescriptors = mapping.numDescriptors > 0 ? mapping.numDescriptors : 1u;
			range.BaseShaderRegister = bindingInfo.baseRegister;
			range.RegisterSpace = bindingInfo.registerSpace;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			ranges.push_back(range);

			D3D12_ROOT_PARAMETER param{};
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			param.DescriptorTable.NumDescriptorRanges = 1;
			param.DescriptorTable.pDescriptorRanges = &ranges.back();
			param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(param);
		}

		D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
		rootSigDesc.NumParameters = static_cast<UINT>(rootParams.size());
		rootSigDesc.pParameters = rootParams.empty() ? nullptr : rootParams.data();
		rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		m_rootSignatureLayout = std::make_unique<DX12DescriptorSetLayout>();
		const bool initialized = m_rootSignatureLayout->init(device, rootSigDesc);
		if (initialized && m_rootSignatureLayout->get_root_signature())
		{
			std::string debugName = (m_desc.pipeline.name ? m_desc.pipeline.name : "DX12ComputeRenderProgram");
			debugName += ".RootSignature";
			dx12_set_debug_name(m_rootSignatureLayout->get_root_signature(), debugName.c_str());
		}
		return initialized;
	}

	bool DX12ComputeRenderProgram::_build_pso()
	{
		auto* device = DX12Context::get()->get_device();
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = m_rootSignatureLayout ? m_rootSignatureLayout->get_root_signature() : nullptr;
		psoDesc.CS = m_computeShader->get_d3d12_bytecode();

		m_pipeline = std::make_unique<DX12Pipeline>();
		return m_pipeline->init_compute(device, psoDesc, m_desc.pipeline.name);
	}

	void DX12ComputeRenderProgram::_apply_bindings(DX12CommandBuffer* cmdBuf)
	{
		auto* cmdList = cmdBuf->get_command_list();
		apply_committed_descriptor_tables(cmdList, m_committedDescriptorTables, false);
	}
}
