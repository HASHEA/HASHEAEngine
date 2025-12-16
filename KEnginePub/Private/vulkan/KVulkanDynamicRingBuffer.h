#pragma once

#include <vector>
#include "KVulkanFunc.h"
#include "KVulkanDevice.h"

namespace gfx
{
	class Ring
	{
	public:
		void Create(uint32_t TotalSize)
		{
			m_Head = 0;
			m_AllocatedSize = 0;
			m_TotalSize = TotalSize;
		}

		uint32_t GetSize() { return m_AllocatedSize; }
		uint32_t GetHead() { return m_Head; }
		uint32_t GetTail() { return (m_Head + m_AllocatedSize) % m_TotalSize; }

		//helper to avoid allocating chunks that wouldn't fit contiguously in the ring
		uint32_t PaddingToAvoidCrossOver(uint32_t size)
		{
			int tail = GetTail();

			if ((tail + size) > m_TotalSize)
			{
				return (m_TotalSize - tail);
			}

			return 0;
		}

		bool Alloc(uint32_t size, uint32_t* pOut)
		{
			if (m_AllocatedSize + size <= m_TotalSize)
			{
				if (pOut)
				{
					*pOut = GetTail();
				}

				m_AllocatedSize += size;
				return true;
			}

			//assert(false);
			return false;
		}

		bool Free(uint32_t size)
		{
			if (m_AllocatedSize >= size)
			{
				m_Head = (m_Head + size) % m_TotalSize;
				m_AllocatedSize -= size;
				return true;
			}

			return false;
		}
	private:
		uint32_t m_Head;
		uint32_t m_AllocatedSize;
		uint32_t m_TotalSize;
	};

	// 
	// This class can be thought as ring buffer inside a ring buffer. The outer ring is for , 
	// the frames and the internal one is for the resources that were allocated for that frame.
	// The size of the outer ring is typically the number of back buffers.
	//
	// When the outer ring is full, for the next allocation it automatically frees the entries 
	// of the oldest frame and makes those entries available for the next frame. This happens 
	// when you call 'OnBeginFrame()' 
	//
	class RingWithTabs
	{
	public:

		void OnCreate(uint32_t numberOfBackBuffers, uint32_t memTotalSize)
		{
			m_backBufferIndex = 0;
			m_numberOfBackBuffers = numberOfBackBuffers;

			//init mem per frame tracker
			m_memAllocatedInFrame = 0;
            m_allocatedMemPerBackBuffer.resize(numberOfBackBuffers + 1);
			for (uint32_t i = 0; i < numberOfBackBuffers + 1; i++)
			{
				m_allocatedMemPerBackBuffer[i] = 0;
			}

			m_mem.Create(memTotalSize);
		}

		void OnDestroy()
		{
			m_mem.Free(m_mem.GetSize());
		}

		bool Alloc(uint32_t size, uint32_t* pOut)
		{
			uint32_t padding = m_mem.PaddingToAvoidCrossOver(size);
			if (padding > 0)
			{
				m_memAllocatedInFrame += padding;

				if (m_mem.Alloc(padding, NULL) == false) //alloc chunk to avoid crossover, ignore offset        
				{
					return false;  //no mem, cannot allocate apdding
				}
			}

			if (m_mem.Alloc(size, pOut) == true)
			{
				m_memAllocatedInFrame += size;
				return true;
			}
			return false;
		}

		void BeginFrame()
		{
			m_allocatedMemPerBackBuffer[m_backBufferIndex] = m_memAllocatedInFrame;
			m_memAllocatedInFrame = 0;

			m_backBufferIndex = (m_backBufferIndex + 1) % m_numberOfBackBuffers;

			// free all the entries for the oldest buffer in one go
			uint32_t memToFree = m_allocatedMemPerBackBuffer[m_backBufferIndex];
			m_mem.Free(memToFree);
		}
	private:
		//internal ring buffer
		Ring m_mem;

		//this is the external ring buffer (I could have reused the Ring class though)
		uint32_t m_backBufferIndex;
		uint32_t m_numberOfBackBuffers;

		uint32_t m_memAllocatedInFrame;
		//uint32_t m_allocatedMemPerBackBuffer[4];
        std::vector<uint32_t> m_allocatedMemPerBackBuffer;
	};


	class KDynamicBufferRing : public KGFX_DelayReleaseObject
	{
	public:
		KDynamicBufferRing();
        virtual ~KDynamicBufferRing();
		VkResult Create(vks::KVulkanDevice* pDevice, uint32_t numberOfBackBuffers, uint32_t memTotalSize, char* name = NULL);
		void Destroy();
		bool AllocConstantBuffer(uint32_t size, void** pData, VkDescriptorBufferInfo& pOut, BOOL bShared);
		//VkDescriptorBufferInfo AllocConstantBuffer(uint32_t size, const void* pData);
		//VkDescriptorBufferInfo AllocConstantBuffer(VkCommandBuffer pCmdBuffer, uint32_t size, const void* pData);
		void BeginFrame();		
		VkBuffer GetVKBuffer() { return m_vkBuffer; }		
		void SetDescriptorSet(int i, uint32_t size, VkDescriptorSet descriptorSet);	
		//BOOL AllocConstantBuffer(uint32_t size, VkDescriptorBufferInfo& info);
		void ReUpdate(uint32_t uSize, uint32_t uOffset, const void* pData);
		inline int GetAllocCount() { return m_nAllocCount; }

	private:
		vks::KVulkanDevice*	m_pDevice;
		uint32_t        m_uMemTotalSize;
		RingWithTabs    m_mem{};
		char*			m_pData;
		VkBuffer        m_vkBuffer;
		int				m_nAlignment;
		int				m_nAllocSize;

		int				m_nAllocCount;

		VmaAllocation   m_pVmaAllocation = nullptr;
		VkDeviceMemory  m_deviceMemory = VK_NULL_HANDLE;
	};
}
