#include "DX12Context.h"
#include "DX12CommandBuffer.h"
#include "DX12Buffer.h"
#include "DX12BufferView.h"
#include "DX12Texture.h"
#include "DX12TextureView.h"
#include "DX12Sampler.h"
#include "DX12Shader.h"
#include "DX12RenderPass.h"
#include "DX12Framebuffer.h"
#include "DX12RenderProgram.h"
#include "DX12StagingBuffer.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include "D3D12MemAlloc.h"

namespace RHI
{
	DX12Context* DX12Context::s_instance = nullptr;

	auto DX12Context::init(void* config) -> bool
	{
		const auto& cfg = *reinterpret_cast<const GraphicsContextInitConfig*>(config);
		m_numThread = cfg.num_threads;
		m_enableDebugLayer = cfg.dx12Validation.enableDebugLayer;
		m_enableGpuValidation = cfg.dx12Validation.enableGpuValidation;

		_enable_debug_layer();

		if (!_create_factory()) return false;
		if (!_select_adapter()) return false;
		if (!_create_device()) return false;
		if (!_create_command_queues()) return false;
		if (!_create_memory_allocator()) return false;
		if (!_create_descriptor_heaps()) return false;
		if (!_create_frame_resources(cfg.num_threads)) return false;

		// Create staging buffer
		m_stagingBuffer = Ash_New<DX12StagingBuffer>();
		m_stagingBuffer->init(m_device.Get(), m_d3d12maAllocator, 64 * 1024 * 1024); // 64MB staging

		HLogInfo("DX12Context: Initialization complete.");
		return true;
	}

	auto DX12Context::shutdown() -> bool
	{
		wait_idle();

		// Process all deletion queues
		for (auto& q : m_delayedDeletionQueues)
			q.flush();

		if (m_stagingBuffer)
		{
			m_stagingBuffer->shutdown();
			Ash_Delete(nullptr, m_stagingBuffer);
			m_stagingBuffer = nullptr;
		}

		for (auto* cb : m_commandBuffers)
		{
			Ash_Delete(nullptr, cb);
		}
		m_commandBuffers.clear();

		_shutdown_frame_resources();
		m_descriptorHeaps.shutdown();

		if (m_d3d12maAllocator)
		{
			m_d3d12maAllocator->Release();
			m_d3d12maAllocator = nullptr;
		}

		m_graphicsQueue.shutdown();
		m_computeQueue.shutdown();
		m_copyQueue.shutdown();

		m_shaderPool.clear();
		m_samplerCache.clear();

		m_device.Reset();
		m_adapter.Reset();
		m_factory.Reset();

		s_instance = nullptr;
		HLogInfo("DX12Context: Shutdown complete.");
		return true;
	}

	auto DX12Context::destroy() -> void
	{
		DX12Context* self = this;
		Ash_Delete(nullptr, self);
	}

