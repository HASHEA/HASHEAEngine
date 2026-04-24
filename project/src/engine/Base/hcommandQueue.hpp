#pragma once
#include "hcore.h"
#include "hplatform.h"
#include <deque>
#include <functional>
namespace AshEngine
{
	struct DelayCommandQueue
	{
		DelayCommandQueue() = default;
		DelayCommandQueue(const DelayCommandQueue&) = delete;
		auto operator=(const DelayCommandQueue&)->DelayCommandQueue & = delete;
		std::deque<std::function<void()>> command_queue;
		template <typename F>
		inline auto emplace(F&& function)
		{
			command_queue.emplace_back(function);
		}
		//force flush
		inline auto flush()
		{
			// Lambdas may emplace new entries into command_queue during execution
			// (e.g. ~VulkanDescriptorPool queues vkDestroyDescriptorPool while a
			// destructor cascade is being driven by flush itself). std::deque
			// invalidates iterators on emplace_back, so iterate a local copy and
			// re-drain until no new work was queued.
			while (!command_queue.empty())
			{
				std::deque<std::function<void()>> drained;
				drained.swap(command_queue);
				for (const auto& elem : drained)
				{
					elem();
				}
			}
		}
	};
};