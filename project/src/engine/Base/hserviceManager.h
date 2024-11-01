#pragma once
#include "hcore.h"
namespace HASHEAENGINE
{
	class Allocator;
	struct Service;

	struct ServiceManager
	{
		auto Init(Allocator* allocator) -> bool;
		auto Shutdown() -> bool;

		auto RegisterService(Service* service, const char* name) -> bool;
		auto CancelService(const char* name) -> bool;

		Service* GetService(const char* name);

		template<typename T>
		T* get();

		template<typename T>
		inline T* ServiceManager::get() {
			T* service = (T*)GetService(T::k_name);
			if (!service) {
				RegisterService(T::instance(), T::k_name);
			}
			return T::instance();
		}

		static ServiceManager* instance;
		Allocator* allocator = nullptr;
	};
};