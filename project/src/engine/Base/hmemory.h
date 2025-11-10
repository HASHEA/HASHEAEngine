#pragma once
#include <string>
#include <stdarg.h>
#include "hcore.h"
#include "hservice.h"
#include <memory>
namespace AshEngine
{
//#define ASH_TRACE_MEM_ALLOCATE
//#define ASH_TRACE_MEM_DEALLOCATE
	// Memory Methods /////////////////////////////////////////////////////
	void memory_copy(void* destination, void* source, size_t size);

	//
	//  Calculate aligned memory size.
	size_t memory_align(size_t size, size_t alignment);


	enum AllocType
	{
		eHeap,
		eStack,
		eLinear,
	};
	
	struct Allocator 
	{
		virtual ~Allocator() { }
		virtual auto allocate(size_t size, size_t alignment) -> void* = 0;
		virtual auto allocate(size_t size, size_t alignment, char* file, uint32_t line) -> void* = 0;

		virtual auto deallocate(void* pointer, char* file, uint32_t line) -> bool = 0;
		virtual auto deallocate(void* pointer) -> bool = 0;
		virtual auto deallocate(const void* pointer) -> bool = 0;
		virtual auto deallocate(const void* pointer, char* file, uint32_t line) -> bool = 0;
	}; // struct Allocator

	struct MemoryStatistics {
		size_t                       m_szAllocatedbytes;
		size_t                       m_szTotalBytes;

		uint32_t                         m_uAllocationCounts;

		auto add(size_t a) -> bool {
			if (a) {
				m_szAllocatedbytes += a;
				++m_uAllocationCounts;
			}
			return true;
		}
	}; // struct MemoryStatistics

	class HeapAllocator : public Allocator
	{
	public:
		~HeapAllocator() override;

		auto init(size_t size) -> bool;
		auto shutdown()-> bool;

#ifdef ASH_DEBUG
		auto on_gui() -> void;
#endif // ASH_DEBUG

		auto allocate(size_t size, size_t alignment) -> void* override;
		auto allocate(size_t size, size_t alignment, char* file, uint32_t line)->void* override;

		auto deallocate(void* pointer, char* file, uint32_t line)-> bool override;
		auto deallocate(void* pointer) -> bool override;
		auto deallocate(const void* pointer) -> bool override;
		auto deallocate(const void* pointer, char* file, uint32_t line) -> bool override;

	private:
		void* m_pTlsfHandle = nullptr;
		void* m_pMemory = nullptr;
		size_t                       m_szAllocatedSize = 0;
		size_t                       m_szMaxSize = 0;
	}; // struct HeapAllocator

	class StackAllocator : public Allocator
	{
	public:
		auto                        init(size_t size) -> bool;
		auto                        shutdown() -> bool;

		auto allocate(size_t size, size_t alignment)-> void* override;
		auto allocate(size_t size, size_t alignment, char* file, uint32_t line)-> void* override;

		auto                        deallocate(void* pointer, char* file, uint32_t line)-> bool override;
		auto						deallocate(void* pointer) -> bool override;
		auto						deallocate(const void* pointer) -> bool override;
		auto						deallocate(const void* pointer, char* file, uint32_t line) -> bool override;
		auto						get_marker() -> size_t;
		auto                        free_marker(size_t marker) -> bool;

		auto                        clear() -> bool;
	private:
		uint8_t* m_pMemory = nullptr;
		size_t                       m_szTotalSize = 0;
		size_t                       m_szAllocatedSize = 0;

	}; // struct StackAllocator

	//
// Allocator that can only be reset.
//
	class LinearAllocator : public Allocator 
	{
	public:
		~LinearAllocator();

		auto                        init(size_t size) -> bool;
		auto                        shutdown()-> bool;

		auto						allocate(size_t size, size_t alignment)->void* override;
		auto						allocate(size_t size, size_t alignment, char* file, uint32_t line)->void* override;

		auto                        deallocate(void* pointer, char* file, uint32_t line)-> bool override;
		auto						deallocate(void* pointer) -> bool override;
		auto						deallocate(const void* pointer) -> bool override;
		auto						deallocate(const void* pointer, char* file, uint32_t line) -> bool override;
		auto                        clear()-> bool;
	private:
		uint8_t* m_pMemory = nullptr;
		size_t                       m_szTotalSize = 0;
		size_t                       m_szAllocatedSize = 0;
	}; // struct LinearAllocator
	struct MemoryServiceConfiguration {

		size_t                       m_szMaxDynamicSize = 512 * 1024 * 1024;    // Defaults to max 32MB of dynamic memory.

	};
	class MemoryService : public Service
	{
	public:
		static constexpr char* k_name = "memory_service";
		auto init(void* configuration) -> bool override;
		auto shutdown() -> bool override;
#ifdef ASH_DEBUG
		auto on_gui() -> void override;
#endif // ASH_DEBUG
		ASH_DECLARE_SERVICE(MemoryService);
		auto get_system_allocator() -> HeapAllocator*
		{
			return &m_heapAllocator;
		};

		auto get_stack_allocator() -> StackAllocator*
		{
			return &m_stackAllocator;
		};
	private:
		// Global Frame allocator
		StackAllocator m_stackAllocator{};
		HeapAllocator m_heapAllocator{};
	};

	/*template<typename T, typename ... Args>
	T* _OriginalNew(Args&& ... args) {
		T* pResult = new T(std::forward<Args>(args) ...);
		return pResult;
	}

	template<typename T>
	void _OriginalDelete(T* pObject) {
		delete pObject;
	};*/

