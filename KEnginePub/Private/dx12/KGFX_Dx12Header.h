#pragma once
#ifdef _WIN32

#include <windows.h>
//#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include "KEnginePub/Public/IGFX_Public.h"

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")


#ifndef D3D10_COMPONENT_MASK_X
#define D3D10_COMPONENT_MASK_X 1
#endif

#ifndef D3D10_COMPONENT_MASK_Y
#define D3D10_COMPONENT_MASK_Y 2
#endif

#ifndef D3D10_COMPONENT_MASK_Z
#define D3D10_COMPONENT_MASK_Z 4
#endif

#ifndef D3D10_COMPONENT_MASK_W
#define D3D10_COMPONENT_MASK_W 8
#endif

struct CD3DX12_DEFAULT
{
};

extern const DECLSPEC_SELECTANY CD3DX12_DEFAULT D3D12_DEFAULT;

#define STEPIFY(m_number, m_alignment) ((((m_number) + ((m_alignment) - 1)) / (m_alignment)) * (m_alignment))
constexpr uint32_t DX12_SWAPCHAIN_BUFFER_COUNT = 3;

namespace gfx
{
    struct CD3D12_DESCRIPTOR_HANDLE
    {
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle{ 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle{ 0 };
        ID3D12DescriptorHeap* m_pDescriptorHeap = nullptr;
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_HEAP_PROPERTIES : D3D12_HEAP_PROPERTIES
    {
        CD3DX12_HEAP_PROPERTIES() = default;

        explicit CD3DX12_HEAP_PROPERTIES(const D3D12_HEAP_PROPERTIES& o) :
            D3D12_HEAP_PROPERTIES(o)
        {
        }

        CD3DX12_HEAP_PROPERTIES(
            D3D12_CPU_PAGE_PROPERTY cpuPageProperty,
            D3D12_MEMORY_POOL memoryPoolPreference,
            uint32_t creationNodeMask = 1,
            uint32_t nodeMask = 1)
        {
            Type = D3D12_HEAP_TYPE_CUSTOM;
            CPUPageProperty = cpuPageProperty;
            MemoryPoolPreference = memoryPoolPreference;
            CreationNodeMask = creationNodeMask;
            VisibleNodeMask = nodeMask;
        }

        explicit CD3DX12_HEAP_PROPERTIES(
            D3D12_HEAP_TYPE type,
            uint32_t creationNodeMask = 1,
            uint32_t nodeMask = 1)
        {
            Type = type;
            CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            CreationNodeMask = creationNodeMask;
            VisibleNodeMask = nodeMask;
        }

        operator const D3D12_HEAP_PROPERTIES& () const { return *this; }

        bool IsCPUAccessible() const
        {
            return Type == D3D12_HEAP_TYPE_UPLOAD || Type == D3D12_HEAP_TYPE_READBACK || (Type == D3D12_HEAP_TYPE_CUSTOM &&
                (CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE || CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK));
        }
    };


    //------------------------------------------------------------------------------------------------
    struct CD3DX12_CPU_DESCRIPTOR_HANDLE : D3D12_CPU_DESCRIPTOR_HANDLE
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE()
        {
        }

        explicit CD3DX12_CPU_DESCRIPTOR_HANDLE(const D3D12_CPU_DESCRIPTOR_HANDLE& o) :
            D3D12_CPU_DESCRIPTOR_HANDLE(o)
        {
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE(CD3DX12_DEFAULT) { ptr = 0; }

        CD3DX12_CPU_DESCRIPTOR_HANDLE(const D3D12_CPU_DESCRIPTOR_HANDLE& other, int32_t offsetScaledByIncrementSize)
        {
            InitOffsetted(other, offsetScaledByIncrementSize);
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE(const D3D12_CPU_DESCRIPTOR_HANDLE& other, int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            InitOffsetted(other, offsetInDescriptors, descriptorIncrementSize);
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            ptr += offsetInDescriptors * descriptorIncrementSize;
            return *this;
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(int32_t offsetScaledByIncrementSize)
        {
            ptr += offsetScaledByIncrementSize;
            return *this;
        }

        bool operator==(const D3D12_CPU_DESCRIPTOR_HANDLE& other) const
        {
            return ptr == other.ptr;
        }

        bool operator!=(const D3D12_CPU_DESCRIPTOR_HANDLE& other) const
        {
            return ptr != other.ptr;
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE& operator=(const D3D12_CPU_DESCRIPTOR_HANDLE& other)
        {
            ptr = other.ptr;
            return *this;
        }

        void InitOffsetted(const D3D12_CPU_DESCRIPTOR_HANDLE& base, int32_t offsetScaledByIncrementSize)
        {
            InitOffsetted(*this, base, offsetScaledByIncrementSize);
        }

        void InitOffsetted(const D3D12_CPU_DESCRIPTOR_HANDLE& base, int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            InitOffsetted(*this, base, offsetInDescriptors, descriptorIncrementSize);
        }

        static void InitOffsetted(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE& handle, const D3D12_CPU_DESCRIPTOR_HANDLE& base, int32_t offsetScaledByIncrementSize)
        {
            handle.ptr = base.ptr + offsetScaledByIncrementSize;
        }

        static void InitOffsetted(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE& handle, const D3D12_CPU_DESCRIPTOR_HANDLE& base, int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            handle.ptr = base.ptr + offsetInDescriptors * descriptorIncrementSize;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_GPU_DESCRIPTOR_HANDLE : D3D12_GPU_DESCRIPTOR_HANDLE
    {
        CD3DX12_GPU_DESCRIPTOR_HANDLE()
        {
        }

        explicit CD3DX12_GPU_DESCRIPTOR_HANDLE(const D3D12_GPU_DESCRIPTOR_HANDLE& o) :
            D3D12_GPU_DESCRIPTOR_HANDLE(o)
        {
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE(CD3DX12_DEFAULT) { ptr = 0; }

        CD3DX12_GPU_DESCRIPTOR_HANDLE(const D3D12_GPU_DESCRIPTOR_HANDLE& other, int32_t offsetScaledByIncrementSize)
        {
            InitOffsetted(other, offsetScaledByIncrementSize);
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE(const D3D12_GPU_DESCRIPTOR_HANDLE& other, int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            InitOffsetted(other, offsetInDescriptors, descriptorIncrementSize);
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE& Offset(int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            ptr += offsetInDescriptors * descriptorIncrementSize;
            return *this;
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE& Offset(int32_t offsetScaledByIncrementSize)
        {
            ptr += offsetScaledByIncrementSize;
            return *this;
        }

        bool operator==(const D3D12_GPU_DESCRIPTOR_HANDLE& other) const
        {
            return ptr == other.ptr;
        }

        bool operator!=(const D3D12_GPU_DESCRIPTOR_HANDLE& other) const
        {
            return ptr != other.ptr;
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE& operator=(const D3D12_GPU_DESCRIPTOR_HANDLE& other)
        {
            ptr = other.ptr;
            return *this;
        }

        void InitOffsetted(
            const D3D12_GPU_DESCRIPTOR_HANDLE& base,
            int32_t offsetScaledByIncrementSize)
        {
            InitOffsetted(*this, base, offsetScaledByIncrementSize);
        }

        void InitOffsetted(const D3D12_GPU_DESCRIPTOR_HANDLE& base,
            int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            InitOffsetted(*this, base, offsetInDescriptors, descriptorIncrementSize);
        }

        static void InitOffsetted(_Out_ D3D12_GPU_DESCRIPTOR_HANDLE& handle, const D3D12_GPU_DESCRIPTOR_HANDLE& base, int32_t offsetScaledByIncrementSize)
        {
            handle.ptr = base.ptr + offsetScaledByIncrementSize;
        }

        static void InitOffsetted(_Out_ D3D12_GPU_DESCRIPTOR_HANDLE& handle, const D3D12_GPU_DESCRIPTOR_HANDLE& base, int32_t offsetInDescriptors, uint32_t descriptorIncrementSize)
        {
            handle.ptr = base.ptr + offsetInDescriptors * descriptorIncrementSize;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_RESOURCE_BARRIER : D3D12_RESOURCE_BARRIER
    {
        CD3DX12_RESOURCE_BARRIER()
        {
        }

        explicit CD3DX12_RESOURCE_BARRIER(const D3D12_RESOURCE_BARRIER& o) :
            D3D12_RESOURCE_BARRIER(o)
        {
        }

        static CD3DX12_RESOURCE_BARRIER Transition(
            ID3D12Resource* pResource,
            D3D12_RESOURCE_STATES stateBefore,
            D3D12_RESOURCE_STATES stateAfter,
            uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
        {
            CD3DX12_RESOURCE_BARRIER result;
            ZeroMemory(&result, sizeof(result));
            D3D12_RESOURCE_BARRIER& barrier = result;
            result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            result.Flags = flags;
            barrier.Transition.pResource = pResource;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = subresource;
            return result;
        }

        static CD3DX12_RESOURCE_BARRIER Aliasing(
            ID3D12Resource* pResourceBefore,
            ID3D12Resource* pResourceAfter)
        {
            CD3DX12_RESOURCE_BARRIER result;
            ZeroMemory(&result, sizeof(result));
            D3D12_RESOURCE_BARRIER& barrier = result;
            result.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing.pResourceBefore = pResourceBefore;
            barrier.Aliasing.pResourceAfter = pResourceAfter;
            return result;
        }

        static CD3DX12_RESOURCE_BARRIER UAV(
            ID3D12Resource* pResource)
        {
            CD3DX12_RESOURCE_BARRIER result;
            ZeroMemory(&result, sizeof(result));
            D3D12_RESOURCE_BARRIER& barrier = result;
            result.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = pResource;
            return result;
        }

        operator const D3D12_RESOURCE_BARRIER& () const { return *this; }
    };


    inline uint8_t D3D12GetFormatPlaneCount(
        ID3D12Device* pDevice,
        DXGI_FORMAT Format
    )
    {
        D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { Format };
        if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))))
        {
            return 0;
        }
        return formatInfo.PlaneCount;
    }

    inline uint32_t D3D12CalcSubresource(uint32_t MipSlice, uint32_t ArraySlice, uint32_t PlaneSlice, uint32_t MipLevels, uint32_t ArraySize)
    {
        return MipSlice + ArraySlice * MipLevels + PlaneSlice * MipLevels * ArraySize;
    }

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_RESOURCE_DESC : D3D12_RESOURCE_DESC
    {
        CD3DX12_RESOURCE_DESC()
        {
        }

        explicit CD3DX12_RESOURCE_DESC(const D3D12_RESOURCE_DESC& o) :
            D3D12_RESOURCE_DESC(o)
        {
        }

        CD3DX12_RESOURCE_DESC(
            D3D12_RESOURCE_DIMENSION dimension,
            uint64_t alignment,
            uint64_t width,
            uint32_t height,
            uint16_t depthOrArraySize,
            uint16_t mipLevels,
            DXGI_FORMAT format,
            uint32_t sampleCount,
            uint32_t sampleQuality,
            D3D12_TEXTURE_LAYOUT layout,
            D3D12_RESOURCE_FLAGS flags)
        {
            Dimension = dimension;
            Alignment = alignment;
            Width = width;
            Height = height;
            DepthOrArraySize = depthOrArraySize;
            MipLevels = mipLevels;
            Format = format;
            SampleDesc.Count = sampleCount;
            SampleDesc.Quality = sampleQuality;
            Layout = layout;
            Flags = flags;
        }

        static CD3DX12_RESOURCE_DESC Buffer(
            const D3D12_RESOURCE_ALLOCATION_INFO& resAllocInfo,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
        {
            return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_BUFFER, resAllocInfo.Alignment, resAllocInfo.SizeInBytes,
                1, 1, 1, DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags);
        }

        static CD3DX12_RESOURCE_DESC Buffer(
            uint64_t width,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            uint64_t alignment = 0)
        {
            return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_BUFFER, alignment, width, 1, 1, 1,
                DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags);
        }

        static CD3DX12_RESOURCE_DESC Tex1D(
            DXGI_FORMAT format,
            uint64_t width,
            uint16_t arraySize = 1,
            uint16_t mipLevels = 0,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            uint64_t alignment = 0)
        {
            return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE1D, alignment, width, 1, arraySize,
                mipLevels, format, 1, 0, layout, flags);
        }

        static CD3DX12_RESOURCE_DESC Tex2D(
            DXGI_FORMAT format,
            uint64_t width,
            uint32_t height,
            uint16_t arraySize = 1,
            uint16_t mipLevels = 0,
            uint32_t sampleCount = 1,
            uint32_t sampleQuality = 0,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            uint64_t alignment = 0)
        {
            return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE2D, alignment, width, height, arraySize,
                mipLevels, format, sampleCount, sampleQuality, layout, flags);
        }

        static CD3DX12_RESOURCE_DESC Tex3D(
            DXGI_FORMAT format,
            uint64_t width,
            uint32_t height,
            uint16_t depth,
            uint16_t mipLevels = 0,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            uint64_t alignment = 0)
        {
            return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE3D, alignment, width, height, depth,
                mipLevels, format, 1, 0, layout, flags);
        }

        uint16_t Depth() const
        {
            return Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? DepthOrArraySize : 1;
        }

        uint16_t ArraySize() const
        {
            return Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D ? DepthOrArraySize : 1;
        }

        UINT8 PlaneCount(_In_ ID3D12Device* pDevice) const
        {
            return D3D12GetFormatPlaneCount(pDevice, Format);
        }

        uint32_t Subresources(_In_ ID3D12Device* pDevice) const
        {
            return MipLevels * ArraySize() * PlaneCount(pDevice);
        }

        uint32_t CalcSubresource(uint32_t MipSlice, uint32_t ArraySlice, uint32_t PlaneSlice) const
        {
            return D3D12CalcSubresource(MipSlice, ArraySlice, PlaneSlice, MipLevels, ArraySize());
        }

        operator const D3D12_RESOURCE_DESC& () const { return *this; }
    };

    inline bool operator==(const D3D12_RESOURCE_DESC& l, const D3D12_RESOURCE_DESC& r)
    {
        return l.Dimension == r.Dimension &&
            l.Alignment == r.Alignment &&
            l.Width == r.Width &&
            l.Height == r.Height &&
            l.DepthOrArraySize == r.DepthOrArraySize &&
            l.MipLevels == r.MipLevels &&
            l.Format == r.Format &&
            l.SampleDesc.Count == r.SampleDesc.Count &&
            l.SampleDesc.Quality == r.SampleDesc.Quality &&
            l.Layout == r.Layout &&
            l.Flags == r.Flags;
    }

    inline bool operator!=(const D3D12_RESOURCE_DESC& l, const D3D12_RESOURCE_DESC& r)
    {
        return !(l == r);
    }

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_BOX : D3D12_BOX
    {
        CD3DX12_BOX()
        {
        }

        explicit CD3DX12_BOX(const D3D12_BOX& o) :
            D3D12_BOX(o)
        {
        }

        explicit CD3DX12_BOX(
            uint32_t Left,
            uint32_t Right)
        {
            left = Left;
            top = 0;
            front = 0;
            right = Right;
            bottom = 1;
            back = 1;
        }

        explicit CD3DX12_BOX(
            uint32_t Left,
            uint32_t Top,
            uint32_t Right,
            uint32_t Bottom)
        {
            left = Left;
            top = Top;
            front = 0;
            right = Right;
            bottom = Bottom;
            back = 1;
        }

        explicit CD3DX12_BOX(
            uint32_t Left,
            uint32_t Top,
            uint32_t Front,
            uint32_t Right,
            uint32_t Bottom,
            uint32_t Back)
        {
            left = Left;
            top = Top;
            front = Front;
            right = Right;
            bottom = Bottom;
            back = Back;
        }

        ~CD3DX12_BOX()
        {
        }

        operator const D3D12_BOX& () const { return *this; }
    };

    inline bool operator==(const D3D12_BOX& l, const D3D12_BOX& r)
    {
        return l.left == r.left && l.top == r.top && l.front == r.front &&
            l.right == r.right && l.bottom == r.bottom && l.back == r.back;
    }

    inline bool operator!=(const D3D12_BOX& l, const D3D12_BOX& r)
    {
        return !(l == r);
    }

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_TEXTURE_COPY_LOCATION : D3D12_TEXTURE_COPY_LOCATION
    {
        CD3DX12_TEXTURE_COPY_LOCATION()
        {
        }

        explicit CD3DX12_TEXTURE_COPY_LOCATION(const D3D12_TEXTURE_COPY_LOCATION& o) :
            D3D12_TEXTURE_COPY_LOCATION(o)
        {
        }

        CD3DX12_TEXTURE_COPY_LOCATION(ID3D12Resource* pRes) { pResource = pRes; }

        CD3DX12_TEXTURE_COPY_LOCATION(ID3D12Resource* pRes, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& Footprint)
        {
            pResource = pRes;
            Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            PlacedFootprint = Footprint;
        }

        CD3DX12_TEXTURE_COPY_LOCATION(ID3D12Resource* pRes, uint32_t Sub)
        {
            pResource = pRes;
            Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            SubresourceIndex = Sub;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_DESCRIPTOR_RANGE : D3D12_DESCRIPTOR_RANGE
    {
        CD3DX12_DESCRIPTOR_RANGE()
        {
        }

        explicit CD3DX12_DESCRIPTOR_RANGE(const D3D12_DESCRIPTOR_RANGE& o) :
            D3D12_DESCRIPTOR_RANGE(o)
        {
        }

        CD3DX12_DESCRIPTOR_RANGE(
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
            UINT numDescriptors,
            UINT baseShaderRegister,
            UINT registerSpace = 0,
            UINT offsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
        {
            Init(rangeType, numDescriptors, baseShaderRegister, registerSpace, offsetInDescriptorsFromTableStart);
        }

        void Init(
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
            UINT numDescriptors,
            UINT baseShaderRegister,
            UINT registerSpace = 0,
            UINT offsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
        {
            Init(*this, rangeType, numDescriptors, baseShaderRegister, registerSpace, offsetInDescriptorsFromTableStart);
        }

        static void Init(
            _Out_ D3D12_DESCRIPTOR_RANGE& range,
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
            UINT numDescriptors,
            UINT baseShaderRegister,
            UINT registerSpace = 0,
            UINT offsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
        {
            range.RangeType = rangeType;
            range.NumDescriptors = numDescriptors;
            range.BaseShaderRegister = baseShaderRegister;
            range.RegisterSpace = registerSpace;
            range.OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_DESCRIPTOR_RANGE1 : D3D12_DESCRIPTOR_RANGE1
    {
        CD3DX12_DESCRIPTOR_RANGE1()
        {
        }

        explicit CD3DX12_DESCRIPTOR_RANGE1(const D3D12_DESCRIPTOR_RANGE1& o) :
            D3D12_DESCRIPTOR_RANGE1(o)
        {
        }

        CD3DX12_DESCRIPTOR_RANGE1(
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
            UINT numDescriptors,
            UINT baseShaderRegister,
            UINT registerSpace = 0,
            D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
            UINT offsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
        {
            Init(rangeType, numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
        }

        void Init(
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
            UINT numDescriptors,
            UINT baseShaderRegister,
            UINT registerSpace = 0,
            D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
            UINT offsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
        {
            Init(*this, rangeType, numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
        }

        static void Init(
            _Out_ D3D12_DESCRIPTOR_RANGE1& range,
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
            UINT numDescriptors,
            UINT baseShaderRegister,
            UINT registerSpace = 0,
            D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
            UINT offsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
        {
            range.RangeType = rangeType;
            range.NumDescriptors = numDescriptors;
            range.BaseShaderRegister = baseShaderRegister;
            range.RegisterSpace = registerSpace;
            range.OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart;
            range.Flags = flags;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_CONSTANTS : D3D12_ROOT_CONSTANTS
    {
        CD3DX12_ROOT_CONSTANTS()
        {
        }

        explicit CD3DX12_ROOT_CONSTANTS(const D3D12_ROOT_CONSTANTS& o) :
            D3D12_ROOT_CONSTANTS(o)
        {
        }

        CD3DX12_ROOT_CONSTANTS(
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0)
        {
            Init(num32BitValues, shaderRegister, registerSpace);
        }

        void Init(
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0)
        {
            Init(*this, num32BitValues, shaderRegister, registerSpace);
        }

        static void Init(
            _Out_ D3D12_ROOT_CONSTANTS& rootConstants,
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0)
        {
            rootConstants.Num32BitValues = num32BitValues;
            rootConstants.ShaderRegister = shaderRegister;
            rootConstants.RegisterSpace = registerSpace;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_DESCRIPTOR_TABLE : D3D12_ROOT_DESCRIPTOR_TABLE
    {
        CD3DX12_ROOT_DESCRIPTOR_TABLE()
        {
        }

        explicit CD3DX12_ROOT_DESCRIPTOR_TABLE(const D3D12_ROOT_DESCRIPTOR_TABLE& o) :
            D3D12_ROOT_DESCRIPTOR_TABLE(o)
        {
        }

        CD3DX12_ROOT_DESCRIPTOR_TABLE(
            UINT numDescriptorRanges,
            _In_reads_opt_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE* _pDescriptorRanges)
        {
            Init(numDescriptorRanges, _pDescriptorRanges);
        }

        void Init(
            UINT numDescriptorRanges,
            _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE* _pDescriptorRanges)
        {
            Init(*this, numDescriptorRanges, _pDescriptorRanges);
        }

        static void Init(
            _Out_ D3D12_ROOT_DESCRIPTOR_TABLE& rootDescriptorTable,
            UINT numDescriptorRanges,
            _In_reads_opt_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE* _pDescriptorRanges)
        {
            rootDescriptorTable.NumDescriptorRanges = numDescriptorRanges;
            rootDescriptorTable.pDescriptorRanges = _pDescriptorRanges;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_DESCRIPTOR_TABLE1 : D3D12_ROOT_DESCRIPTOR_TABLE1
    {
        CD3DX12_ROOT_DESCRIPTOR_TABLE1()
        {
        }

        explicit CD3DX12_ROOT_DESCRIPTOR_TABLE1(const D3D12_ROOT_DESCRIPTOR_TABLE1& o) :
            D3D12_ROOT_DESCRIPTOR_TABLE1(o)
        {
        }

        CD3DX12_ROOT_DESCRIPTOR_TABLE1(
            UINT numDescriptorRanges,
            _In_reads_opt_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1* _pDescriptorRanges)
        {
            Init(numDescriptorRanges, _pDescriptorRanges);
        }

        void Init(
            UINT numDescriptorRanges,
            _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1* _pDescriptorRanges)
        {
            Init(*this, numDescriptorRanges, _pDescriptorRanges);
        }

        static void Init(
            _Out_ D3D12_ROOT_DESCRIPTOR_TABLE1& rootDescriptorTable,
            UINT numDescriptorRanges,
            _In_reads_opt_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1* _pDescriptorRanges)
        {
            rootDescriptorTable.NumDescriptorRanges = numDescriptorRanges;
            rootDescriptorTable.pDescriptorRanges = _pDescriptorRanges;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_DESCRIPTOR : D3D12_ROOT_DESCRIPTOR
    {
        CD3DX12_ROOT_DESCRIPTOR()
        {
        }

        explicit CD3DX12_ROOT_DESCRIPTOR(const D3D12_ROOT_DESCRIPTOR& o) :
            D3D12_ROOT_DESCRIPTOR(o)
        {
        }

        CD3DX12_ROOT_DESCRIPTOR(
            UINT shaderRegister,
            UINT registerSpace = 0)
        {
            Init(shaderRegister, registerSpace);
        }

        void Init(
            UINT shaderRegister,
            UINT registerSpace = 0)
        {
            Init(*this, shaderRegister, registerSpace);
        }

        static void Init(_Out_ D3D12_ROOT_DESCRIPTOR& table, UINT shaderRegister, UINT registerSpace = 0)
        {
            table.ShaderRegister = shaderRegister;
            table.RegisterSpace = registerSpace;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_DESCRIPTOR1 : D3D12_ROOT_DESCRIPTOR1
    {
        CD3DX12_ROOT_DESCRIPTOR1()
        {
        }

        explicit CD3DX12_ROOT_DESCRIPTOR1(const D3D12_ROOT_DESCRIPTOR1& o) :
            D3D12_ROOT_DESCRIPTOR1(o)
        {
        }

        CD3DX12_ROOT_DESCRIPTOR1(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            Init(shaderRegister, registerSpace, Flags);
        }

        void Init(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            Init(*this, shaderRegister, registerSpace, Flags);
        }

        static void Init(_Out_ D3D12_ROOT_DESCRIPTOR1& table, UINT shaderRegister, UINT registerSpace = 0, D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            table.ShaderRegister = shaderRegister;
            table.RegisterSpace = registerSpace;
            table.Flags = Flags;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_PARAMETER : D3D12_ROOT_PARAMETER
    {
        CD3DX12_ROOT_PARAMETER()
        {
        }

        explicit CD3DX12_ROOT_PARAMETER(const D3D12_ROOT_PARAMETER& o) :
            D3D12_ROOT_PARAMETER(o)
        {
        }

        static void InitAsDescriptorTable(
            _Out_ D3D12_ROOT_PARAMETER& rootParam,
            UINT numDescriptorRanges,
            _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE* pDescriptorRanges,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR_TABLE::Init(rootParam.DescriptorTable, numDescriptorRanges, pDescriptorRanges);
        }

        static void InitAsConstants(
            _Out_ D3D12_ROOT_PARAMETER& rootParam,
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_CONSTANTS::Init(rootParam.Constants, num32BitValues, shaderRegister, registerSpace);
        }

        static void InitAsConstantBufferView(
            _Out_ D3D12_ROOT_PARAMETER& rootParam,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR::Init(rootParam.Descriptor, shaderRegister, registerSpace);
        }

        static void InitAsShaderResourceView(
            _Out_ D3D12_ROOT_PARAMETER& rootParam,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR::Init(rootParam.Descriptor, shaderRegister, registerSpace);
        }

        static void InitAsUnorderedAccessView(
            _Out_ D3D12_ROOT_PARAMETER& rootParam,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR::Init(rootParam.Descriptor, shaderRegister, registerSpace);
        }

        void InitAsDescriptorTable(
            UINT numDescriptorRanges,
            _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE* pDescriptorRanges,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsDescriptorTable(*this, numDescriptorRanges, pDescriptorRanges, visibility);
        }

        void InitAsConstants(
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsConstants(*this, num32BitValues, shaderRegister, registerSpace, visibility);
        }

        void InitAsConstantBufferView(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsConstantBufferView(*this, shaderRegister, registerSpace, visibility);
        }

        void InitAsShaderResourceView(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsShaderResourceView(*this, shaderRegister, registerSpace, visibility);
        }

        void InitAsUnorderedAccessView(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsUnorderedAccessView(*this, shaderRegister, registerSpace, visibility);
        }
    };


    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_PARAMETER1 : D3D12_ROOT_PARAMETER1
    {
        CD3DX12_ROOT_PARAMETER1()
        {
        }

        explicit CD3DX12_ROOT_PARAMETER1(const D3D12_ROOT_PARAMETER1& o) :
            D3D12_ROOT_PARAMETER1(o)
        {
        }

        static void InitAsDescriptorTable(
            _Out_ D3D12_ROOT_PARAMETER1& rootParam,
            UINT numDescriptorRanges,
            _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1* pDescriptorRanges,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR_TABLE1::Init(rootParam.DescriptorTable, numDescriptorRanges, pDescriptorRanges);
        }

        static void InitAsConstants(
            _Out_ D3D12_ROOT_PARAMETER1& rootParam,
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_CONSTANTS::Init(rootParam.Constants, num32BitValues, shaderRegister, registerSpace);
        }

        static void InitAsConstantBufferView(
            _Out_ D3D12_ROOT_PARAMETER1& rootParam,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
            D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR1::Init(rootParam.Descriptor, shaderRegister, registerSpace, Flags);
        }

        static void InitAsShaderResourceView(
            _Out_ D3D12_ROOT_PARAMETER1& rootParam,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
            D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR1::Init(rootParam.Descriptor, shaderRegister, registerSpace, Flags);
        }

        static void InitAsUnorderedAccessView(
            _Out_ D3D12_ROOT_PARAMETER1& rootParam,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL, D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            rootParam.ShaderVisibility = visibility;
            CD3DX12_ROOT_DESCRIPTOR1::Init(rootParam.Descriptor, shaderRegister, registerSpace, Flags);
        }

        void InitAsDescriptorTable(
            UINT numDescriptorRanges,
            _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1* pDescriptorRanges,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsDescriptorTable(*this, numDescriptorRanges, pDescriptorRanges, visibility);
        }

        void InitAsConstants(
            UINT num32BitValues,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
        {
            InitAsConstants(*this, num32BitValues, shaderRegister, registerSpace, visibility);
        }

        void InitAsConstantBufferView(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
            D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            InitAsConstantBufferView(*this, shaderRegister, registerSpace, visibility, Flags);
        }

        void InitAsShaderResourceView(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL, D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            InitAsShaderResourceView(*this, shaderRegister, registerSpace, visibility, Flags);
        }

        void InitAsUnorderedAccessView(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL, D3D12_ROOT_DESCRIPTOR_FLAGS Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
        {
            InitAsUnorderedAccessView(*this, shaderRegister, registerSpace, visibility, Flags);
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_STATIC_SAMPLER_DESC : D3D12_STATIC_SAMPLER_DESC
    {
        CD3DX12_STATIC_SAMPLER_DESC()
        {
        }

        explicit CD3DX12_STATIC_SAMPLER_DESC(const D3D12_STATIC_SAMPLER_DESC& o) :
            D3D12_STATIC_SAMPLER_DESC(o)
        {
        }

        CD3DX12_STATIC_SAMPLER_DESC(
            UINT shaderRegister,
            D3D12_FILTER filter = D3D12_FILTER_ANISOTROPIC,
            D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            FLOAT mipLODBias = 0,
            UINT maxAnisotropy = 16,
            D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
            FLOAT minLOD = 0.f,
            FLOAT maxLOD = D3D12_FLOAT32_MAX,
            D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
            UINT registerSpace = 0)
        {
            Init(
                shaderRegister,
                filter,
                addressU,
                addressV,
                addressW,
                mipLODBias,
                maxAnisotropy,
                comparisonFunc,
                borderColor,
                minLOD,
                maxLOD,
                shaderVisibility,
                registerSpace);
        }

        static void Init(
            _Out_ D3D12_STATIC_SAMPLER_DESC& samplerDesc,
            UINT shaderRegister,
            D3D12_FILTER filter = D3D12_FILTER_ANISOTROPIC,
            D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            FLOAT mipLODBias = 0,
            UINT maxAnisotropy = 16,
            D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
            FLOAT minLOD = 0.f,
            FLOAT maxLOD = D3D12_FLOAT32_MAX,
            D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
            UINT registerSpace = 0)
        {
            samplerDesc.ShaderRegister = shaderRegister;
            samplerDesc.Filter = filter;
            samplerDesc.AddressU = addressU;
            samplerDesc.AddressV = addressV;
            samplerDesc.AddressW = addressW;
            samplerDesc.MipLODBias = mipLODBias;
            samplerDesc.MaxAnisotropy = maxAnisotropy;
            samplerDesc.ComparisonFunc = comparisonFunc;
            samplerDesc.BorderColor = borderColor;
            samplerDesc.MinLOD = minLOD;
            samplerDesc.MaxLOD = maxLOD;
            samplerDesc.ShaderVisibility = shaderVisibility;
            samplerDesc.RegisterSpace = registerSpace;
        }

        void Init(
            UINT shaderRegister,
            D3D12_FILTER filter = D3D12_FILTER_ANISOTROPIC,
            D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            FLOAT mipLODBias = 0,
            UINT maxAnisotropy = 16,
            D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
            FLOAT minLOD = 0.f,
            FLOAT maxLOD = D3D12_FLOAT32_MAX,
            D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
            UINT registerSpace = 0)
        {
            Init(
                *this,
                shaderRegister,
                filter,
                addressU,
                addressV,
                addressW,
                mipLODBias,
                maxAnisotropy,
                comparisonFunc,
                borderColor,
                minLOD,
                maxLOD,
                shaderVisibility,
                registerSpace);
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_SIGNATURE_DESC : D3D12_ROOT_SIGNATURE_DESC
    {
        CD3DX12_ROOT_SIGNATURE_DESC()
        {
        }

        explicit CD3DX12_ROOT_SIGNATURE_DESC(const D3D12_ROOT_SIGNATURE_DESC& o) :
            D3D12_ROOT_SIGNATURE_DESC(o)
        {
        }

        CD3DX12_ROOT_SIGNATURE_DESC(
            UINT numParameters,
            _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER* _pParameters,
            UINT numStaticSamplers = 0,
            _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
        {
            Init(numParameters, _pParameters, numStaticSamplers, _pStaticSamplers, flags);
        }

        CD3DX12_ROOT_SIGNATURE_DESC(CD3DX12_DEFAULT)
        {
            Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
        }

        void Init(
            UINT numParameters,
            _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER* _pParameters,
            UINT numStaticSamplers = 0,
            _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
        {
            Init(*this, numParameters, _pParameters, numStaticSamplers, _pStaticSamplers, flags);
        }

        static void Init(
            _Out_ D3D12_ROOT_SIGNATURE_DESC& desc,
            UINT numParameters,
            _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER* _pParameters,
            UINT numStaticSamplers = 0,
            _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
        {
            desc.NumParameters = numParameters;
            desc.pParameters = _pParameters;
            desc.NumStaticSamplers = numStaticSamplers;
            desc.pStaticSamplers = _pStaticSamplers;
            desc.Flags = flags;
        }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_ROOT_SIGNATURE_DESC1 : D3D12_ROOT_SIGNATURE_DESC1
    {
        CD3DX12_ROOT_SIGNATURE_DESC1()
        {
        }

        explicit CD3DX12_ROOT_SIGNATURE_DESC1(const D3D12_ROOT_SIGNATURE_DESC1& o) :
            D3D12_ROOT_SIGNATURE_DESC1(o)
        {
        }

        CD3DX12_ROOT_SIGNATURE_DESC1(
            UINT numParameters,
            _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER1* _pParameters,
            UINT numStaticSamplers = 0,
            _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
        {
            Init(numParameters, _pParameters, numStaticSamplers, _pStaticSamplers, flags);
        }

        CD3DX12_ROOT_SIGNATURE_DESC1(CD3DX12_DEFAULT)
        {
            Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
        }

        void Init(
            UINT numParameters,
            _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER1* _pParameters,
            UINT numStaticSamplers = 0,
            _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
        {
            Init(*this, numParameters, _pParameters, numStaticSamplers, _pStaticSamplers, flags);
        }

        static void Init(
            _Out_ D3D12_ROOT_SIGNATURE_DESC1& desc,
            UINT numParameters,
            _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER1* _pParameters,
            UINT numStaticSamplers = 0,
            _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
        {
            desc.NumParameters = numParameters;
            desc.pParameters = _pParameters;
            desc.NumStaticSamplers = numStaticSamplers;
            desc.pStaticSamplers = _pStaticSamplers;
            desc.Flags = flags;
        }
    };


    //------------------------------------------------------------------------------------------------
    struct CD3DX12_RASTERIZER_DESC : D3D12_RASTERIZER_DESC
    {
        CD3DX12_RASTERIZER_DESC()
        {
        }

        explicit CD3DX12_RASTERIZER_DESC(const D3D12_RASTERIZER_DESC& o) :
            D3D12_RASTERIZER_DESC(o)
        {
        }

        explicit CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT)
        {
            FillMode = D3D12_FILL_MODE_SOLID;
            CullMode = D3D12_CULL_MODE_BACK;
            FrontCounterClockwise = FALSE;
            DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            DepthClipEnable = TRUE;
            MultisampleEnable = FALSE;
            AntialiasedLineEnable = FALSE;
            ForcedSampleCount = 0;
            ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        }

        explicit CD3DX12_RASTERIZER_DESC(
            D3D12_FILL_MODE fillMode,
            D3D12_CULL_MODE cullMode,
            BOOL frontCounterClockwise,
            INT depthBias,
            FLOAT depthBiasClamp,
            FLOAT slopeScaledDepthBias,
            BOOL depthClipEnable,
            BOOL multisampleEnable,
            BOOL antialiasedLineEnable,
            UINT forcedSampleCount,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE conservativeRaster)
        {
            FillMode = fillMode;
            CullMode = cullMode;
            FrontCounterClockwise = frontCounterClockwise;
            DepthBias = depthBias;
            DepthBiasClamp = depthBiasClamp;
            SlopeScaledDepthBias = slopeScaledDepthBias;
            DepthClipEnable = depthClipEnable;
            MultisampleEnable = multisampleEnable;
            AntialiasedLineEnable = antialiasedLineEnable;
            ForcedSampleCount = forcedSampleCount;
            ConservativeRaster = conservativeRaster;
        }

        ~CD3DX12_RASTERIZER_DESC()
        {
        }

        operator const D3D12_RASTERIZER_DESC& () const { return *this; }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_BLEND_DESC : D3D12_BLEND_DESC
    {
        CD3DX12_BLEND_DESC()
        {
        }

        explicit CD3DX12_BLEND_DESC(const D3D12_BLEND_DESC& o) :
            D3D12_BLEND_DESC(o)
        {
        }

        explicit CD3DX12_BLEND_DESC(CD3DX12_DEFAULT)
        {
            AlphaToCoverageEnable = FALSE;
            IndependentBlendEnable = FALSE;
            constexpr D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
            {
                FALSE,FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP,
                D3D12_COLOR_WRITE_ENABLE_ALL,
            };
            for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
                RenderTarget[i] = defaultRenderTargetBlendDesc;
        }

        ~CD3DX12_BLEND_DESC()
        {
        }

        operator const D3D12_BLEND_DESC& () const { return *this; }
    };

    //------------------------------------------------------------------------------------------------
    struct CD3DX12_DEPTH_STENCIL_DESC : D3D12_DEPTH_STENCIL_DESC
    {
        CD3DX12_DEPTH_STENCIL_DESC()
        {
        }

        explicit CD3DX12_DEPTH_STENCIL_DESC(const D3D12_DEPTH_STENCIL_DESC& o) :
            D3D12_DEPTH_STENCIL_DESC(o)
        {
        }

        explicit CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT)
        {
            DepthEnable = TRUE;
            DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            DepthFunc = D3D12_COMPARISON_FUNC_LESS;
            StencilEnable = FALSE;
            StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            constexpr D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
            FrontFace = defaultStencilOp;
            BackFace = defaultStencilOp;
        }

        explicit CD3DX12_DEPTH_STENCIL_DESC(
            BOOL depthEnable,
            D3D12_DEPTH_WRITE_MASK depthWriteMask,
            D3D12_COMPARISON_FUNC depthFunc,
            BOOL stencilEnable,
            UINT8 stencilReadMask,
            UINT8 stencilWriteMask,
            D3D12_STENCIL_OP frontStencilFailOp,
            D3D12_STENCIL_OP frontStencilDepthFailOp,
            D3D12_STENCIL_OP frontStencilPassOp,
            D3D12_COMPARISON_FUNC frontStencilFunc,
            D3D12_STENCIL_OP backStencilFailOp,
            D3D12_STENCIL_OP backStencilDepthFailOp,
            D3D12_STENCIL_OP backStencilPassOp,
            D3D12_COMPARISON_FUNC backStencilFunc)
        {
            DepthEnable = depthEnable;
            DepthWriteMask = depthWriteMask;
            DepthFunc = depthFunc;
            StencilEnable = stencilEnable;
            StencilReadMask = stencilReadMask;
            StencilWriteMask = stencilWriteMask;
            FrontFace.StencilFailOp = frontStencilFailOp;
            FrontFace.StencilDepthFailOp = frontStencilDepthFailOp;
            FrontFace.StencilPassOp = frontStencilPassOp;
            FrontFace.StencilFunc = frontStencilFunc;
            BackFace.StencilFailOp = backStencilFailOp;
            BackFace.StencilDepthFailOp = backStencilDepthFailOp;
            BackFace.StencilPassOp = backStencilPassOp;
            BackFace.StencilFunc = backStencilFunc;
        }

        ~CD3DX12_DEPTH_STENCIL_DESC()
        {
        }

        operator const D3D12_DEPTH_STENCIL_DESC& () const { return *this; }
    };


    enumTextureFormat GetDxToTexFormat(DXGI_FORMAT eFormat);
    DXGI_FORMAT GetTexToDxFormat(enumTextureFormat eFormat, bool bSRGB = false);
    DXGI_FORMAT GetTexFormatToSRVFormat(DXGI_FORMAT eFormat);
    enumTextureFormat GetDxToTexFormat(DXGI_FORMAT eFormat, BOOL& bDepth, BOOL& bStencil);
    DXGI_FORMAT GetTexToDxFormat(enumTextureFormat eFormat, BOOL& bDepth, BOOL& bStencil);
    DXGI_FORMAT GetTexToDxFormat(enumTextureFormat eFormat, bool& bDepth, bool& bStencil);
    D3D12_RESOURCE_STATES To_D3D12_RESOURCE_STATES(enumImageLayout layout);

    /**
     * 将glvk中的贴图usage转换为dx12中的贴图usage
     * @param usage
     * @return
     */
    D3D12_RESOURCE_FLAGS To_D3D12_RESOURCE_FLAGS(TextureUsageFlags uUsageFlags, enumTextureFormat eFormat);

    struct DX12SubTexCopyInfo
    {
        uint32_t xLength = 0;
        uint32_t yLength = 0;
        uint32_t zLength = 0;
        uint32_t xOffset = 0;
        uint32_t yOffset = 0;
        uint32_t zOffset = 0;
        uint32_t pixelSize = 0;
    };

    void MemcpyAllResource(
        _In_ const D3D12_MEMCPY_DEST* pDest,
        _In_ const D3D12_SUBRESOURCE_DATA* pSrc,
        SIZE_T RowSizeInBytes,
        uint32_t uNumRows,
        uint32_t uNumSlices,
        BOOL bAllChunck
    );

    void MemcpySubresource(
        _In_ const D3D12_MEMCPY_DEST* pDest,
        _In_ const D3D12_SUBRESOURCE_DATA* pSrc,
        BOOL bAllChunck,
        const DX12SubTexCopyInfo& subInfo
    );

    size_t BitsPerPixel(DXGI_FORMAT fmt);

    void GetSurfaceInfo(
        size_t width,
        size_t height,
        DXGI_FORMAT fmt,
        _Out_ size_t* outNumBytes,
        _Out_ size_t* outRowBytes,
        _Out_ size_t* outNumRows);

    HRESULT FillInitData(
        size_t width,
        size_t height,
        size_t depth,
        size_t mipCount,
        size_t arraySize,
        DXGI_FORMAT format,
        size_t maxsize,
        size_t bitSize,
        const uint8_t* bitData,
        _Out_ size_t& twidth,
        _Out_ size_t& theight,
        _Out_ size_t& tdepth,
        _Out_ size_t& skipMip,
        _Out_ D3D12_SUBRESOURCE_DATA* initDataArray
    );

    uint64_t GetRequiredIntermediateSize(
        ID3D12Device* pD3dDevice,
        ID3D12Resource* pDestinationResource,
        uint32_t uFirstSubresource,
        uint32_t uNumSubresources);

    uint64_t UpdateSubresources(
        ID3D12Device* pD3dDevice,
        ID3D12GraphicsCommandList* pCmdList,
        ID3D12Resource* pDestinationResource,
        ID3D12Resource* pIntermediate,
        uint64_t IntermediateOffset,
        uint32_t FirstSubresource,
        uint32_t NumSubresources,
        const D3D12_SUBRESOURCE_DATA* pSrcData,
        uint32_t xoffset,
        uint32_t yoffset,
        uint32_t zoffset
    );

    uint32_t CalcConstantBufferByteSize(uint32_t byteSize);

    /**
     * 在cpu端对于资源的操作一般只需要一个cpu的句柄，不提供GPU的句柄
     */
    struct D3D12Descriptor
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        operator bool() const { return cpuHandle.ptr != 0; }
    };

    enum class BindlessConfiguration
    {
        AllShader,
        RayTracingShader
    };

    enum class BindlessHeapType
    {
        Standard,
        Sampler,
        RenderTarget,
        DepthStencil,
        InValid
    };

    struct BindlessDescriptor
    {
        BindlessHeapType Type = BindlessHeapType::InValid;
        uint32_t Index = MAXUINT32;
        bool IsValid() { return Index != MAXUINT32 && Type != BindlessHeapType::InValid; }
    };

    struct KD3DX12_RESOURCE_DESC1 : public D3D12_RESOURCE_DESC1
    {
        static constexpr int kRemainingTextureSize = 0xffffffff;

        KD3DX12_RESOURCE_DESC1();
        explicit KD3DX12_RESOURCE_DESC1(const D3D12_RESOURCE_DESC1& o) noexcept;

        explicit KD3DX12_RESOURCE_DESC1(const D3D12_RESOURCE_DESC& o) noexcept;

        KD3DX12_RESOURCE_DESC1(
            D3D12_RESOURCE_DIMENSION dimension,
            UINT64 alignment,
            UINT64 width,
            UINT height,
            UINT16 depthOrArraySize,
            UINT16 mipLevels,
            DXGI_FORMAT format,
            UINT sampleCount,
            UINT sampleQuality,
            D3D12_TEXTURE_LAYOUT layout,
            D3D12_RESOURCE_FLAGS flags,
            UINT samplerFeedbackMipRegionWidth = 0,
            UINT samplerFeedbackMipRegionHeight = 0,
            UINT samplerFeedbackMipRegionDepth = 0) noexcept;

        static KD3DX12_RESOURCE_DESC1 Buffer(
            const D3D12_RESOURCE_ALLOCATION_INFO& resAllocInfo,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) noexcept;

        static KD3DX12_RESOURCE_DESC1 Buffer(
            UINT64 width,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            UINT64 alignment = 0) noexcept;

        static KD3DX12_RESOURCE_DESC1 Tex1D(
            DXGI_FORMAT format,
            UINT64 width,
            UINT16 arraySize = 1,
            UINT16 mipLevels = 0,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            UINT64 alignment = 0) noexcept;

        static KD3DX12_RESOURCE_DESC1 Tex2D(
            DXGI_FORMAT format,
            UINT64 width,
            UINT height,
            UINT16 arraySize = 1,
            UINT16 mipLevels = 0,
            UINT sampleCount = 1,
            UINT sampleQuality = 0,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            UINT64 alignment = 0,
            UINT samplerFeedbackMipRegionWidth = 0,
            UINT samplerFeedbackMipRegionHeight = 0,
            UINT samplerFeedbackMipRegionDepth = 0) noexcept;

        static KD3DX12_RESOURCE_DESC1 Tex3D(
            DXGI_FORMAT format,
            UINT64 width,
            UINT height,
            UINT16 depth,
            UINT16 mipLevels = 0,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            UINT64 alignment = 0) noexcept;

        UINT16 Depth() const noexcept;

        UINT16 ArraySize() const noexcept;

        UINT8 PlaneCount(_In_ ID3D12Device* pDevice) const noexcept;

        UINT Subresources(_In_ ID3D12Device* pDevice) const noexcept;

        UINT CalcSubresource(UINT MipSlice, UINT ArraySlice, UINT PlaneSlice) const noexcept;

        D3D12_RESOURCE_DESC ToDesc() const noexcept;
    };

    bool operator==(const D3D12_RESOURCE_DESC1& l, const D3D12_RESOURCE_DESC1& r) noexcept;

    bool operator!=(const D3D12_RESOURCE_DESC1& l, const D3D12_RESOURCE_DESC1& r) noexcept;

    D3D12_RESOURCE_DIMENSION GetTexDimension(uint32_t uDepth, uint32_t uHeight);

    //uint32_t GetSubresourceIndex(uint32_t mipSlice, uint32_t arraySlice, uint32_t planeSlice, uint32_t arraySize, uint32_t mipLevels);

    //uint32_t GetSubresourceIndex(SubresourceRange subRange);

    D3D12_RESOURCE_DIMENSION GetGFXDimensionToDX(ResourceViewDimension desc, bool bCube = false);

    ResourceViewDimension GetDXDimensionToGfx(D3D12_RESOURCE_DIMENSION desc, bool bCube = false);

    struct KD3DX12_RESOURCE_BARRIER : D3D12_RESOURCE_BARRIER
    {
        KD3DX12_RESOURCE_BARRIER()
        {
        }

        KD3DX12_RESOURCE_BARRIER(const D3D12_RESOURCE_BARRIER& o) :
            D3D12_RESOURCE_BARRIER(o)
        {
        }

        static KD3DX12_RESOURCE_BARRIER TransitionBarrier(
            ID3D12Resource* pResource,
            D3D12_RESOURCE_STATES stateBefore,
            D3D12_RESOURCE_STATES stateAfter,
            uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
        {
            KD3DX12_RESOURCE_BARRIER result;
            ZeroMemory(&result, sizeof(result));
            D3D12_RESOURCE_BARRIER& barrier = result;
            result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            result.Flags = flags;
            barrier.Transition.pResource = pResource;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = subresource;
            return result;
        }

        /**
         * 这个基本不会使用
         * @param pResourceBefore
         * @param pResourceAfter
         * @return
         */
        static KD3DX12_RESOURCE_BARRIER AliasingBarrier(
            ID3D12Resource* pResourceBefore,
            ID3D12Resource* pResourceAfter)
        {
            KD3DX12_RESOURCE_BARRIER result;
            ZeroMemory(&result, sizeof(result));
            D3D12_RESOURCE_BARRIER& barrier = result;
            result.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing.pResourceBefore = pResourceBefore;
            barrier.Aliasing.pResourceAfter = pResourceAfter;
            return result;
        }

        static KD3DX12_RESOURCE_BARRIER UAVBarrier(
            ID3D12Resource* pResource)
        {
            KD3DX12_RESOURCE_BARRIER result;
            ZeroMemory(&result, sizeof(result));
            D3D12_RESOURCE_BARRIER& barrier = result;
            result.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = pResource;
            return result;
        }

        operator const D3D12_RESOURCE_BARRIER& () const { return *this; }
    };


    D3D12_BLEND GetDxBlendFactor(enumBlendType blendType);

    D3D12_COMPARISON_FUNC GetDxDepthCompareOp(enumDepthType cp);

    D3D12_COMPARISON_FUNC GetDxStencilCompareOp(enumStencilType st);

    D3D12_STENCIL_OP GetDxStencilOp(enumStencilOpType p);

    D3D12_RESOURCE_STATES GetDxResourceLayout(BufferUsageFlags usage);

    D3D12_FILTER_TYPE TranslateFilterMode(enumSamplerFilter mode);

    D3D12_FILTER_TYPE TranslateMipFilterMode(enumMipMapMode mode);

    D3D12_FILTER_TYPE TranslateMipMapFilter(enumMipMapMode op);

    D3D12_TEXTURE_ADDRESS_MODE TranslateAddressingMode(enumSamplerAddressMode mode);

    D3D12_COMPARISON_FUNC TranslateComparisonFunc(enumSamplerCompareFunc func);

    D3D12_FILTER_REDUCTION_TYPE TranslateFilterReduction(enumTextureReductionOp op);

    std::array<float, 4> TranslateBorderColor(enumBorderColor color);
};
#endif