	auto DX12Context::_enable_debug_layer() -> bool
	{
		if (!m_enableDebugLayer)
		{
			return true;
		}

		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			HLogInfo("DX12Context: Debug layer enabled.");

			if (m_enableGpuValidation)
			{
				ComPtr<ID3D12Debug1> debugController1;
				if (SUCCEEDED(debugController.As(&debugController1)))
				{
					debugController1->SetEnableGPUBasedValidation(TRUE);
					HLogInfo("DX12Context: GPU-based validation enabled.");
				}
			}
		}
		return true;
	}

	auto DX12Context::_create_factory() -> bool
	{
		UINT dxgiFactoryFlags = 0;
		if (m_enableDebugLayer)
		{
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
		HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));
		if (FAILED(hr))
		{
			HLogError("DX12Context: Failed to create DXGI factory. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	auto DX12Context::_select_adapter() -> bool
	{
		ComPtr<IDXGIAdapter1> adapter1;
		// Try to get high performance adapter first
		for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1)) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter1->GetDesc1(&desc);

			// Skip software adapters
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			// Check for D3D12 support
			if (SUCCEEDED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr)))
			{
				adapter1.As(&m_adapter);
				DXGI_ADAPTER_DESC1 adapterDesc;
				m_adapter->GetDesc1(&adapterDesc);
				HLogInfo("DX12Context: Selected adapter: {}", std::string(adapterDesc.Description, adapterDesc.Description + wcslen(adapterDesc.Description)));
				return true;
			}
		}

		HLogError("DX12Context: No suitable D3D12 adapter found.");
		return false;
	}

	auto DX12Context::_create_device() -> bool
	{
		HRESULT hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device));
		if (FAILED(hr))
		{
			HLogError("DX12Context: Failed to create D3D12 device. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		// Check ray tracing support
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
		if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		{
			if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
			{
				HLogInfo("DX12Context: Ray tracing supported (Tier {}).", (int)options5.RaytracingTier);
			}
		}

		// Check shader model support
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
		if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
		{
			m_highestShaderModel = shaderModel.HighestShaderModel;
			HLogInfo("DX12Context: Shader Model 6.{} supported.", (int)(shaderModel.HighestShaderModel & 0xF));
		}

#ifdef ASH_DEBUG
		ComPtr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(m_device.As(&infoQueue)))
		{
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		}
#endif

		return true;
	}

	auto DX12Context::_create_command_queues() -> bool
	{
		if (!m_graphicsQueue.init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT)) return false;
		if (!m_computeQueue.init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE)) return false;
		if (!m_copyQueue.init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY)) return false;
		return true;
	}

	auto DX12Context::_create_memory_allocator() -> bool
	{
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.pDevice = m_device.Get();
		allocatorDesc.pAdapter = m_adapter.Get();
		allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;

		HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_d3d12maAllocator);
		if (FAILED(hr))
		{
			HLogError("DX12Context: Failed to create D3D12MA allocator. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	auto DX12Context::_create_descriptor_heaps() -> bool
	{
		return m_descriptorHeaps.init(m_device.Get());
	}

	auto DX12Context::_create_frame_resources(uint16_t numThread) -> bool
	{
		m_frameResources.resize(k_dx12_max_frames);
		for (uint32_t i = 0; i < k_dx12_max_frames; ++i)
		{
			auto& fr = m_frameResources[i];
			fr.cmdAllocator = Ash_New<DX12CommandPool>();
			fr.cmdAllocator->init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);

			fr.fence = Ash_New<DX12Fence>();
			fr.fence->init(m_device.Get(), 0);
			fr.fenceValue = 0;
		}

		// Create command buffers (one per thread)
		uint32_t numCmdBuffers = static_cast<uint32_t>(numThread) * k_dx12_max_frames;
		m_commandBuffers.resize(numCmdBuffers);
		for (uint32_t i = 0; i < numCmdBuffers; ++i)
		{
			m_commandBuffers[i] = Ash_New<DX12CommandBuffer>();
			m_commandBuffers[i]->init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		}

		m_currentFrame = 0;
		m_previousFrame = 0;
		m_absoluteFrame = 0;

		return true;
	}

	auto DX12Context::_shutdown_frame_resources() -> bool
	{
		for (auto& fr : m_frameResources)
		{
			if (fr.cmdAllocator)
			{
				fr.cmdAllocator->shutdown();
				Ash_Delete(nullptr, fr.cmdAllocator);
				fr.cmdAllocator = nullptr;
			}
			if (fr.fence)
			{
				fr.fence->shutdown();
				Ash_Delete(nullptr, fr.fence);
				fr.fence = nullptr;
			}
		}
		m_frameResources.clear();
		return true;
	}

	// ──────────────────────────────────────────────────────────────
	// Frame Management
	// ──────────────────────────────────────────────────────────────
	auto DX12Context::begin_frame() -> void
	{
		m_currentFrame = static_cast<uint32_t>(m_absoluteFrame % k_dx12_max_frames);

		auto& fr = m_frameResources[m_currentFrame];

		// Wait for this frame's previous work to complete
		fr.fence->wait();

		// Reset command allocator for this frame
		fr.cmdAllocator->reset();

		// Reset shader-visible descriptor heaps
		m_descriptorHeaps.begin_frame();

		// Process delayed deletions
		m_delayedDeletionQueues[m_currentFrame].flush();
	}

	auto DX12Context::end_frame() -> void
	{
		m_previousFrame = m_currentFrame;
		m_absoluteFrame++;
	}

	auto DX12Context::wait_idle() -> void
	{
		// Signal and wait on all queues
		for (auto& fr : m_frameResources)
		{
			fr.fence->signal(m_graphicsQueue.get_queue());
			fr.fence->wait();
		}
	}

	auto DX12Context::get_command_buffer(uint32_t threadIndx) -> CommandBuffer*
	{
		uint32_t index = m_currentFrame * m_numThread + threadIndx;
		H_ASSERT(index < m_commandBuffers.size());
		auto* cb = m_commandBuffers[index];
		cb->set_allocator(m_frameResources[m_currentFrame].cmdAllocator->get_allocator());
		return cb;
	}

	auto DX12Context::get_secondary_command_buffer(uint32_t threadIndx) -> CommandBuffer*
	{
		// DX12 uses bundles for secondary command buffers
		// For now, return a regular command buffer
		return get_command_buffer(threadIndx);
	}

	auto DX12Context::submit(const SubmitInfo& info) -> void
	{
		std::vector<ID3D12CommandList*> cmdLists;
		for (uint32_t i = 0; i < info.cmdCount; ++i)
		{
			auto* dx12Cb = static_cast<DX12CommandBuffer*>(&info.cmds[i]);
			cmdLists.push_back(dx12Cb->get_command_list());
		}

		if (!cmdLists.empty())
		{
			m_graphicsQueue.execute_command_lists(static_cast<UINT>(cmdLists.size()), cmdLists.data());
		}

		// Signal fence for this frame
		auto& fr = m_frameResources[m_currentFrame];
		fr.fence->signal(m_graphicsQueue.get_queue());
	}

	auto DX12Context::submit_immediately(const SubmitInfo& info) -> void
	{
		submit(info);
		// Immediately wait
		auto& fr = m_frameResources[m_currentFrame];
		fr.fence->wait();
	}

	// ──────────────────────────────────────────────────────────────
	// Resource Creation (forward to DX12 implementations)
	// ──────────────────────────────────────────────────────────────
	auto DX12Context::create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader>
	{
		uint64_t hash = get_shader_hash(ci);
		auto it = m_shaderPool.find(hash);
		if (it != m_shaderPool.end())
			return it->second;

		auto shader = std::make_shared<DX12Shader>();
		if (!shader->init(ci))
			return nullptr;

		m_shaderPool.emplace(hash, std::static_pointer_cast<Shader>(shader));
		return shader;
	}

	auto DX12Context::create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer>
	{
		auto buffer = std::make_shared<DX12Buffer>();
		if (!buffer->init(ci, m_device.Get(), m_d3d12maAllocator, &m_descriptorHeaps))
			return nullptr;
		return buffer;
	}

	auto DX12Context::create_buffer_view(const BufferViewCreation& ci, std::shared_ptr<Buffer> parentBuffer) -> std::shared_ptr<BufferView>
	{
		auto view = std::make_shared<DX12BufferView>();
		if (!view->init(ci, std::static_pointer_cast<DX12Buffer>(parentBuffer), m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return view;
	}

	auto DX12Context::create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture>
	{
		auto texture = std::make_shared<DX12Texture>();
		if (!texture->init(ci, m_device.Get(), m_d3d12maAllocator, &m_descriptorHeaps))
			return nullptr;
		return texture;
	}

	auto DX12Context::create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView>
	{
		auto view = std::make_shared<DX12TextureView>();
		if (!view->init(ci, std::static_pointer_cast<DX12Texture>(parentTexture), m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return view;
	}

	auto DX12Context::create_render_pass(const RenderPassCreation& ci) -> std::shared_ptr<RenderPass>
	{
		auto rp = std::make_shared<DX12RenderPass>();
		if (!rp->init(ci))
			return nullptr;
		return rp;
	}

	auto DX12Context::create_framebuffer(const FramebufferCreation& ci) -> std::shared_ptr<Framebuffer>
	{
		auto fb = std::make_shared<DX12Framebuffer>();
		if (!fb->init(ci, m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return fb;
	}

	auto DX12Context::create_graphics_render_program(const GraphicProgramCreateDesc& desc) -> std::unique_ptr<IGraphicsRenderProgram>
	{
		auto program = std::make_unique<DX12GraphicsRenderProgram>();
		if (!program->create(desc))
			return nullptr;
		return program;
	}

	auto DX12Context::create_compute_render_program(const ComputeProgramCreateDesc& desc) -> std::unique_ptr<IComputeRenderProgram>
	{
		auto program = std::make_unique<DX12ComputeRenderProgram>();
		if (!program->create(desc))
			return nullptr;
		return program;
	}

	auto DX12Context::get_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler>
	{
		if (ss < m_samplerCache.size() && m_samplerCache[ss] != nullptr)
			return m_samplerCache[ss];

		auto sampler = std::make_shared<DX12Sampler>();
		SamplerCreation sc{};
		// Default sampler for ASH_SAMPLER_STATE_DEFAULT
		sc.minFilter = ASH_FILTER_LINEAR;
		sc.magFilter = ASH_FILTER_LINEAR;
		sc.mipFilter = ASH_FILTER_LINEAR;
		sc.address_mode_u = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		sc.address_mode_v = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		sc.address_mode_w = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		sc.enable_anisotropy = true;
		sc.max_anisotropy = 16.0f;
		sc.max_lod = 16.0f;

		if (!sampler->init(sc, m_device.Get(), &m_descriptorHeaps))
			return nullptr;

		if (ss >= m_samplerCache.size())
			m_samplerCache.set_size(ss + 1);
		m_samplerCache[ss] = sampler;
		return sampler;
	}
}