	//template<typename T, typename ... Args>
	//T* _OriginalNewArray(size_t uSize, Args&& ... args) {
	//	T* pResult = new T[uSize](std::forward<Args>(args) ...);
	//	return pResult;
	//}

	//template<typename T>
	//void _OriginalDeleteArray(T* pObject)
	//{
	//	delete[] pObject;
	//}

	template<typename T, typename ...Args>
	T* _original_placement_new(void* pvAddress, Args&& ... args) {
		T* pResult = new (pvAddress) T(std::forward<Args>(args) ...);
		return pResult;
	}

	template<typename T>
	void _original_destroy(T* pObject)
	{
		pObject->~T();
	}

//if nullptr, use system allocator
#ifdef ASH_TRACE_MEM_ALLOCATE
#define Ash_Alloc(allocater,size,align/*set 1 to no align*/)\
	((allocater) == nullptr ? ((MemoryService::instance()->get_system_allocator())->allocate( size, align, __FILE__, __LINE__ )):(static_cast<Allocator*>(allocater)->allocate( size, align, __FILE__, __LINE__ )));
#else
#define Ash_Alloc(allocater,size,align/*set 1 to no align*/)\
	((allocater) == nullptr ? ((MemoryService::instance()->get_system_allocator())->allocate( size, align)):(static_cast<Allocator*>(allocater)->allocate( size, align)));
#endif

template<typename T, typename... Args>
inline T* Ash_New(Allocator* allocator = nullptr,Args&&... args) {
	if (allocator == nullptr) {
#ifdef ASH_TRACE_MEM_ALLOCATE
		return _original_placement_new<T>(static_cast<T*>(MemoryService::instance()->get_system_allocator()->allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);
#else
		return _original_placement_new<T>(static_cast<T*>(MemoryService::instance()->get_system_allocator()->allocate(sizeof(T), 1)), std::forward<Args>(args)...);
#endif
	}
	else {
#ifdef ASH_TRACE_MEM_ALLOCATE
		return _original_placement_new<T>(static_cast<T*>(allocator->allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);

#else
		return _original_placement_new<T>(static_cast<T*>(allocator->allocate(sizeof(T), 1)), std::forward<Args>(args)...);
#endif
	}
}

template<typename T, typename... Args>
inline std::shared_ptr<T> Ash_New_Shared(Args&&... args) {
	T* pO = nullptr;
#ifdef ASH_TRACE_MEM_ALLOCATE
	pO = _original_placement_new<T>(static_cast<T*>(MemoryService::instance()->get_system_allocator()->allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);

#else
	pO = _original_placement_new<T>(static_cast<T*>(MemoryService::instance()->get_system_allocator()->allocate(sizeof(T), 1)), std::forward<Args>(args)...);
#endif
	auto Deleter = [](T* pObject) {
		auto allocator = (MemoryService::instance()->get_system_allocator());
		_original_destroy(pObject);
#ifdef ASH_TRACE_MEM_DEALLOCATE
		return allocator->deallocate(pObject, __FILE__, __LINE__);

#else
		return allocator->deallocate(pObject);
#endif
		
	};
	std::shared_ptr<T> sp(pO, Deleter);
	return sp;
}

//some problem can;t cover
//template<typename T, typename... Args>
//std::unique_ptr<T> Ash_New_Unique(Args&&... args) {
//	T* pO = nullptr;
//	pO = _original_placement_new<T>(static_cast<T*>(MemoryService::instance()->get_system_allocator()->allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);
//
//	auto Deleter = [](T* pObject) {
//		auto allocator = (MemoryService::instance()->get_system_allocator());
//		_original_destroy(pObject);
//		allocator->deallocate(pObject);
//		};
//	std::unique_ptr<T, decltype(Deleter)> sp(pO, Deleter);
//	return sp;
//}


#ifdef ASH_TRACE_MEM_DEALLOCATE
#define Ash_Free(_allocator,pObject)\
{\
	Allocator* l_pAlloc = (_allocator);\
	if(!(l_pAlloc))\
		(l_pAlloc) = (MemoryService::instance()->get_system_allocator());\
	(l_pAlloc)->deallocate(pObject,__FILE__, __LINE__);\
}
#else
#define Ash_Free(_allocator,pObject)\
{\
	Allocator* l_pAlloc = (_allocator);\
	if(!(l_pAlloc))\
		(l_pAlloc) = (MemoryService::instance()->get_system_allocator());\
	(l_pAlloc)->deallocate(pObject);\
}
#endif

#ifdef ASH_TRACE_MEM_DEALLOCATE
#define Ash_Delete(_allocator,pObject)\
{\
	if(pObject)\
	{\
	Allocator* l_pAlloc = (_allocator);\
	if(!(l_pAlloc))\
			(l_pAlloc) = (MemoryService::instance()->get_system_allocator());\
	_original_destroy(pObject);\
	(l_pAlloc)->deallocate(pObject,__FILE__, __LINE__);\
	 pObject = nullptr;\
	}\
}
#else
#define Ash_Delete(_allocator,pObject)\
{\
	if(pObject)\
	{\
	Allocator* l_pAlloc = (_allocator);\
	if(!(l_pAlloc))\
			(l_pAlloc) = (MemoryService::instance()->get_system_allocator());\
	_original_destroy(pObject);\
	(l_pAlloc)->deallocate(pObject);\
	 pObject = nullptr;\
	}\
}
#endif




#define ASH_KILO(size)                 (size * 1024)
#define ASH_MEGA(size)                 (size * 1024 * 1024)
#define ASH_GIGA(size)                 (size * 1024 * 1024 * 1024)

};

