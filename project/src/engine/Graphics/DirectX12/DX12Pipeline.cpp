#include "DX12Pipeline.h"
#include "DX12DescriptorSetLayout.h"
#include "DX12Context.h"
#include "Base/hlog.h"

namespace RHI
{
	DX12Pipeline::~DX12Pipeline()
	{
		shutdown();
	}

	bool DX12Pipeline::init_graphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
	{
		m_isCompute = false;
		HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pso));
		if (FAILED(hr))
		{
			HLogError("DX12Pipeline: Failed to create graphics PSO. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	bool DX12Pipeline::init_compute(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
	{
		m_isCompute = true;
		HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_pso));
		if (FAILED(hr))
		{
			HLogError("DX12Pipeline: Failed to create compute PSO. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	void DX12Pipeline::shutdown()
	{
		if (!m_pso)
		{
			return;
		}

		ComPtr<ID3D12PipelineState> deferredPso = m_pso;
		m_pso.Reset();

		DX12Context* context = DX12Context::get();
		if (immediate_deletion || !context)
		{
			deferredPso.Reset();
			return;
		}

		context->get_current_frame_deletion_queue().emplace([deferredPso]() mutable {
			deferredPso.Reset();
		});
	}
}
