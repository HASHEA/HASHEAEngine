#pragma once
#include "DX12Wrapper.h"
#include "Graphics/SwapChain.h"
#include <vector>
#include <memory>

namespace RHI
{
	class DX12Texture;
	class DX12DescriptorHeapManager;

	class DX12Swapchain : public Swapchain
	{
	public:
		DX12Swapchain() = default;
		~DX12Swapchain();

		auto init(void* config) -> bool override;
		auto shutdown() -> bool override;
		auto destroy() -> void override;
		auto resize_swapchain(uint32_t width, uint32_t height) -> void override;
		auto present() -> void override;
		auto get_swapchain_buffer() -> std::shared_ptr<Texture> override;
		auto get_swapchain_buffer(uint32_t index) -> std::shared_ptr<Texture> override;
		auto get_format() -> AshFormat override { return m_format; }
		auto get_width() -> uint32_t override { return m_width; }
		auto get_height() -> uint32_t override { return m_height; }
		auto begin_frame() -> void override;
		auto end_frame() -> void override;
		auto get_swapchain_buffer_count() -> uint8_t override { return static_cast<uint8_t>(m_backBuffers.size()); }

		uint32_t get_current_back_buffer_index() const { return m_currentBackBufferIndex; }

	private:
		void _create_back_buffers();
		void _release_back_buffers();

	private:
		ComPtr<IDXGISwapChain4> m_swapchain;
		std::vector<std::shared_ptr<DX12Texture>> m_backBuffers;
		uint32_t m_currentBackBufferIndex = 0;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		AshFormat m_format{};
		DXGI_FORMAT m_surfaceFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t m_syncInterval = 1; // VSync by default
		UINT m_presentFlags = 0;
	};
}
