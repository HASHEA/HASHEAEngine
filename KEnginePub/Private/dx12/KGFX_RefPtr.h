#pragma once
#include <cassert>
#include <cstddef>

namespace gfx
{
    class ScopedAllocation
    {
    public:
        using ThisType = ScopedAllocation;
        ScopedAllocation();
        ~ScopedAllocation();

        ScopedAllocation(const ThisType& rhs) = delete;
        void operator=(const ThisType& rhs) = delete;

        void* Allocate(size_t size);

        void* AllocateTerminated(size_t size);

        void Deallocate();

        void Reallocate(size_t capacity);

        void* Detach();

        void Attach(void* data, size_t size);

        void* Set(const void* data, size_t size);

        void* GetData() const;

        size_t GetSizeInBytes() const;

        size_t GetCapacityInBytes() const;

        void SetSizeInBytes(size_t size);

        void Swap(ThisType& rhs) noexcept;

        bool IsTerminated() const;

        void* m_data = nullptr;
        size_t m_sizeInBytes = {};
        size_t m_capacityInBytes = {};
    };

    struct KGFX_IBlob
    {
        virtual ~KGFX_IBlob() = default;
        virtual __declspec(nothrow) const void* __stdcall GetBufferPointer() = 0;
        virtual __declspec(nothrow) size_t __stdcall GetBufferSize() = 0;
    };

 
    class RawBlob final : public KGFX_IBlob
    {
    public:
        ~RawBlob() override;
     
        __declspec(nothrow) const void* __stdcall GetBufferPointer() override;

        __declspec(nothrow) size_t __stdcall GetBufferSize() override;

        static KGFX_IBlob* MoveCreate(ScopedAllocation& alloc);

        static KGFX_IBlob* Create(const void* inData, size_t size);

    private:
        RawBlob(const void* data, size_t size);

        RawBlob();

        ScopedAllocation m_data;
    };

    /**
     * 使用这个智能指针来管理<不含引用计数>的对象的生命周期
     * @tparam T 
     */
    template<typename T>
    class RefPtr
    {
    public:
        RefPtr();
        RefPtr(std::nullptr_t);
        explicit RefPtr(T* ptr);

        RefPtr(const RefPtr& rhs) = delete;
        RefPtr operator=(RefPtr& rhs) = delete;

        RefPtr(RefPtr&& rhs)noexcept;
        RefPtr& operator=(RefPtr&& rhs)noexcept;

        ~RefPtr();

        operator T* () const;

        T** operator&();

        T& operator*();

        T* operator->() const;

        T* Get() const;

        T* Detach();

        void Attch(T* invalue)
        {
            if(m_ptr)
            {
                delete m_ptr;
                m_ptr = nullptr;
            }
            m_ptr = invalue;
        }

    private:
        T* m_ptr;
    };

    template <typename T>
    RefPtr<T>::RefPtr(): m_ptr(nullptr)
    {
    }

    template <typename T>
    RefPtr<T>::RefPtr(std::nullptr_t): m_ptr(nullptr)
    {
    }

    template <typename T>
    RefPtr<T>::RefPtr(T* ptr): m_ptr(ptr)
    {
    }

    template <typename T>
    RefPtr<T>::RefPtr(RefPtr&& rhs) noexcept
    {
        m_ptr = rhs.m_ptr;
        rhs.m_ptr = nullptr;
    }

    template <typename T>
    RefPtr<T>& RefPtr<T>::operator=(RefPtr&& rhs) noexcept
    {
        if (m_ptr)
        {
            delete m_ptr;
            m_ptr = nullptr;
        }

        m_ptr = rhs.m_ptr;
        rhs.m_ptr = nullptr;
        return *this;
    }

    template <typename T>
    RefPtr<T>::~RefPtr()
    {
        if (m_ptr)
        {
            delete m_ptr;
            m_ptr = nullptr;
        }
    }

    template <typename T>
    RefPtr<T>::operator T*() const
    { return m_ptr; }

    template <typename T>
    T** RefPtr<T>::operator&()
    {
        /// 确保指针为空
        assert(m_ptr == nullptr);
        return &m_ptr;
    }

    template <typename T>
    T& RefPtr<T>::operator*()
    { return *m_ptr; }

    template <typename T>
    T* RefPtr<T>::operator->() const
    { return m_ptr; }

    template <typename T>
    T* RefPtr<T>::Get() const
    { return m_ptr; }

    template <typename T>
    T* RefPtr<T>::Detach()
    {
        T* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }
}
