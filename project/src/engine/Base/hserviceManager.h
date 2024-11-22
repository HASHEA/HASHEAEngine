#pragma once
#include "hplatform.h"
#include "hcore.h"
#include"ds/hhash_map.hpp"
namespace HASHEAENGINE
{
	class Allocator;
	struct Service;

	struct ServiceManager
	{
		auto init(Allocator* allocator) -> bool;
		auto shutdown() -> bool;

		auto register_service(Service* service, const char* name) -> bool;
		auto cancel_service(const char* name) -> bool;

		Service* get_service(const char* name);

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
		FlatHashMap<uint64_t, Service*> services;
	};
};