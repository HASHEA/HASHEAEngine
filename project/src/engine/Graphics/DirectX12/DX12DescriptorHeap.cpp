#include "DX12DescriptorHeap.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include <utility>

namespace RHI
{
	namespace
	{
		const char* descriptor_heap_type_name(D3D12_DESCRIPTOR_HEAP_TYPE type)
		{
			switch (type)
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
				return "CBV_SRV_UAV";
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				return "Sampler";
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				return "RTV";
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				return "DSV";
			default:
				return "Unknown";
			}
		}
	}

	// ──────────────────────────────────────────────────────────────
	// DX12CPUDescriptorHeap
	// ──────────────────────────────────────────────────────────────
	bool DX12CPUDescriptorHeap::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxDescriptors)
	{
		m_type = type;
		m_maxDescriptors = maxDescriptors;
		m_currentIndex = 0;
		m_nextAllocationSerial = 0;
		m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = maxDescriptors;
		heapDesc.Type = type;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // CPU-only
		heapDesc.NodeMask = 0;

		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
		if (FAILED(hr))
		{
			HLogError("DX12CPUDescriptorHeap: Failed to create heap. Type: {}, HRESULT: 0x{:08X}", (int)type, (uint32_t)hr);
			return false;
		}
		const std::string debugName = std::string("DX12 CPU Descriptor Heap ") + descriptor_heap_type_name(type);
		dx12_set_debug_name(m_heap.Get(), debugName.c_str());

		m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		return true;
	}

	void DX12CPUDescriptorHeap::shutdown()
	{
		m_heap.Reset();
		m_freeList.clear();
	}

	DX12DescriptorHandle DX12CPUDescriptorHeap::allocate()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_nextAllocationSerial == UINT64_MAX)
		{
			HLogError("DX12CPUDescriptorHeap: descriptor allocation serial exhausted. Type: {}", descriptor_heap_type_name(m_type));
			H_ASSERT(false);
			return {};
		}

		DX12DescriptorHandle handle = {};
		handle.allocationSerial = ++m_nextAllocationSerial;
		uint32_t index = 0;

		if (!m_freeList.empty())
		{
			index = m_freeList.back();
			m_freeList.pop_back();
		}
		else
		{
			H_ASSERT(m_currentIndex < m_maxDescriptors);
			index = m_currentIndex++;
		}

		handle.cpuHandle.ptr = m_cpuStart.ptr + static_cast<SIZE_T>(index) * m_descriptorSize;
		handle.gpuHandle = {}; // CPU-only heap has no GPU handle
		handle.heapIndex = index;
		return handle;
	}

	void DX12CPUDescriptorHeap::free(const DX12DescriptorHandle& handle)
	{
		if (handle.heapIndex == UINT32_MAX)
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		m_freeList.push_back(handle.heapIndex);
	}

	// ──────────────────────────────────────────────────────────────
	// DX12GPUDescriptorHeap
	// ──────────────────────────────────────────────────────────────
	bool DX12GPUDescriptorHeap::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxDescriptors)
	{
		m_type = type;
		m_maxDescriptors = maxDescriptors;
		m_currentOffset = 0;
		m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = maxDescriptors;
		heapDesc.Type = type;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NodeMask = 0;

		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
		if (FAILED(hr))
		{
			HLogError("DX12GPUDescriptorHeap: Failed to create shader-visible heap. Type: {}, HRESULT: 0x{:08X}", (int)type, (uint32_t)hr);
			return false;
		}
		const std::string debugName = std::string("DX12 GPU Descriptor Heap ") + descriptor_heap_type_name(type);
		dx12_set_debug_name(m_heap.Get(), debugName.c_str());

		m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
		return true;
	}

	void DX12GPUDescriptorHeap::shutdown()
	{
		m_heap.Reset();
	}

	void DX12GPUDescriptorHeap::begin_frame(uint32_t frameIndex, uint32_t frameCount)
	{
		if (frameCount == 0)
		{
			frameCount = 1;
		}

		const uint32_t frameCapacity = m_maxDescriptors / frameCount;
		if (frameCapacity == 0)
		{
			HLogError(
				"DX12GPUDescriptorHeap: descriptor heap capacity {} cannot be split across {} frame(s).",
				m_maxDescriptors,
				frameCount);
			H_ASSERT(false);
			m_frameStartOffset = 0;
			m_frameEndOffset = m_maxDescriptors;
			m_currentOffset = 0;
			return;
		}

		const uint32_t safeFrameIndex = frameIndex % frameCount;
		m_frameStartOffset = safeFrameIndex * frameCapacity;
		m_frameEndOffset = safeFrameIndex == frameCount - 1 ? m_maxDescriptors : m_frameStartOffset + frameCapacity;
		m_currentOffset = m_frameStartOffset;
	}

	DX12DescriptorHandle DX12GPUDescriptorHeap::allocate(uint32_t count)
	{
		DX12DescriptorHandle handle = {};
		if (m_frameEndOffset == 0)
		{
			m_frameStartOffset = 0;
			m_frameEndOffset = m_maxDescriptors;
			m_currentOffset = 0;
		}

		if (count == 0 || count > m_frameEndOffset - m_currentOffset)
		{
			HLogError(
				"DX12GPUDescriptorHeap: descriptor heap frame partition overflow. type={}, requested={}, remaining={}, frame_start={}, frame_end={}, capacity={}.",
				descriptor_heap_type_name(m_type),
				count,
				m_frameEndOffset - m_currentOffset,
				m_frameStartOffset,
				m_frameEndOffset,
				m_maxDescriptors);
			H_ASSERT(false);
			return handle;
		}

		handle.cpuHandle.ptr = m_cpuStart.ptr + static_cast<SIZE_T>(m_currentOffset) * m_descriptorSize;
		handle.gpuHandle.ptr = m_gpuStart.ptr + static_cast<UINT64>(m_currentOffset) * m_descriptorSize;
		handle.heapIndex = m_currentOffset;
		m_currentOffset += count;
		return handle;
	}

	void DX12GPUDescriptorHeap::copy_descriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srcCPU, DX12DescriptorHandle& dst)
	{
		device->CopyDescriptorsSimple(1, dst.cpuHandle, srcCPU, m_type);
	}

	// ──────────────────────────────────────────────────────────────
	// DX12DescriptorHeapManager
	// ──────────────────────────────────────────────────────────────
	bool DX12DescriptorHeapManager::init(ID3D12Device* device)
	{
		if (!cpuCbvSrvUav.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16384)) return false;
		if (!cpuSampler.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256)) return false;
		if (!cpuRtv.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 512)) return false;
		if (!cpuDsv.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 128)) return false;
		if (!gpuCbvSrvUav.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536)) return false;
		if (!gpuSampler.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048)) return false;
		return true;
	}

	void DX12DescriptorHeapManager::shutdown()
	{
		m_frameCbvSrvUavTableCache.clear();
		m_frameSamplerTableCache.clear();
		cpuCbvSrvUav.shutdown();
		cpuSampler.shutdown();
		cpuRtv.shutdown();
		cpuDsv.shutdown();
		gpuCbvSrvUav.shutdown();
		gpuSampler.shutdown();
	}

	void DX12DescriptorHeapManager::begin_frame(uint32_t frameIndex, uint32_t frameCount)
	{
		m_frameCbvSrvUavTableCache.clear();
		m_frameSamplerTableCache.clear();
		gpuCbvSrvUav.begin_frame(frameIndex, frameCount);
		gpuSampler.begin_frame(frameIndex, frameCount);
	}

	bool DX12DescriptorHeapManager::find_or_create_shader_visible_table(
		ID3D12Device* device,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		const DX12DescriptorHandle* descriptorHandles,
		uint32_t descriptorCount,
		DX12DescriptorHandle& outHandle)
	{
		outHandle = {};
		if (!device || !descriptorHandles || descriptorCount == 0)
		{
			return false;
		}

		const bool samplerHeap = heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		DX12GPUDescriptorHeap& targetHeap = samplerHeap ? gpuSampler : gpuCbvSrvUav;
		auto& tableCache = samplerHeap ? m_frameSamplerTableCache : m_frameCbvSrvUavTableCache;

		for (uint32_t index = 0; index < descriptorCount; ++index)
		{
			if (descriptorHandles[index].cpuHandle.ptr == 0)
			{
				HLogError("DX12DescriptorHeapManager: cannot cache a descriptor table with a null CPU handle.");
				return false;
			}
			if (descriptorHandles[index].allocationSerial == 0)
			{
				HLogError("DX12DescriptorHeapManager: cannot cache a descriptor table with a zero allocation serial.");
				return false;
			}
		}
		DescriptorTableCacheKey key = DescriptorTableCacheKey::from_handles(heapType, descriptorHandles, descriptorCount);

		const auto cached = tableCache.find(key);
		if (cached != tableCache.end())
		{
			outHandle = cached->second;
			return true;
		}

		DX12DescriptorHandle gpuHandle = targetHeap.allocate(descriptorCount);
		if (!gpuHandle.is_shader_visible())
		{
			return false;
		}

		if (descriptorCount == 1)
		{
			device->CopyDescriptorsSimple(1, gpuHandle.cpuHandle, descriptorHandles[0].cpuHandle, heapType);
		}
		else
		{
			for (uint32_t index = 0; index < descriptorCount; ++index)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE destination{};
				destination.ptr = gpuHandle.cpuHandle.ptr + static_cast<SIZE_T>(index) * targetHeap.get_descriptor_size();
				device->CopyDescriptorsSimple(1, destination, descriptorHandles[index].cpuHandle, heapType);
			}
		}

		outHandle = gpuHandle;
		tableCache.emplace(std::move(key), gpuHandle);
		return true;
	}
}
