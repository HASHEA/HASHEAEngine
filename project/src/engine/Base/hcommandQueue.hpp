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
			for (const auto& elem : command_queue) {
				elem();
			}
			command_queue.clear();
		}
	};
};