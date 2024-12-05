#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
namespace RHI
{
	enum AshFormat;
	enum AshColorSpace;
	enum AshPresentMode;
	struct SwapChainInitConfig
	{
		void* window = nullptr;
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

		virtual auto init(void* config)->HS_Result = 0;
		virtual auto shutdown()->HS_Result = 0;
		static Swapchain* create();
	private:

	};

}