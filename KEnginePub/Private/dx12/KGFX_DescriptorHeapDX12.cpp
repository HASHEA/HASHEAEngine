#include "KGFX_DescriptorHeapDX12.h"
#include "KGFX_GraphiceDeviceDX12.h"
#include "KBase/Public/KMemLeak.h"
namespace gfx
{
#pragma region D3D12DescriptorHeap
    bool D3D12DescriptorHeap::Init(uint32_t size, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
    {
        HRESULT hrRes = E_FAIL;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = size;
        srvHeapDesc.Flags = flags;
        srvHeapDesc.Type = type;
        hrRes = pD3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_Heap));
        KG_COM_PROCESS_ERROR(hrRes);

        m_DescriptorSize = pD3dDevice->GetDescriptorHandleIncrementSize(type);
        m_TotalSize = size;
        m_HeapFlags = flags;

        return true;
    Exit0:
        SAFE_RELEASE(m_Heap);
        return false;
    }

    bool D3D12DescriptorHeap::Init(const D3D12_CPU_DESCRIPTOR_HANDLE* handles, uint32_t numHandles, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        D3D12_CPU_DESCRIPTOR_HANDLE dst = {};
        bool bRes = Init(numHandles, type, flags);
        KG_PROCESS_ERROR(bRes);

        dst = m_Heap->GetCPUDescriptorHandleForHeapStart();

        for (uint32_t i = 0; i < numHandles; i++, dst.ptr += m_DescriptorSize)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE src = handles[i];
            if (src.ptr != 0)
            {
                pD3dDevice->CopyDescriptorsSimple(1, dst, src, type);
            }
        }

        return true;
    Exit0:
        return false;
    }

    uint32_t D3D12DescriptorHeap::Allocate()
    {
        return Allocate(1);
    }

    uint32_t D3D12DescriptorHeap::Allocate(uint32_t numDescriptors)
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        uint32_t index = 0;
        if (m_CurrentIndex + numDescriptors <= m_TotalSize)
        {
            index = m_CurrentIndex;
            m_CurrentIndex += numDescriptors;
            return index;
        }

        /// 对于GPU上使用的view池，不能做动态扩容
        if (m_HeapFlags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        {
            assert(false);
            return -1;
        }


        ID3D12DescriptorHeap* oldHeap = m_Heap;
        uint32_t currentIndex = m_CurrentIndex;
        D3D12_DESCRIPTOR_HEAP_DESC desc = m_Heap->GetDesc();

        HRESULT HrRes = Init(desc.NumDescriptors * 2, desc.Type, desc.Flags);
        KG_COM_PROCESS_ERROR(HrRes);

        pD3dDevice->CopyDescriptorsSimple(
            currentIndex,
            m_Heap->GetCPUDescriptorHandleForHeapStart(),
            oldHeap->GetCPUDescriptorHandleForHeapStart(),
            desc.Type
        );
        SAFE_RELEASE(oldHeap);

        m_CurrentIndex = currentIndex;
        index = m_CurrentIndex;
        m_CurrentIndex += numDescriptors;
        return index;

    Exit0:
        return -1;
    }

    uint32_t D3D12DescriptorHeap::PlaceAt(uint32_t index)
    {
        ASSERT(index < m_TotalSize);
        m_CurrentIndex = index + 1;

        return index;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetGpuHandle(int index) const
    {
        ASSERT(index >= 0 && index < (int)m_TotalSize);
        D3D12_GPU_DESCRIPTOR_HANDLE start = m_Heap->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE dst;
        dst.ptr = start.ptr + static_cast<size_t>(m_DescriptorSize) * index;
        return dst;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetCpuHandle(int index) const
    {
        ASSERT(index >= 0 && index < (int)m_TotalSize);
        D3D12_CPU_DESCRIPTOR_HANDLE start = m_Heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dst;
        dst.ptr = start.ptr + static_cast<size_t>(m_DescriptorSize) * index;
        return dst;
    }

    D3D12DescriptorHeap::~D3D12DescriptorHeap()
    {
        SAFE_RELEASE(m_Heap);
    }

    int D3D12DescriptorHeap::GetUsedSize() const
    {
        return m_CurrentIndex;
    }

    int D3D12DescriptorHeap::GetTotalSize() const
    {
        return m_TotalSize;
    }

    void D3D12DescriptorHeap::DeallocateAll()
    {
        m_CurrentIndex = 0;
    }

    int D3D12DescriptorHeap::GetDescriptorSize() const
    {
        return m_DescriptorSize;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetGpuStart() const
    {
        return m_Heap->GetGPUDescriptorHandleForHeapStart();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetCpuStart() const
    {
        return m_Heap->GetCPUDescriptorHandleForHeapStart();
    }

    ID3D12DescriptorHeap* D3D12DescriptorHeap::GetHeap() const
    {
        return m_Heap;
    }

#pragma endregion


    VirtualObjectPool::~VirtualObjectPool()
    {
        Destroy();
    }

    void VirtualObjectPool::Destroy()
    {
        auto list = m_FreeListHead;
        while (list)
        {
            auto next = list->next;
            delete list;
            list = next;
        }
        m_FreeListHead = nullptr;
    }

    void VirtualObjectPool::InitPool(int numElements)
    {
        m_FreeListHead = new FreeListNode();
        m_FreeListHead->prev = nullptr;
        m_FreeListHead->next = nullptr;
        m_FreeListHead->Offset = 0;
        m_FreeListHead->Length = numElements;
    }

    int VirtualObjectPool::Alloc(int size)
    {
        if (!m_FreeListHead)
            return -1;

        FreeListNode* freeBlock = m_FreeListHead;

        while (freeBlock && freeBlock->Length < size)
            freeBlock = freeBlock->next;

        if (!freeBlock || freeBlock->Length < size)
            return -1;

        int result = freeBlock->Offset;
        freeBlock->Offset += size;
        freeBlock->Length -= size;

        if (freeBlock->Length == 0)
        {
            if (freeBlock->prev)
                freeBlock->prev->next = freeBlock->next;

            if (freeBlock->next)
                freeBlock->next->prev = freeBlock->prev;

            if (freeBlock == m_FreeListHead)
                m_FreeListHead = freeBlock->next;

            delete freeBlock;
        }

        return result;
    }

    void VirtualObjectPool::Free(int offset, int size)
    {
        if (!m_FreeListHead)
        {
            m_FreeListHead = new FreeListNode();
            m_FreeListHead->next = m_FreeListHead->prev = nullptr;
            m_FreeListHead->Length = size;
            m_FreeListHead->Offset = offset;
            return;
        }

        auto freeListNode = m_FreeListHead;
        FreeListNode* prevFreeNode = nullptr;
        while (freeListNode && freeListNode->Offset < offset + size)
        {
            prevFreeNode = freeListNode;
            freeListNode = freeListNode->next;
        }

        FreeListNode* newNode = new FreeListNode();
        newNode->Offset = offset;
        newNode->Length = size;
        newNode->prev = prevFreeNode;
        newNode->next = freeListNode;

        if (freeListNode)
            freeListNode->prev = newNode;

        if (prevFreeNode)
            prevFreeNode->next = newNode;

        if (freeListNode == m_FreeListHead)
            m_FreeListHead = newNode;

        if (prevFreeNode && prevFreeNode->Offset + prevFreeNode->Length == newNode->Offset)
        {
            prevFreeNode->Length += newNode->Length;
            prevFreeNode->next = freeListNode;
            if (freeListNode)
                freeListNode->prev = prevFreeNode;

            delete newNode;
            newNode = prevFreeNode;
        }

        if (freeListNode && newNode->Offset + newNode->Length == freeListNode->Offset)
        {
            newNode->Length += freeListNode->Length;
            newNode->next = freeListNode->next;
            if (freeListNode->next)
                freeListNode->next->prev = newNode;

            delete freeListNode;
        }
    }

    D3D12GeneralDescriptorHeap::~D3D12GeneralDescriptorHeap()= default;

    int D3D12GeneralDescriptorHeap::GetSize() const
    {
        return m_ChunkSize;
    }

    bool D3D12GeneralDescriptorHeap::Init(int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
    {
        m_ChunkSize = chunkSize;
        m_Type = type;

        bool bRes = m_Heap.Init(m_ChunkSize, m_Type, flag);
        KG_PROCESS_ERROR(bRes);

        m_Allocator.InitPool(m_ChunkSize);

        return true;
    Exit0:
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12GeneralDescriptorHeap::GetCpuHandle(int index) const
    {
        return m_Heap.GetCpuHandle(index);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12GeneralDescriptorHeap::GetGpuHandle(int index) const
    {
        return m_Heap.GetGpuHandle(index);
    }

    int D3D12GeneralDescriptorHeap::Allocate(int count)
    {
        return m_Allocator.Alloc(count);
    }

    bool D3D12GeneralDescriptorHeap::Allocate(D3D12Descriptor* outDescriptor)
    {
        int index = m_Allocator.Alloc(1);
        KG_PROCESS_ERROR(index < 0);

        D3D12Descriptor descriptor;
        descriptor.cpuHandle = m_Heap.GetCpuHandle(index);
        *outDescriptor = descriptor;

        return true;
    Exit0:
        return false;
    }

    void D3D12GeneralDescriptorHeap::Free(int index, int count)
    {
        m_Allocator.Free(index, count);
    }

    void D3D12GeneralDescriptorHeap::Free(D3D12Descriptor descriptor)
    {
        int index = static_cast<int>((descriptor.cpuHandle.ptr - m_Heap.GetCpuStart().ptr) / static_cast<size_t>(m_Heap.GetDescriptorSize()));
        Free(index, 1);
    }

    D3D12GeneralExpandingDescriptorHeap::~D3D12GeneralExpandingDescriptorHeap()
    {
        for (auto& subHeap : m_SubHeaps)
            SAFE_DELETE(subHeap);
    }

    bool D3D12GeneralExpandingDescriptorHeap::NewSubHeap()
    {
        D3D12GeneralDescriptorHeap* subHeap = new D3D12GeneralDescriptorHeap();
        bool bRes = subHeap->Init(m_ChunkSize, m_Type, m_Flag);
        KG_PROCESS_ERROR(bRes);

        m_SubHeaps.push_back(subHeap);
        if (!m_SubHeapStartingIndex.empty())
        {
            m_SubHeapStartingIndex.push_back(m_SubHeapStartingIndex.back() + m_SubHeaps.back()->GetSize());
        }
        else
        {
            m_SubHeapStartingIndex.push_back(0);
        }

        return true;
    Exit0:
        return false;
    }

    bool D3D12GeneralExpandingDescriptorHeap::Init(int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
    {
        m_ChunkSize = chunkSize;
        m_Type = type;
        m_Flag = flag;
        return NewSubHeap();
    }

    bool D3D12GeneralExpandingDescriptorHeap::Allocate(D3D12Descriptor* outDescriptor)
    {
        int index = Allocate(1);
        KG_PROCESS_ERROR(index >= 0);

        D3D12Descriptor descriptor;
        descriptor.cpuHandle = GetCpuHandle(index);

        *outDescriptor = descriptor;

        return true;
    Exit0:
        return false;
    }

    int D3D12GeneralExpandingDescriptorHeap::GetSubHeapIndex(int descriptorIndex) const
    {
        int l = 0;
        int r = static_cast<int>(m_SubHeapStartingIndex.size());

        while (l < r - 1)
        {
            int m = l + (r - l) / 2;
            if (m_SubHeapStartingIndex[m] < descriptorIndex)
            {
                l = m;
            }
            else if (m_SubHeapStartingIndex[m] > descriptorIndex)
            {
                r = m;
            }
            else
            {
                return m;
            }
        }

        assert(m_SubHeapStartingIndex[l] <= descriptorIndex && m_SubHeapStartingIndex[l] + m_SubHeaps[l]->GetSize() > descriptorIndex);

        return l;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12GeneralExpandingDescriptorHeap::GetCpuHandle(int index) const
    {
        auto subHeapIndex = GetSubHeapIndex(index);
        return m_SubHeaps[subHeapIndex]->GetCpuHandle(index - m_SubHeapStartingIndex[subHeapIndex]);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12GeneralExpandingDescriptorHeap::GetGpuHandle(int index) const
    {
        auto subHeapIndex = GetSubHeapIndex(index);
        return m_SubHeaps[subHeapIndex]->GetGpuHandle(index - m_SubHeapStartingIndex[subHeapIndex]);
    }

    int D3D12GeneralExpandingDescriptorHeap::Allocate(int count)
    {
        auto result = m_SubHeaps.back()->Allocate(count);
        if (result == -1)
        {
            NewSubHeap();
            return Allocate(count);
        }
        return result + m_SubHeapStartingIndex.back();
    }

    void D3D12GeneralExpandingDescriptorHeap::Free(int index, int count) const
    {
        auto subHeapIndex = GetSubHeapIndex(index);
        m_SubHeaps[subHeapIndex]->Free(index - m_SubHeapStartingIndex[subHeapIndex], count);
    }

    void D3D12GeneralExpandingDescriptorHeap::Free(D3D12Descriptor descriptor) const
    {
        for (auto& subHeap : m_SubHeaps)
        {
            if (descriptor.cpuHandle.ptr >= subHeap->GetCpuHandle(0).ptr)
            {
                auto subIndex = descriptor.cpuHandle.ptr - subHeap->GetCpuHandle(0).ptr;
                if (subIndex < static_cast<SIZE_T>(subHeap->GetSize()))
                {
                    subHeap->Free(descriptor);
                    break;
                }
            }
        }
    }

    D3D12LinearExpandingDescriptorHeap::~D3D12LinearExpandingDescriptorHeap()
    {
        for (auto& subHeap : m_SubHeaps)
            SAFE_DELETE(subHeap);
    }

    bool D3D12LinearExpandingDescriptorHeap::NewSubHeap()
    {
        m_SubHeapIndex++;
        if (m_SubHeapIndex <= m_SubHeaps.size())
        {
            D3D12DescriptorHeap* subHeap = new D3D12DescriptorHeap();
            bool bRes = subHeap->Init(m_ChunkSize, m_Type, m_Flag);
            KG_PROCESS_ERROR(bRes);
            m_SubHeaps.push_back(subHeap);
        }

        return true;
    Exit0:
        return false;
    }

    bool D3D12LinearExpandingDescriptorHeap::Init(ID3D12Device* device, int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
    {
        m_ChunkSize = chunkSize;
        m_Type = type;
        m_Flag = flag;
        m_SubHeapIndex = -1;
        return NewSubHeap();
    }

    int D3D12LinearExpandingDescriptorHeap::Allocate(int count)
    {
        auto result = m_SubHeaps[m_SubHeapIndex]->Allocate(count);
        if (result == -1)
        {
            NewSubHeap();
            return Allocate(count);
        }
        assert(result <= 0xFFFFFF);
        assert(m_SubHeapIndex <= 255);
        return (m_SubHeapIndex << 24) + result;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12LinearExpandingDescriptorHeap::GetCpuHandle(int index) const
    {
        auto subHeapIndex = static_cast<uint32_t>(index >> 24) & 0xFF;
        return m_SubHeaps[subHeapIndex]->GetCpuHandle(index & 0xFFFFFF);
    }

    void D3D12LinearExpandingDescriptorHeap::Free(int index, int count) const
    {
        assert(false);
    }

    void D3D12LinearExpandingDescriptorHeap::Free(D3D12Descriptor descriptor) const
    {
        assert(false);
    }

    void D3D12LinearExpandingDescriptorHeap::FreeAll()
    {
        for (auto& subHeap : m_SubHeaps)
            subHeap->DeallocateAll();
        m_SubHeapIndex = 0;
    }
}
