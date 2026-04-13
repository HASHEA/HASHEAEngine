#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/Pipeline.h"
#include <memory>

namespace RHI
{
	class DX12DescriptorSetLayout;

	class DX12Pipeline : public Pipeline
	{
	public:
		DX12Pipeline() = default;
		~DX12Pipeline();

		bool init_graphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
		bool init_compute(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc);
		void shutdown();

		ID3D12PipelineState* get_pso() const { return m_pso.Get(); }
		bool is_compute() const { return m_isCompute; }

	public:
		auto get_native_handle() -> void* override { return m_pso.Get(); }
		auto get_name() -> const char* override { return m_name.c_str(); }

	private:
		ComPtr<ID3D12PipelineState> m_pso;
		bool m_isCompute = false;
		std::string m_name;
	};
}
