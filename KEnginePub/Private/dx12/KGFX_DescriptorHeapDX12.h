#pragma once
#include "KGFX_DX12Header.h"
#include "DMA_2.0.0/D3D12MemAlloc.h"

namespace gfx
{
    /**
     * 线性的heap池，如果仅是CPU可见可以不断扩容
     */
    class D3D12DescriptorHeap
    {
    public:
        D3D12DescriptorHeap() = default;
        ~D3D12DescriptorHeap();
        D3D12DescriptorHeap(const D3D12DescriptorHeap&) = delete;
        D3D12DescriptorHeap operator =(const D3D12DescriptorHeap&) = delete;
        D3D12DescriptorHeap(const D3D12DescriptorHeap&&) = delete;
        D3D12DescriptorHeap operator =(const D3D12DescriptorHeap&&) = delete;

        bool Init(uint32_t size, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags);

        bool Init(
            const D3D12_CPU_DESCRIPTOR_HANDLE* handles,
            uint32_t numHandles,
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            D3D12_DESCRIPTOR_HEAP_FLAGS flags
        );

        int GetUsedSize() const;

        int GetTotalSize() const;
        uint32_t Allocate();
        uint32_t Allocate(uint32_t numDescriptors);

        uint32_t PlaceAt(uint32_t index);

        void DeallocateAll();

        int GetDescriptorSize() const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuStart() const;

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuStart() const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const;

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const;

        ID3D12DescriptorHeap* GetHeap() const;

    private:
        ID3D12DescriptorHeap* m_Heap = nullptr;

        uint32_t m_TotalSize = 0;

        uint32_t m_CurrentIndex = 0;

        uint32_t m_DescriptorSize = 0;

        D3D12_DESCRIPTOR_HEAP_FLAGS m_HeapFlags = {D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    };

    /**
     * 一个记录池分配状态的链表
     */
    class VirtualObjectPool
    {
    public:
        ~VirtualObjectPool();

        struct FreeListNode
        {
            int Offset;
            int Length;
            FreeListNode* prev;
            FreeListNode* next;
        };

        FreeListNode* m_FreeListHead = nullptr;

        void Destroy();

        void InitPool(int numElements);

        int Alloc(int size);

        void Free(int offset, int size);
    };

    /**
     * 线性heap池加了状态记录，可以单独释放某个descriptor
     */
    class D3D12GeneralDescriptorHeap 
    {
    public:
        D3D12GeneralDescriptorHeap() = default;
        ~D3D12GeneralDescriptorHeap();
        D3D12GeneralDescriptorHeap(const D3D12GeneralDescriptorHeap&) = delete;
        D3D12GeneralDescriptorHeap operator =(const D3D12GeneralDescriptorHeap&) = delete;
        D3D12GeneralDescriptorHeap(const D3D12GeneralDescriptorHeap&&) = delete;
        D3D12GeneralDescriptorHeap operator =(const D3D12GeneralDescriptorHeap&&) = delete;

        int GetSize() const;

        bool Init(int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag);

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const;
        ID3D12DescriptorHeap* GetHeap() { return m_Heap.GetHeap(); }

        int Allocate(int count);

        bool Allocate(D3D12Descriptor* outDescriptor);

        void Free(int index, int count);

        void Free(D3D12Descriptor descriptor);

    private:
        int m_ChunkSize = 0;
        D3D12_DESCRIPTOR_HEAP_TYPE m_Type = {};

        D3D12DescriptorHeap m_Heap = {};
        VirtualObjectPool m_Allocator = {};
    };

    /**
     * 包含多个D3D12GeneralDescriptorHeap的池，可以单独释放某个descriptor
     */
    class D3D12GeneralExpandingDescriptorHeap
    {
    public:
        D3D12GeneralExpandingDescriptorHeap() = default;
        ~D3D12GeneralExpandingDescriptorHeap();
        D3D12GeneralExpandingDescriptorHeap(const D3D12GeneralExpandingDescriptorHeap&) = delete;
        D3D12GeneralExpandingDescriptorHeap operator =(const D3D12GeneralExpandingDescriptorHeap&) = delete;
        D3D12GeneralExpandingDescriptorHeap(const D3D12GeneralExpandingDescriptorHeap&&) = delete;
        D3D12GeneralExpandingDescriptorHeap operator =(const D3D12GeneralExpandingDescriptorHeap&&) = delete;

        bool NewSubHeap();

        int GetSubHeapIndex(int descriptorIndex) const;

        bool Init(int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag);

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const;

        int Allocate(int count);

        bool Allocate(D3D12Descriptor* outDescriptor);

        void Free(int index, int count) const;

        void Free(D3D12Descriptor descriptor) const;

    private:
        D3D12_DESCRIPTOR_HEAP_TYPE m_Type = {};
        D3D12_DESCRIPTOR_HEAP_FLAGS m_Flag = {};

        int m_ChunkSize = 0;
        std::vector<D3D12GeneralDescriptorHeap*> m_SubHeaps = {};
        std::vector<int> m_SubHeapStartingIndex = {};
    };

    /**
     * 包含多个D3D12DescriptorHeap的池，不能单独释放descriptor
     */
    class D3D12LinearExpandingDescriptorHeap
    {
    public:
        D3D12LinearExpandingDescriptorHeap() = default;
        ~D3D12LinearExpandingDescriptorHeap();
        D3D12LinearExpandingDescriptorHeap(const D3D12LinearExpandingDescriptorHeap&) = delete;
        D3D12LinearExpandingDescriptorHeap operator =(const D3D12LinearExpandingDescriptorHeap&) = delete;
        D3D12LinearExpandingDescriptorHeap(const D3D12LinearExpandingDescriptorHeap&&) = delete;
        D3D12LinearExpandingDescriptorHeap operator =(const D3D12LinearExpandingDescriptorHeap&&) = delete;

        bool NewSubHeap();

        bool Init(ID3D12Device* device, int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag);

        int Allocate(int count);

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const;

        void Free(int index, int count) const;

        void Free(D3D12Descriptor descriptor) const;

        void FreeAll();

    private:
        D3D12_DESCRIPTOR_HEAP_TYPE m_Type = {};
        D3D12_DESCRIPTOR_HEAP_FLAGS m_Flag = {};
        int m_ChunkSize = 0;
        int32_t m_SubHeapIndex = {};
        std::vector<D3D12DescriptorHeap*> m_SubHeaps = {};
    };
}
