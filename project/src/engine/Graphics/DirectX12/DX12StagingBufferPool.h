#pragma once
#include "DX12Wrapper.h"
#include <array>
#include <list>
#include <mutex>
#include <utility>

namespace D3D12MA { class Allocator; class Allocation; }

namespace RHI
{
	enum class DX12StagingHeapKind : uint8_t
	{
		Upload = 0,
		Readback = 1,
	};

	class DX12StagingBufferPool
	{
	public:
		DX12StagingBufferPool() = default;
		~DX12StagingBufferPool();

		bool init(ID3D12Device* device, D3D12MA::Allocator* allocator);
		void shutdown();

		// Returns (resource, byte offset within resource) for the staged copy.
		// Asserts on bad args; treats hard allocation failure as fatal (no silent failure path).
		std::pair<ID3D12Resource*, uint64_t> stage_data(
			const void* data,
			uint32_t size,
			uint32_t alignment = 256u,
			DX12StagingHeapKind kind = DX12StagingHeapKind::Upload);

		// Reclaim pages whose GPU work has completed; update high-water; evict free pages above water.
		// Must be called from DX12Context::begin_frame AFTER fr.fence->wait() AND
		// AFTER m_delayedDeletionQueues[currentFrame].flush().
		void begin_frame(uint64_t absoluteFrame);

	private:
		struct Page
		{
			ComPtr<ID3D12Resource> resource;
			D3D12MA::Allocation* allocation = nullptr;
			uint8_t* mapped = nullptr;
			uint32_t size = 0;
			uint32_t offset = 0;
			uint64_t last_use_frame = 0;
			uint64_t freed_frame = 0;
			DX12StagingHeapKind kind = DX12StagingHeapKind::Upload;
			bool dedicated = false;
		};

		struct PerKind
		{
			Page* active = nullptr;
			std::list<Page*> in_use;
			std::list<Page*> free;
			std::list<Page*> dedicated_pending;
		};

	private:
		Page* _create_page(uint32_t size, DX12StagingHeapKind kind, bool dedicated);
		void _destroy_page(Page* page);
		Page* _activate_or_grow(PerKind& pk, DX12StagingHeapKind kind, uint32_t neededBytes);
		void _reclaim_kind(PerKind& pk, uint64_t absoluteFrame);
		void _evict_kind(PerKind& pk, uint64_t absoluteFrame, uint32_t high_water);

		PerKind& _kind(DX12StagingHeapKind kind)
		{
			return kind == DX12StagingHeapKind::Upload ? m_upload : m_readback;
		}

	private:
		ID3D12Device* m_device = nullptr;
		D3D12MA::Allocator* m_allocator = nullptr;
		uint64_t m_currentAbsoluteFrame = 0;

		PerKind m_upload;
		PerKind m_readback;

		// Total in-use pages (across both kinds, including the active one) across recent frames.
		static constexpr uint32_t k_high_water_window = 16u;
		std::array<uint32_t, k_high_water_window> m_inUseRing{};
		uint32_t m_ringIndex = 0;

		std::mutex m_mutex;
	};
}
