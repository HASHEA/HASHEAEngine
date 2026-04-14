#include "DX12Swapchain.h"
#include "DX12Context.h"
#include "DX12Texture.h"
#include "DX12DescriptorHeap.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"

// GLFW
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace RHI
{
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
		if (cfg->colorFormatCount > 0 && cfg->pColorFormat)
		{
			DXGI_FORMAT requestedFormat = ash_to_dxgi_format(cfg->pColorFormat[0]);
			// Convert SRGB to UNORM for swapchain surface
			switch (requestedFormat)
			{
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: preferredFormat = DXGI_FORMAT_B8G8R8A8_UNORM; break;
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: preferredFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
			default: preferredFormat = requestedFormat; break;
			}
		}
		m_format = dxgi_to_ash_format(preferredFormat);

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
		if (cfg->presentModeCount > 0 && cfg->pPresentMode)
		{
			for (uint32_t i = 0; i < cfg->presentModeCount; ++i)
			{
				if (cfg->pPresentMode[i] == ASH_PRESENT_MODE_IMMEDIATE_KHR)
				{
					m_syncInterval = 0;
					if (tearingSupported)
						m_presentFlags = DXGI_PRESENT_ALLOW_TEARING;
					break;
				}
				if (cfg->pPresentMode[i] == ASH_PRESENT_MODE_MAILBOX_KHR)
				{
					m_syncInterval = 0;
					break;
				}
			}
		}

		DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
		swapchainDesc.Width = m_width;
		swapchainDesc.Height = m_height;
		swapchainDesc.Format = preferredFormat;
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
			return false;
		}

		// Disable Alt+Enter fullscreen
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

		hr = swapchain1.As(&m_swapchain);
		if (FAILED(hr))
		{
			HLogError("DX12Swapchain: Failed to query IDXGISwapChain4. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		_create_back_buffers();

		HLogInfo("DX12Swapchain: Created {}x{} with {} buffers.", m_width, m_height, (int)swapchainDesc.BufferCount);
		return true;
	}

	auto DX12Swapchain::shutdown() -> bool
	{
		_release_back_buffers();
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

			m_backBuffers[i] = std::make_shared<DX12Texture>();
			m_backBuffers[i]->init_from_swapchain(
				backBuffer.Get(),
				dxgi_to_ash_format(desc.Format),
				static_cast<uint16_t>(m_width),
				static_cast<uint16_t>(m_height),
				ctx->get_device(),
				&ctx->get_descriptor_heaps());
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
			return;
		}

		m_width = width;
		m_height = height;
		_create_back_buffers();
	}

	auto DX12Swapchain::present() -> void
	{
		HRESULT hr = m_swapchain->Present(m_syncInterval, m_presentFlags);
		if (FAILED(hr))
		{
			HLogError("DX12Swapchain: Present failed. HRESULT: 0x{:08X}", (uint32_t)hr);
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
