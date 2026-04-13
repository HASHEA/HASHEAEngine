#pragma once
#include "DX12Wrapper.h"
#include <cstdint>

namespace RHI
{
	class DX12Fence
	{
	public:
		DX12Fence() = default;
		~DX12Fence();

		bool init(ID3D12Device* device, uint64_t initialValue = 0);
		void shutdown();

		void signal(ID3D12CommandQueue* queue);
		void wait(uint64_t timeout = INFINITE);
		void cpu_signal(uint64_t value);
		uint64_t get_completed_value() const;
		uint64_t get_current_value() const { return m_fenceValue; }
		ID3D12Fence* get_fence() const { return m_fence.Get(); }

	private:
		ComPtr<ID3D12Fence> m_fence;
		HANDLE m_fenceEvent = nullptr;
		uint64_t m_fenceValue = 0;
	};
}
