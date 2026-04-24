#include "DX12StagingBufferPool.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include "DX12Context.h"
#include "D3D12MemAlloc.h"

namespace RHI
{
	namespace
	{
		constexpr uint32_t k_staging_page_size           = 4u * 1024u * 1024u;
		constexpr uint32_t k_staging_dedicated_threshold = 2u * 1024u * 1024u;
		constexpr uint32_t k_staging_page_idle_frames    = 16u;
		constexpr uint32_t k_staging_evict_tolerance     = 1u;

		inline uint32_t align_up_u32(uint32_t v, uint32_t a)
		{
			H_ASSERT(a != 0u && (a & (a - 1u)) == 0u);
			return (v + a - 1u) & ~(a - 1u);
		}
	}

	DX12StagingBufferPool::~DX12StagingBufferPool()
	{
		shutdown();
	}

	bool DX12StagingBufferPool::init(ID3D12Device* device, D3D12MA::Allocator* allocator)
	{
		H_ASSERT(device);
		H_ASSERT(allocator);
		m_device = device;
		m_allocator = allocator;
		m_currentAbsoluteFrame = 0;
		m_ringIndex = 0;
		m_inUseRing.fill(0u);
		return true;
	}

	void DX12StagingBufferPool::shutdown()
	{
		auto destroy_list = [this](std::list<Page*>& list)
		{
			for (auto* p : list) _destroy_page(p);
			list.clear();
		};
		auto destroy_kind = [&](PerKind& pk)
		{
			if (pk.active) { _destroy_page(pk.active); pk.active = nullptr; }
			destroy_list(pk.in_use);
			destroy_list(pk.free);
			destroy_list(pk.dedicated_pending);
		};
		destroy_kind(m_upload);
		destroy_kind(m_readback);
		m_device = nullptr;
		m_allocator = nullptr;
	}

	DX12StagingBufferPool::Page* DX12StagingBufferPool::_create_page(uint32_t size, DX12StagingHeapKind kind, bool dedicated)
	{
		H_ASSERT(m_allocator);
		H_ASSERT(size > 0u);

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = size;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12MA::ALLOCATION_DESC allocDesc = {};
		const D3D12_RESOURCE_STATES initialState = (kind == DX12StagingHeapKind::Upload)
			? D3D12_RESOURCE_STATE_GENERIC_READ
			: D3D12_RESOURCE_STATE_COPY_DEST;
		allocDesc.HeapType = (kind == DX12StagingHeapKind::Upload)
			? D3D12_HEAP_TYPE_UPLOAD
			: D3D12_HEAP_TYPE_READBACK;

		ComPtr<ID3D12Resource> resource;
		D3D12MA::Allocation* allocation = nullptr;
		HRESULT hr = m_allocator->CreateResource(
			&allocDesc,
			&resourceDesc,
			initialState,
			nullptr,
			&allocation,
			IID_PPV_ARGS(&resource));
		if (FAILED(hr))
		{
			HLogError("DX12StagingBufferPool: Failed to create page (size={}, kind={}, dedicated={}). HRESULT: 0x{:08X}",
				size, (int)kind, dedicated ? 1 : 0, (uint32_t)hr);
			if (allocation) allocation->Release();
			return nullptr;
		}

		D3D12_RANGE readRange = { 0, 0 };
		void* mapped = nullptr;
		hr = resource->Map(0, &readRange, &mapped);
		if (FAILED(hr))
		{
			HLogError("DX12StagingBufferPool: Map failed. HRESULT: 0x{:08X}", (uint32_t)hr);
			allocation->Release();
			return nullptr;
		}

		Page* page = Ash_New<Page>();
		page->resource = std::move(resource);
		page->allocation = allocation;
		page->mapped = static_cast<uint8_t*>(mapped);
		page->size = size;
		page->offset = 0;
		page->last_use_frame = 0;
		page->freed_frame = 0;
		page->kind = kind;
		page->dedicated = dedicated;

		HLogInfo("DX12StagingBufferPool: created page (size={} bytes, kind={}, dedicated={})",
			size, (int)kind, dedicated ? 1 : 0);
		return page;
	}

	void DX12StagingBufferPool::_destroy_page(Page* page)
	{
		if (!page) return;
		if (page->mapped)
		{
			page->resource->Unmap(0, nullptr);
			page->mapped = nullptr;
		}
		if (page->allocation)
		{
			page->allocation->Release();
			page->allocation = nullptr;
		}
		page->resource.Reset();
		HLogInfo("DX12StagingBufferPool: destroyed page (size={}, kind={}, dedicated={})",
			page->size, (int)page->kind, page->dedicated ? 1 : 0);
		Ash_Delete(nullptr, page);
	}

