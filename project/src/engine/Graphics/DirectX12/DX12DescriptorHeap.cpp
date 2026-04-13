#include "DX12DescriptorHeap.h"
#include "Base/hlog.h"
#include "Base/hassert.h"

namespace RHI
{
	// ──────────────────────────────────────────────────────────────
	// DX12CPUDescriptorHeap
	// ──────────────────────────────────────────────────────────────
	bool DX12CPUDescriptorHeap::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxDescriptors)
	{
		m_type = type;
		m_maxDescriptors = maxDescriptors;
		m_currentIndex = 0;
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

		DX12DescriptorHandle handle = {};
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

		m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
		return true;
	}

	void DX12GPUDescriptorHeap::shutdown()
	{
		m_heap.Reset();
	}

	void DX12GPUDescriptorHeap::reset_frame_allocation()
	{
		m_currentOffset = 0;
	}

	DX12DescriptorHandle DX12GPUDescriptorHeap::allocate(uint32_t count)
	{
		H_ASSERT(m_currentOffset + count <= m_maxDescriptors);

		DX12DescriptorHandle handle = {};
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
		cpuCbvSrvUav.shutdown();
		cpuSampler.shutdown();
		cpuRtv.shutdown();
		cpuDsv.shutdown();
		gpuCbvSrvUav.shutdown();
		gpuSampler.shutdown();
	}

	void DX12DescriptorHeapManager::begin_frame()
	{
		gpuCbvSrvUav.reset_frame_allocation();
		gpuSampler.reset_frame_allocation();
	}
}
