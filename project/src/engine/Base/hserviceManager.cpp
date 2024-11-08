#include "hserviceManager.h"
namespace HASHEAENGINE
{
	auto ServiceManager::Init(Allocator* allocator) -> bool
	{
		return false;
	}
	auto ServiceManager::Shutdown() -> bool
	{
		return false;
	}
	auto ServiceManager::RegisterService(Service* service, const char* name) -> bool
	{
		return false;
	}
	auto ServiceManager::CancelService(const char* name) -> bool
	{
		return false;
	}
	Service* ServiceManager::GetService(const char* name)
	{
		return nullptr;
	}
};