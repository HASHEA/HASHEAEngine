#pragma once
#include "hplatform.h"
#include "hcore.h"
#include"ds/hhash_map.hpp"
#include <mutex>
namespace AshEngine
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
		static ServiceManager* instance;
		Allocator* allocator = nullptr;
		FlatHashMap<uint64_t, Service*> services;
		std::mutex services_mutex;
	};
	template<typename T>
	inline T* ServiceManager::get() {
		std::scoped_lock<std::mutex> lock(services_mutex);
		uint64_t hash_name = hash_calculate(T::k_name);
		Service* existing = services.get(hash_name);
		if (existing)
		{
			return (T*)existing;
		}
		T* service = T::instance();
		services.insert(hash_name, service);
		return service;
	}
};
