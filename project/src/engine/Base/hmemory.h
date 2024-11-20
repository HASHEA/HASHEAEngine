#pragma once
#include <string>
#include <stdarg.h>
#include "hcore.h"
#include "hservice.h"
#include <memory>
namespace HASHEAENGINE
{

	// Memory Methods /////////////////////////////////////////////////////
	void MemoryCopy(void* destination, void* source, size_t size);

	//
	//  Calculate aligned memory size.
	size_t MemoryAlign(size_t size, size_t alignment);


	enum AllocType
	{
		eHeap,
		eStack,
		eLinear,
	};
	
	struct Allocator 
	{
		virtual ~Allocator() { }
		virtual auto Allocate(size_t size, size_t alignment) -> void* = 0;
		virtual auto Allocate(size_t size, size_t alignment, char* file, uint32_t line) -> void* = 0;

		virtual auto Deallocate(void* pointer) -> HS_Result = 0;
	}; // struct Allocator

	struct MemoryStatistics {
		size_t                       m_szAllocatedbytes;
		size_t                       m_szTotalBytes;

		uint32_t                         m_uAllocationCounts;

		auto Add(size_t a) -> HS_Result {
			if (a) {
				m_szAllocatedbytes += a;
				++m_uAllocationCounts;
			}
			return HS_OK;
		}
	}; // struct MemoryStatistics

	class HeapAllocator : public Allocator
	{
	public:
		~HeapAllocator() override;

		auto Init(size_t size) -> HS_Result;
		auto Shutdown()-> HS_Result;

#ifdef HASHEA_DEBUG
		auto OnGUI() -> void;
#endif // HASHEA_DEBUG

		auto Allocate(size_t size, size_t alignment) -> void* override;
		auto Allocate(size_t size, size_t alignment, char* file, uint32_t line)->void* override;

		auto Deallocate(void* pointer)-> HS_Result override;
	private:
		void* m_pTlsfHandle = nullptr;
		void* m_pMemory = nullptr;
		size_t                       m_szAllocatedSize = 0;
		size_t                       m_szMaxSize = 0;
	}; // struct HeapAllocator

	class StackAllocator : public Allocator
	{
	public:
		auto                        Init(size_t size) -> HS_Result;
		auto                        Shutdown() -> HS_Result;

		auto Allocate(size_t size, size_t alignment)-> void* override;
		auto Allocate(size_t size, size_t alignment, char* file, uint32_t line)-> void* override;

		auto                        Deallocate(void* pointer)-> HS_Result override;

		auto                       GetMarker() -> size_t;
		auto                        FreeMarker(size_t marker) -> HS_Result;

		auto                        Clear() -> HS_Result;
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

		auto                        Init(size_t size) -> HS_Result;
		auto                        Shutdown()-> HS_Result;

		auto Allocate(size_t size, size_t alignment)->void* override;
		auto Allocate(size_t size, size_t alignment, char* file, uint32_t line)->void* override;

		auto                        Deallocate(void* pointer)-> HS_Result override;

		auto                        Clear()-> HS_Result;
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
		auto Init(void* configuration) -> HS_Result override;
		auto Shutdown() -> HS_Result override;
#ifdef HASHEA_DEBUG
		auto OnGUI() -> void override;
#endif // HASHEA_DEBUG
		HASHEA_DECLARE_SERVICE(MemoryService);
		auto GetSystemAllocator() -> HeapAllocator*
		{
			return &m_heapAllocator;
		};

		auto GetStackAllocator() -> StackAllocator*
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
	T* _OriginalPlacementNew(void* pvAddress, Args&& ... args) {
		T* pResult = new (pvAddress) T(std::forward<Args>(args) ...);
		return pResult;
	}

	template<typename T>
	void _OriginalDestroy(T* pObject)
	{
		pObject->~T();
	}

//if nullptr, use system allocator
#define Hashea_Alloc(allocater,size,align/*set 1 to no align*/)\
	((allocater) == nullptr ? ((MemoryService::instance()->GetSystemAllocator())->Allocate( size, align, __FILE__, __LINE__ )):(static_cast<Allocator*>(allocater)->Allocate( size, align, __FILE__, __LINE__ )))
	
//#define Hashea_New(allocater,type,...)\
//	(\
//		(allocater) == nullptr ? \
//		_OriginalPlacementNew<type>(static_cast<type*>(MemoryService::instance()->GetSystemAllocator()->Allocate( sizeof(type), 1, __FILE__, __LINE__ )),__VA_ARGS__) \
//		:_OriginalPlacementNew<type>(static_cast<type*>((allocater)->Allocate( sizeof(type), 1, __FILE__, __LINE__ )),__VA_ARGS__)\
//	)

template<typename T, typename... Args>
T* Hashea_New(Allocator* allocator = nullptr,Args&&... args) {
	if (allocator == nullptr) {
		return _OriginalPlacementNew<T>(static_cast<T*>(MemoryService::instance()->GetSystemAllocator()->Allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);
	}
	else {
		return _OriginalPlacementNew<T>(static_cast<T*>(allocator->Allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);
	}
}

template<typename T, typename... Args>
std::shared_ptr<T> Hashea_New_Shared(Args&&... args) {
	T* pO = nullptr;
	pO = _OriginalPlacementNew<T>(static_cast<T*>(MemoryService::instance()->GetSystemAllocator()->Allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);

	auto Deleter = [](T* pObject) {
		auto allocator = (MemoryService::instance()->GetSystemAllocator());
		_OriginalDestroy(pObject);
		allocator->Deallocate(pObject); 
	};
	std::shared_ptr<T> sp(pO, Deleter);
	return sp;
}

template<typename T, typename... Args>
std::unique_ptr<T> Hashea_New_Unique(Args&&... args) {
	T* pO = nullptr;
	pO = _OriginalPlacementNew<T>(static_cast<T*>(MemoryService::instance()->GetSystemAllocator()->Allocate(sizeof(T), 1, __FILE__, __LINE__)), std::forward<Args>(args)...);

	auto Deleter = [](T* pObject) {
		auto allocator = (MemoryService::instance()->GetSystemAllocator());
		_OriginalDestroy(pObject);
		allocator->Deallocate(pObject);
		};
	std::unique_ptr<T, decltype(Deleter)> sp(pO, Deleter);
	return sp;
}



#define Hashea_Free(_allocator,pObject)\
{\
	Allocator* l_pAlloc = (_allocator);\
	if(!(l_pAlloc))\
		(l_pAlloc) = (MemoryService::instance()->GetSystemAllocator());\
	(l_pAlloc)->Deallocate(pObject);\
}

#define Hashea_Delete(allocator,pObject)\
{\
	Allocator* l_pAlloc = allocater;\
	if(!(l_pAlloc))\
			(l_pAlloc) = (MemoryService::instance()->GetSystemAllocator());\
	_OriginalDestroy(pObject);\
	(l_pAlloc)->Deallocate(pObject);\
	 pObject = nullptr;\
}



#define HASHEA_KILO(size)                 (size * 1024)
#define HASHEA_MEGA(size)                 (size * 1024 * 1024)
#define HASHEA_GIGA(size)                 (size * 1024 * 1024 * 1024)

};

