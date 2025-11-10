#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
namespace RHI
{
	enum AshFormat;
	enum AshColorSpace;
	enum AshPresentMode;
	class Texture;
	struct SwapChainInitConfig
	{
		void* window = nullptr;
		uint8_t swapchainBufferCount = 1;
		uint16_t  width = 1;
		uint16_t  height = 1;
		uint32_t colorFormatCount = 0;
		const AshFormat* pColorFormat = nullptr;
		uint32_t colorSpaceCount = 0;
		const AshColorSpace* pColorSpace;
		uint32_t presentModeCount = 0;
		const AshPresentMode* pPresentMode = nullptr;
	};
	class Swapchain
	{
	public:
		Swapchain() = default;
		virtual ~Swapchain() {}

		virtual auto init(void* config)->bool = 0;
		virtual auto shutdown()->bool = 0;
		static Swapchain* create();
	public:
		/****** rhi interfaces ********/
		virtual auto resize_swapchain(uint32_t width, uint32_t height) -> void = 0;
		virtual auto present() -> void = 0;
		virtual auto get_swapchain_buffer() -> std::shared_ptr<Texture> = 0;
		virtual auto get_swapchain_buffer(uint32_t index) -> std::shared_ptr<Texture> = 0;
		virtual auto get_width() -> uint32_t = 0;
		virtual auto get_height() -> uint32_t = 0;
		virtual auto begin_frame() -> void = 0;
		virtual auto end_frame() -> void = 0;
		virtual auto get_swapchain_buffer_count() -> uint8_t = 0;
	private:

	};

}