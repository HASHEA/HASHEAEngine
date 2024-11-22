#include "hserviceManager.h"
namespace HASHEAENGINE
{
	static ServiceManager           s_service_manager;
	ServiceManager* ServiceManager::instance = &s_service_manager;

	auto ServiceManager::Init(Allocator* allocator_) -> bool
	{
		allocator = allocator_;

		services.init(allocator, 8);
		return true;
	}
	auto ServiceManager::Shutdown() -> bool
	{
		services.shutdown();
		return true;
	}
	auto ServiceManager::RegisterService(Service* service, const char* name) -> bool
	{
		uint64_t hash_name = hash_calculate(name);
		FlatHashMapIterator it = services.find(hash_name);
		H_ASSERTLOG(it.is_invalid(), "Overwriting service{0}, is this intended ?", name);
		services.insert(hash_name, service);
		return true;
	}
	auto ServiceManager::CancelService(const char* name) -> bool
	{
		uint64_t hash_name = hash_calculate(name);
		services.remove(hash_name);
		return true;
	}
	Service* ServiceManager::GetService(const char* name)
	{
		uint64_t hash_name = hash_calculate(name);
		return services.get(hash_name);
	}
};