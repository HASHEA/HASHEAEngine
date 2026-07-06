#include "DX12Swapchain.h"
#include "DX12Context.h"
#include "DX12Texture.h"
#include "DX12DescriptorHeap.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include "Base/hprofiler.h"
#include <string>

// GLFW
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace RHI
{
	namespace
	{
		constexpr auto present_mode_to_string(AshPresentMode presentMode) -> const char*
		{
			switch (presentMode)
			{
			case ASH_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
			case ASH_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
			case ASH_PRESENT_MODE_FIFO_KHR: return "FIFO";
			case ASH_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
			case ASH_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR: return "SHARED_DEMAND_REFRESH";
			case ASH_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "SHARED_CONTINUOUS_REFRESH";
			case ASH_PRESENT_MODE_UNDEFINED:
			default:
				return "UNDEFINED";
			}
		}

		constexpr auto bool_to_string(bool value) -> const char*
		{
			return value ? "true" : "false";
		}
	}

	DX12Swapchain::~DX12Swapchain()
	{
		shutdown();
	}

	auto DX12Swapchain::init(void* config) -> bool
	{
		auto* cfg = reinterpret_cast<SwapChainInitConfig*>(config);
		auto* ctx = DX12Context::get();
		auto* window = static_cast<GLFWwindow*>(cfg->window);
		HWND hwnd = glfwGetWin32Window(window);

		m_width = cfg->width;
		m_height = cfg->height;

		// Get factory from context (must be the same one used to create the device)
		IDXGIFactory6* factory = ctx->get_factory();

		// Determine preferred format
		// DX12 FLIP_DISCARD swapchains only support *_UNORM formats, not *_SRGB
		// SRGB interpretation is done through the render target view format
		DXGI_FORMAT preferredFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		AshFormat requestedRenderTargetFormat = ASH_FORMAT_R8G8B8A8_UNORM;
		if (cfg->colorFormatCount > 0 && cfg->pColorFormat)
		{
			requestedRenderTargetFormat = cfg->pColorFormat[0];
			DXGI_FORMAT requestedFormat = ash_to_dxgi_format(requestedRenderTargetFormat);
			// Convert SRGB to UNORM for swapchain surface
			switch (requestedFormat)
			{
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: preferredFormat = DXGI_FORMAT_B8G8R8A8_UNORM; break;
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: preferredFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
			default: preferredFormat = requestedFormat; break;
			}
		}
		m_surfaceFormat = preferredFormat;
		m_format = requestedRenderTargetFormat;

		// Check tearing support
		BOOL tearingSupported = FALSE;
		{
			ComPtr<IDXGIFactory5> factory5;
			if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))))
			{
				factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported));
			}
		}

		// Determine present mode
		m_syncInterval = 1;
		m_presentFlags = 0;
		AshPresentMode selectedPresentMode = ASH_PRESENT_MODE_FIFO_KHR;
		if (cfg->presentModeCount > 0 && cfg->pPresentMode)
		{
			for (uint32_t i = 0; i < cfg->presentModeCount; ++i)
			{
				if (cfg->pPresentMode[i] == ASH_PRESENT_MODE_IMMEDIATE_KHR)
				{
					m_syncInterval = 0;
					if (tearingSupported)
					{
						m_presentFlags = DXGI_PRESENT_ALLOW_TEARING;
					}
					selectedPresentMode = cfg->pPresentMode[i];
					break;
				}
				if (cfg->pPresentMode[i] == ASH_PRESENT_MODE_MAILBOX_KHR)
				{
					// Mailbox must never tear: flip-model sync=0 WITHOUT ALLOW_TEARING,
					// DXGI replaces the queued frame with the newest complete one (SDD-0003).
					m_syncInterval = 0;
					selectedPresentMode = cfg->pPresentMode[i];
					break;
				}
				if (cfg->pPresentMode[i] == ASH_PRESENT_MODE_FIFO_KHR ||
					cfg->pPresentMode[i] == ASH_PRESENT_MODE_FIFO_RELAXED_KHR)
				{
					selectedPresentMode = cfg->pPresentMode[i];
					break;
				}
			}
		}

		DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
		swapchainDesc.Width = m_width;
		swapchainDesc.Height = m_height;
		swapchainDesc.Format = m_surfaceFormat;
		swapchainDesc.Stereo = FALSE;
		swapchainDesc.SampleDesc.Count = 1;
		swapchainDesc.SampleDesc.Quality = 0;
		swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapchainDesc.BufferCount = cfg->swapchainBufferCount > 0 ? cfg->swapchainBufferCount : 2;
		swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapchainDesc.Flags = tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		// Use the same factory as the device to avoid DXGI_ERROR_INVALID_CALL
		ComPtr<IDXGISwapChain1> swapchain1;
		HRESULT hr = factory->CreateSwapChainForHwnd(
			ctx->get_graphics_queue().get_queue(),
			hwnd,
			&swapchainDesc,
			nullptr,
			nullptr,
			&swapchain1);

		if (FAILED(hr))
		{
			HLogError("DX12Swapchain: Failed to create swapchain. HRESULT: 0x{:08X}", (uint32_t)hr);
			if (ctx)
			{
				ctx->_drain_dxgi_debug_messages("create-swapchain");
				ctx->_drain_d3d12_debug_messages("create-swapchain");
			}
			return false;
		}

		// Disable Alt+Enter fullscreen
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

		hr = swapchain1.As(&m_swapchain);
		if (FAILED(hr))
		{
			HLogError("DX12Swapchain: Failed to query IDXGISwapChain4. HRESULT: 0x{:08X}", (uint32_t)hr);
			if (ctx)
			{
				ctx->_drain_dxgi_debug_messages("create-swapchain");
			}
			return false;
		}

		_create_back_buffers();
		if (ctx)
		{
			ctx->_drain_dxgi_debug_messages("create-swapchain");
			ctx->_drain_d3d12_debug_messages("create-swapchain");
		}

		HLogInfo(
			"DX12Swapchain: Created {}x{} with {} buffers. surface_format={}, rtv_format={}, present_mode={}, sync_interval={}, present_flags=0x{:X}, tearing_supported={}.",
			m_width,
			m_height,
			static_cast<int>(swapchainDesc.BufferCount),
			static_cast<int>(m_surfaceFormat),
			static_cast<int>(ash_to_dxgi_format(m_format)),
			present_mode_to_string(selectedPresentMode),
			m_syncInterval,
			m_presentFlags,
			bool_to_string(tearingSupported == TRUE));
		return true;
	}

	auto DX12Swapchain::shutdown() -> bool
	{
		auto* ctx = DX12Context::get();
		_release_back_buffers();
		if (ctx)
		{
			ctx->_drain_dxgi_debug_messages("destroy-swapchain");
			ctx->_drain_d3d12_debug_messages("destroy-swapchain");
		}
		m_swapchain.Reset();
		return true;
	}

	auto DX12Swapchain::destroy() -> void
	{
		DX12Swapchain* self = this;
		Ash_Delete(nullptr, self);
	}

	void DX12Swapchain::_create_back_buffers()
	{
		auto* ctx = DX12Context::get();
		DXGI_SWAP_CHAIN_DESC1 desc;
		m_swapchain->GetDesc1(&desc);

		m_backBuffers.resize(desc.BufferCount);
		for (UINT i = 0; i < desc.BufferCount; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			m_swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
			const std::string debugName = "SwapchainBackBuffer[" + std::to_string(i) + "]";

			m_backBuffers[i] = std::make_shared<DX12Texture>();
			m_backBuffers[i]->init_from_swapchain(
				backBuffer.Get(),
				m_format,
				static_cast<uint16_t>(m_width),
				static_cast<uint16_t>(m_height),
				ctx->get_device(),
				&ctx->get_descriptor_heaps(),
				debugName.c_str());
		}
	}

	void DX12Swapchain::_release_back_buffers()
	{
		for (auto& bb : m_backBuffers)
		{
			if (bb) bb->shutdown();
		}
		m_backBuffers.clear();
	}

	auto DX12Swapchain::resize_swapchain(uint32_t width, uint32_t height) -> void
	{
		if (width == 0 || height == 0) return;

		auto* ctx = DX12Context::get();
		ctx->wait_idle();

		_release_back_buffers();

		DXGI_SWAP_CHAIN_DESC1 desc;
		m_swapchain->GetDesc1(&desc);

		HRESULT hr = m_swapchain->ResizeBuffers(
			desc.BufferCount, width, height, desc.Format, desc.Flags);

		if (FAILED(hr))
		{
			HLogError("DX12Swapchain: ResizeBuffers failed. HRESULT: 0x{:08X}", (uint32_t)hr);
			if (ctx)
			{
				ctx->_drain_dxgi_debug_messages("resize-swapchain");
				ctx->_drain_d3d12_debug_messages("resize-swapchain");
			}
			return;
		}

		m_width = width;
		m_height = height;
		_create_back_buffers();
		if (ctx)
		{
			ctx->_drain_dxgi_debug_messages("resize-swapchain");
			ctx->_drain_d3d12_debug_messages("resize-swapchain");
		}
	}

	auto DX12Swapchain::present() -> void
	{
		ASH_PROFILE_SCOPE_NC("DX12Swapchain::Present", AshEngine::Profile::Color::Present);
		HRESULT hr = m_swapchain->Present(m_syncInterval, m_presentFlags);
		if (FAILED(hr))
		{
			HLogError("DX12Swapchain: Present failed. HRESULT: 0x{:08X}", (uint32_t)hr);
		}
		if (auto* ctx = DX12Context::get())
		{
			ctx->_drain_dxgi_debug_messages("present");
			ctx->_drain_d3d12_debug_messages("present");
		}
	}

	auto DX12Swapchain::get_swapchain_buffer() -> std::shared_ptr<Texture>
	{
		return m_backBuffers[m_currentBackBufferIndex];
	}

	auto DX12Swapchain::get_swapchain_buffer(uint32_t index) -> std::shared_ptr<Texture>
	{
		H_ASSERT(index < m_backBuffers.size());
		return m_backBuffers[index];
	}

	auto DX12Swapchain::begin_frame() -> void
	{
		m_currentBackBufferIndex = m_swapchain->GetCurrentBackBufferIndex();
	}

	auto DX12Swapchain::end_frame() -> void
	{
		// Synchronization handled in DX12Context
	}
}
