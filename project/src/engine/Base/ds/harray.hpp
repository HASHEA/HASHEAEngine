#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/hmemory.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
namespace AshEngine
{
    template <typename T>
    class Array {

    public:
        Array();
        ~Array();

        auto                        init(Allocator* allocator, uint32_t initial_capacity, uint32_t initial_size = 0) -> bool;
        auto                        shutdown() -> bool;

        auto                        push_back(const T& element) -> bool;
        auto push_use() -> T&;                 // Grow the size and return T to be filled.

        auto                        pop() -> bool;
        auto                        delete_swap(uint32_t index) -> bool;

        T& operator[](uint32_t index);
        const T& operator[](uint32_t index) const;

        auto                        clear() -> bool;
        auto                        set_size(uint32_t new_size) -> bool;
        auto                        set_capacity(uint32_t new_capacity) -> bool;

        T& back();
        const T& back() const;

        T& front();
        const T& front() const;

        auto                         size_in_bytes() const -> uint32_t;
        auto                         capacity_in_bytes() const -> uint32_t;
        auto size() const -> uint32_t { return m_uSize; }
		auto capacity() const -> uint32_t { return m_uCapacity; }

        auto set_capacity_no_grow(uint32_t capacity) { m_uCapacity  = capacity};
    private:
        uint32_t                         m_uSize = 0;       // Occupied size
        uint32_t                         m_uCapacity = 0;   // Allocated capacity
    private:
        auto                        grow(uint32_t new_capacity) -> bool;

    public:
        Allocator* m_pAllocator = nullptr;
        T* m_pData = nullptr;
        
    };


    // ArrayView //////////////////////////////////////////////////////////

    // View over a contiguous memory block.
    template <typename T>
    class ArrayView 
    {
    public:
        ArrayView()
        {
            m_pData = nullptr;
            m_uSize = 0;
        };
        ArrayView(T* data, uint32_t size);

        auto set(T* data, uint32_t size) -> void;

        T& operator[](uint32_t index);
        const T& operator[](uint32_t index) const;

        T* m_pData = nullptr;
        uint32_t                         m_uSize = 0;
    }; // struct ArrayView


    template<typename T>
    inline Array<T>::Array() {
        
    }

    template<typename T>
    inline Array<T>::~Array() {
        shutdown();
    }

    template<typename T>
    inline auto Array<T>::init(Allocator* allocator_, uint32_t initial_capacity, uint32_t initial_size)->bool {
        m_pData = nullptr;
        m_uSize = initial_size;
        m_uCapacity = 0;
        m_pAllocator = allocator_;
        bool ret = true;
        if (initial_capacity > 0) {
            ret = grow(initial_capacity);
        }
        if (initial_size > 0)
        {
            //init memory
			if constexpr (std::is_trivial<T>::value) {
				memset(m_pData, 0, static_cast<size_t>(initial_size * sizeof(T)));
			}
			else {
				for (uint32_t i = 0; i < initial_size; ++i) {
					new (&m_pData[i]) T();
				}
			}
        } 
        return ret;
    }

    template<typename T>
    inline auto Array<T>::shutdown() -> bool {
        if (m_pData) {
			if constexpr (!std::is_trivially_destructible<T>::value) {
				for (uint32_t i = 0; i < m_uSize; ++i) {
					m_pData[i].~T();
				}
			}
            Ash_Free(m_pAllocator, m_pData);
        }
        m_pData = nullptr;
        m_uSize = m_uCapacity = 0;
        return true;
    }

