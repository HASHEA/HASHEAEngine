#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
namespace RHI
{
	class Swapchain
	{
	public:
		Swapchain() = default;
		virtual ~Swapchain() {}

		virtual auto init(void* config)->HS_Result = 0;
		virtual auto shutdown()->HS_Result = 0;
		static Swapchain* create(uint32_t width, uint32_t height);
	private:

	};

}