	DX12StagingBufferPool::Page* DX12StagingBufferPool::_activate_or_grow(PerKind& pk, DX12StagingHeapKind kind, uint32_t neededBytes)
	{
		// Look for a free page big enough; LRU order has oldest at front.
		for (auto it = pk.free.begin(); it != pk.free.end(); ++it)
		{
			if ((*it)->size >= neededBytes)
			{
				Page* page = *it;
				pk.free.erase(it);
				page->offset = 0;
				return page;
			}
		}
		const uint32_t pageSize = neededBytes > k_staging_page_size ? neededBytes : k_staging_page_size;
		return _create_page(pageSize, kind, false);
	}

	std::pair<ID3D12Resource*, uint64_t> DX12StagingBufferPool::stage_data(
		const void* data, uint32_t size, uint32_t alignment, DX12StagingHeapKind kind)
	{
		H_ASSERT(data != nullptr);
		H_ASSERT(size > 0u);
		if (alignment == 0u) alignment = 1u;

		std::lock_guard<std::mutex> lock(m_mutex);
		PerKind& pk = _kind(kind);

		// Dedicated path for large requests.
		if (size > k_staging_dedicated_threshold)
		{
			Page* page = _create_page(size, kind, true);
			H_ASSERT(page);
			memcpy(page->mapped, data, size);
			page->offset = size;
			page->last_use_frame = m_currentAbsoluteFrame;
			pk.dedicated_pending.push_back(page);
			return { page->resource.Get(), 0ull };
		}

		// Pooled path.
		Page* active = pk.active;
		uint32_t alignedOffset = 0;
		if (active)
		{
			alignedOffset = align_up_u32(active->offset, alignment);
			if (alignedOffset + size > active->size)
			{
				// Active doesn't fit; rotate it.
				pk.in_use.push_back(active);
				active = nullptr;
				pk.active = nullptr;
			}
		}
		if (!active)
		{
			active = _activate_or_grow(pk, kind, size);
			H_ASSERT(active);
			pk.active = active;
			alignedOffset = align_up_u32(active->offset, alignment);
			H_ASSERT(alignedOffset + size <= active->size);
		}

		memcpy(active->mapped + alignedOffset, data, size);
		active->offset = alignedOffset + size;
		active->last_use_frame = m_currentAbsoluteFrame;

		return { active->resource.Get(), static_cast<uint64_t>(alignedOffset) };
	}

	void DX12StagingBufferPool::_reclaim_kind(PerKind& pk, uint64_t absoluteFrame)
	{
		// Reclaim in_use pages whose GPU work has completed.
		// Condition: page was last touched at frame F, and we are now at frame F + k_dx12_max_frames or later
		// (meaning the fence for slot (F % k_max_frames) has been waited on in begin_frame).
		for (auto it = pk.in_use.begin(); it != pk.in_use.end(); )
		{
			Page* p = *it;
			if (p->last_use_frame + k_dx12_max_frames <= absoluteFrame)
			{
				p->offset = 0;
				p->freed_frame = absoluteFrame;
				it = pk.in_use.erase(it);
				pk.free.push_back(p);
			}
			else
			{
				++it;
			}
		}
		// Destroy dedicated pages whose GPU work has completed.
		for (auto it = pk.dedicated_pending.begin(); it != pk.dedicated_pending.end(); )
		{
			Page* p = *it;
			if (p->last_use_frame + k_dx12_max_frames <= absoluteFrame)
			{
				_destroy_page(p);
				it = pk.dedicated_pending.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void DX12StagingBufferPool::_evict_kind(PerKind& pk, uint64_t absoluteFrame, uint32_t high_water)
	{
		// Pop oldest free pages while both conditions hold.
		while (pk.free.size() > static_cast<size_t>(high_water + k_staging_evict_tolerance))
		{
			Page* p = pk.free.front();
			if (absoluteFrame - p->freed_frame < k_staging_page_idle_frames) break;
			pk.free.pop_front();
			_destroy_page(p);
		}
	}

	void DX12StagingBufferPool::begin_frame(uint64_t absoluteFrame)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_currentAbsoluteFrame = absoluteFrame;

		_reclaim_kind(m_upload, absoluteFrame);
		_reclaim_kind(m_readback, absoluteFrame);

		const uint32_t total_in_use =
			static_cast<uint32_t>(m_upload.in_use.size() + m_readback.in_use.size())
			+ (m_upload.active ? 1u : 0u) + (m_readback.active ? 1u : 0u);
		m_inUseRing[m_ringIndex % k_high_water_window] = total_in_use;
		m_ringIndex++;

		uint32_t high_water = 0;
		for (uint32_t v : m_inUseRing) if (v > high_water) high_water = v;

		_evict_kind(m_upload, absoluteFrame, high_water);
		_evict_kind(m_readback, absoluteFrame, high_water);
	}
}
