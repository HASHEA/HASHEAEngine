#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "DX12Pipeline.h"
#include "DX12DescriptorSetLayout.h"
#include "DX12RenderProgramBinder.h"
#include "DX12Shader.h"
#include "Graphics/RenderProgram.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace RHI
{
	class DX12DescriptorHeapManager;
	class DX12CommandBuffer;

	// Root parameter mapping info
	struct DX12RootParamMapping
	{
		std::string name;
		uint32_t rootIndex = UINT32_MAX;
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		uint32_t numDescriptors = 1;
	};

	struct DX12ProgramBindingInfo
	{
		uint32_t rootIndex = UINT32_MAX;
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		uint32_t descriptorCount = 1;
		uint32_t baseRegister = 0;
		uint32_t registerSpace = 0;
	};

	struct DX12RootConstantsInfo
	{
		bool enabled = false;
		uint32_t rootIndex = UINT32_MAX;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
		uint32_t byteSize = 0;
		uint32_t dwordCount = 0;
		std::string name;
	};

	class DX12GraphicsRenderProgram : public IGraphicsRenderProgram
	{
	public:
		DX12GraphicsRenderProgram() = default;
		~DX12GraphicsRenderProgram();

		bool create(const GraphicProgramCreateDesc& desc) override;
		bool destroy() override;
		bool apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall) override;
		bool set_const_data_block(uint32_t size, const void* data) override;
		bool apply(std::shared_ptr<CommandBuffer> cb) override;
		IRenderProgramBinder& begin_bind() override;
		bool end_bind() override;

	private:
		bool _build_root_signature();
		bool _build_pso();
		bool _cache_reflection_data();
		bool _validate_binding_state();
		void _apply_bindings(DX12CommandBuffer* cmdList);

	private:
		GraphicProgramCreateDesc m_desc;
		RenderState m_renderState;
		DX12RenderProgramBinder m_binder;
		std::unique_ptr<DX12Pipeline> m_pipeline;
		std::unique_ptr<DX12DescriptorSetLayout> m_rootSignatureLayout;
		std::vector<DX12RootParamMapping> m_rootParamMappings;
		std::unordered_map<std::string, DX12ProgramBindingInfo> m_bindingInfos;
		std::unordered_set<std::string> m_boundResourceNames;
		std::vector<DX12ShaderVertexInput> m_vertexInputs;
		DX12RootConstantsInfo m_rootConstants;
		std::vector<uint8_t> m_constDataBlock;
		std::vector<std::shared_ptr<DX12Shader>> m_shaders;
		bool m_needsRebuild = true;
	};

	class DX12ComputeRenderProgram : public IComputeRenderProgram
	{
	public:
		DX12ComputeRenderProgram() = default;
		~DX12ComputeRenderProgram();

		bool create(const ComputeProgramCreateDesc& desc) override;
		bool destroy() override;
		bool set_const_data_block(uint32_t size, const void* data) override;
		bool apply(std::shared_ptr<CommandBuffer> cb) override;
		IRenderProgramBinder& begin_bind() override;
		bool end_bind() override;

	private:
		bool _build_root_signature();
		bool _build_pso();
		bool _cache_reflection_data();
		bool _validate_binding_state();
		void _apply_bindings(DX12CommandBuffer* cmdList);

	private:
		ComputeProgramCreateDesc m_desc;
		DX12RenderProgramBinder m_binder;
		std::unique_ptr<DX12Pipeline> m_pipeline;
		std::unique_ptr<DX12DescriptorSetLayout> m_rootSignatureLayout;
		std::vector<DX12RootParamMapping> m_rootParamMappings;
		std::unordered_map<std::string, DX12ProgramBindingInfo> m_bindingInfos;
		std::unordered_set<std::string> m_boundResourceNames;
		DX12RootConstantsInfo m_rootConstants;
		std::vector<uint8_t> m_constDataBlock;
		std::shared_ptr<DX12Shader> m_computeShader;
	};
}