    template<typename T>
    inline auto Array<T>::push_back(const T& element) -> bool {
        bool ret = true;
        if (m_uSize >= m_uCapacity) {
            ret = grow(m_uCapacity + 1);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
        if constexpr (std::is_trivially_copy_assignable<T>::value)
        {
            m_pData[m_uSize++] = element;
        }
        else
        {
            new (&m_pData[m_uSize++]) T(element); // Construct element using copy constructor
        }
       
        
        return true;
    }
    // push and return back
    template<typename T>
    inline auto Array<T>::push_use() -> T&{
        bool ret = true;

        if (m_uSize >= m_uCapacity) {
            ret = grow(m_uCapacity + 1);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
		if constexpr (std::is_trivially_default_constructible<T>::value) {
			m_pData[m_uSize] = T(); 
		}
		else {
			new (&m_pData[m_uSize]) T();
		}
		return m_pData[m_uSize++];
    }

    template<typename T>
    inline auto Array<T>::pop() ->bool{
        H_ASSERT(m_uSize > 0);
        --m_uSize;
		if constexpr (!std::is_trivially_destructible<T>::value) {
			m_pData[m_uSize].~T();
		}
        return true;
    }

    //no order 
    template<typename T>
    inline auto Array<T>::delete_swap(uint32_t index) -> bool {
        H_ASSERT(m_uSize > 0 && index < m_uSize);
        const uint32_t last_index = m_uSize - 1;
        if (index != last_index) {
			if constexpr (!std::is_trivially_destructible<T>::value) {
				m_pData[index].~T();
			}
			if constexpr (std::is_trivially_copyable<T>::value) {
				m_pData[index] = std::move(m_pData[last_index]);
			}
			else {
				new (&m_pData[index]) T(std::move(m_pData[last_index]));
				if constexpr (!std::is_trivially_destructible<T>::value) {
					m_pData[last_index].~T();
				}
			}
        }
        else if constexpr (!std::is_trivially_destructible<T>::value) {
            m_pData[last_index].~T();
        }
        --m_uSize;
        return true;
    }

    template<typename T>
    inline auto Array<T>::operator [](uint32_t index) ->T&  {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    inline const T& Array<T>::operator [](uint32_t index) const {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    inline auto Array<T>::clear() -> bool{
		if constexpr (!std::is_trivially_destructible<T>::value) {
			for (uint32_t i = 0; i < m_uSize; ++i) {
				m_pData[i].~T();
			}
		}
        m_uSize = 0;
        return true;
    }


    template<typename T>
    inline auto Array<T>::set_size(uint32_t new_size) -> bool {
        bool ret = true;

        if (new_size > m_uCapacity) {
            ret = grow(new_size);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
		if constexpr (!std::is_trivially_default_constructible<T>::value) {
			for (uint32_t i = m_uSize; i < new_size; ++i) {
				new (&m_pData[i]) T();
			}			
		}
		if constexpr (!std::is_trivially_destructible<T>::value)
		{
			for (uint32_t i = new_size; i < m_uSize; ++i) {
				m_pData[i].~T();
			}
		}
        m_uSize = new_size;
        return ret;
    }

    template<typename T>
    inline auto Array<T>::set_capacity(uint32_t new_capacity) -> bool {
        bool ret = true;

        if (new_capacity > m_uCapacity) {
            ret = grow(new_capacity);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
        return ret;
    }

    template<typename T>
    inline auto Array<T>::grow(uint32_t new_capacity) ->bool{
        bool ret = true;

        if (new_capacity <= (m_uCapacity * 1.5f)) {
            new_capacity = m_uCapacity * 1.5f;
        }
        else if (new_capacity < 4) {
            new_capacity = 4;
        }

        T* new_data = (T*) Ash_Alloc(m_pAllocator, new_capacity * sizeof(T), alignof(T));//(T*)m_pAllocator->allocate(new_capacity * sizeof(T), alignof(T));
		if (m_uSize > 0 && m_pData != nullptr) {
            if constexpr (std::is_trivially_copyable<T>::value) {
                memory_copy(new_data, m_pData, m_uSize * sizeof(T));
            }
            else {
			    for (uint32_t i = 0; i < m_uSize; ++i) {
					new (&new_data[i]) T(std::move(m_pData[i]));
					if constexpr (!std::is_trivially_destructible<T>::value) {
						m_pData[i].~T(); 
					}
			    }
            }
		}

		if (m_pData) {
			Ash_Free(m_pAllocator, m_pData);
		}

        m_pData = new_data;
        m_uCapacity = new_capacity;
        return ret;
    }

    template<typename T>
    inline auto Array<T>::back() ->T& {
        H_ASSERT(m_uSize);
        return m_pData[m_uSize - 1];
    }


    template<typename T>
    inline auto Array<T>::back() const -> const T& {
        H_ASSERT(m_uSize);
        return m_pData[m_uSize - 1];
    }

    template<typename T>
    inline auto Array<T>::front() -> T& {
        H_ASSERT(m_uSize);
        return m_pData[0];
    }

    template<typename T>
    inline auto Array<T>::front() const -> const T& {
        H_ASSERT(m_uSize);
        return m_pData[0];
    }

    template<typename T>
    inline auto Array<T>::size_in_bytes() const -> uint32_t {
        return m_uSize * sizeof(T);
    }

    template<typename T>
    inline auto Array<T>::capacity_in_bytes() const -> uint32_t {
        return m_uCapacity * sizeof(T);
    }

    // ArrayView //////////////////////////////////////////////////////////
    template<typename T>
    inline ArrayView<T>::ArrayView(T* data_, uint32_t size_)
        : m_pData(data_), m_uSize(size_) {
    }

    template<typename T>
    inline void ArrayView<T>::set(T* data_, uint32_t size_) {
        m_pData = data_;
        m_uSize = size_;
    }

    template<typename T>
    inline auto ArrayView<T>::operator[](uint32_t index) -> T& {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    inline auto ArrayView<T>::operator[](uint32_t index) const -> const T& {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

};
