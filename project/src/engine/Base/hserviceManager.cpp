#include "hserviceManager.h"
namespace AshEngine
{
	static ServiceManager           s_service_manager;
	ServiceManager* ServiceManager::instance = &s_service_manager;

	auto ServiceManager::init(Allocator* allocator_) -> bool
	{
		allocator = allocator_;

		services.init(allocator, 8);
		return true;
	}
	auto ServiceManager::shutdown() -> bool
	{
		std::scoped_lock<std::mutex> lock(services_mutex);
		services.shutdown();
		return true;
	}
	auto ServiceManager::register_service(Service* service, const char* name) -> bool
	{
		std::scoped_lock<std::mutex> lock(services_mutex);
		uint64_t hash_name = hash_calculate(name);
		FlatHashMapIterator it = services.find(hash_name);
		H_ASSERTLOG(it.is_invalid(), "Overwriting service{0}, is this intended ?", name);
		services.insert(hash_name, service);
		return true;
	}
	auto ServiceManager::cancel_service(const char* name) -> bool
	{
		std::scoped_lock<std::mutex> lock(services_mutex);
		uint64_t hash_name = hash_calculate(name);
		services.remove(hash_name);
		return true;
	}
	Service* ServiceManager::get_service(const char* name)
	{
		std::scoped_lock<std::mutex> lock(services_mutex);
		uint64_t hash_name = hash_calculate(name);
		return services.get(hash_name);
	}
};
