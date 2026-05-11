#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include <vector>
#include <mutex>
#include <unordered_map>

namespace RHI
{
	// CPU-side descriptor heap for staging descriptors (non-shader-visible)
	class DX12CPUDescriptorHeap
	{
	public:
		DX12CPUDescriptorHeap() = default;
		~DX12CPUDescriptorHeap() = default;

		bool init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxDescriptors);
		void shutdown();

		DX12DescriptorHandle allocate();
		void free(const DX12DescriptorHandle& handle);

		D3D12_DESCRIPTOR_HEAP_TYPE get_type() const { return m_type; }
		uint32_t get_descriptor_size() const { return m_descriptorSize; }
		ID3D12DescriptorHeap* get_heap() const { return m_heap.Get(); }

	private:
		ComPtr<ID3D12DescriptorHeap> m_heap;
		D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		uint32_t m_descriptorSize = 0;
		uint32_t m_maxDescriptors = 0;
		uint32_t m_currentIndex = 0;
		std::vector<uint32_t> m_freeList;
		std::mutex m_mutex;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
	};

	// GPU-visible descriptor heap for shader binding (ring buffer per frame)
	class DX12GPUDescriptorHeap
	{
	public:
		DX12GPUDescriptorHeap() = default;
		~DX12GPUDescriptorHeap() = default;

		bool init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxDescriptors);
		void shutdown();

		// Reset allocation offset (call at frame begin)
		void reset_frame_allocation();

		// Allocate a contiguous range of descriptors for this frame
		DX12DescriptorHandle allocate(uint32_t count = 1);

		// Copy from CPU heap to this GPU heap
		void copy_descriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srcCPU, DX12DescriptorHandle& dst);

		ID3D12DescriptorHeap* get_heap() const { return m_heap.Get(); }
		uint32_t get_descriptor_size() const { return m_descriptorSize; }

	private:
		ComPtr<ID3D12DescriptorHeap> m_heap;
		D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		uint32_t m_descriptorSize = 0;
		uint32_t m_maxDescriptors = 0;
		uint32_t m_currentOffset = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
	};

	// Manages all descriptor heaps for the DX12 backend
	struct DX12DescriptorHeapManager
	{
		struct DescriptorTableCacheKey
		{
			D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			std::vector<SIZE_T> cpuHandles{};

			bool operator==(const DescriptorTableCacheKey& other) const
			{
				return heapType == other.heapType && cpuHandles == other.cpuHandles;
			}
		};

		struct DescriptorTableCacheKeyHasher
		{
			size_t operator()(const DescriptorTableCacheKey& key) const
			{
				size_t hash = static_cast<size_t>(key.heapType);
				for (SIZE_T handle : key.cpuHandles)
				{
					hash ^= static_cast<size_t>(handle) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
				}
				return hash;
			}
		};

		DX12CPUDescriptorHeap cpuCbvSrvUav;   // CPU-only staging for CBV/SRV/UAV
		DX12CPUDescriptorHeap cpuSampler;     // CPU-only staging for Samplers
		DX12CPUDescriptorHeap cpuRtv;         // RTV heap (always CPU-only)
		DX12CPUDescriptorHeap cpuDsv;         // DSV heap (always CPU-only)
		DX12GPUDescriptorHeap gpuCbvSrvUav;   // Shader-visible CBV/SRV/UAV
		DX12GPUDescriptorHeap gpuSampler;     // Shader-visible Sampler

		bool init(ID3D12Device* device);
		void shutdown();
		void begin_frame();
		bool find_or_create_shader_visible_table(
			ID3D12Device* device,
			D3D12_DESCRIPTOR_HEAP_TYPE heapType,
			const D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandles,
			uint32_t descriptorCount,
			DX12DescriptorHandle& outHandle);

	private:
		std::unordered_map<DescriptorTableCacheKey, DX12DescriptorHandle, DescriptorTableCacheKeyHasher> m_frameCbvSrvUavTableCache{};
		std::unordered_map<DescriptorTableCacheKey, DX12DescriptorHandle, DescriptorTableCacheKeyHasher> m_frameSamplerTableCache{};
	};
}
