#pragma once
#include "RHICommon.h"
namespace RHI
{

	class CommandBuffer
	{
	public:
		CommandBuffer() = default;
		virtual ~CommandBuffer() {}
	public:
		//rhi interfaces
		virtual auto begin() -> void = 0;
		virtual auto end() ->  void = 0;
		virtual auto get_state()->AshCommandBufferState = 0;
	private:

	};
};