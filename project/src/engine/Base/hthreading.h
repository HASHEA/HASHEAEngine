#pragma once

#include "hcore.h"
#include <cstdint>
#include <future>
#include <functional>
#include <type_traits>

namespace AshEngine
{
	enum class EngineThreadRole : uint8_t
	{
		Unknown = 0,
		Render,
		Logic,
		Worker,
		RHI,
	};

	struct ASH_API EngineThreadingConfig
	{
		bool enable_logic_thread = false;
		uint32_t worker_thread_count = 0;
		uint32_t logic_thread_idle_sleep_ms = 1;
	};

	using ThreadCommandFuture = std::shared_future<void>;

	ASH_API auto initialize_threading(const EngineThreadingConfig& config) -> bool;
	ASH_API auto shutdown_threading() -> void;
	ASH_API auto get_threading_config() -> const EngineThreadingConfig&;

	ASH_API auto register_current_thread_role(EngineThreadRole role) -> void;
	ASH_API auto get_current_thread_role() -> EngineThreadRole;
	ASH_API auto thread_role_to_string(EngineThreadRole role) -> const char*;
	ASH_API auto is_in_render_thread() -> bool;
	ASH_API auto is_in_logic_thread() -> bool;
	ASH_API auto is_in_worker_thread() -> bool;
	ASH_API auto is_in_rhi_thread() -> bool;

	ASH_API auto enqueue_render_command(const char* debug_name, std::function<void()> command) -> ThreadCommandFuture;
	ASH_API auto pump_render_commands(uint32_t max_command_count = 0) -> uint32_t;
	ASH_API auto flush_render_commands() -> void;
	ASH_API auto has_pending_render_commands() -> bool;
	ASH_API auto is_threading_shutting_down() -> bool;

	namespace Detail
	{
		ASH_API auto enqueue_worker_command(const char* debug_name, std::function<void()> command) -> ThreadCommandFuture;
	}

	template <typename TaskFn>
	auto dispatch_background_task(const char* debug_name, TaskFn&& task)
		-> std::shared_future<typename std::invoke_result_t<std::decay_t<TaskFn>>>
	{
		using ReturnType = typename std::invoke_result_t<std::decay_t<TaskFn>>;

		auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<TaskFn>(task));
		std::shared_future<ReturnType> future = packagedTask->get_future().share();
		Detail::enqueue_worker_command(debug_name, [packagedTask]() mutable
		{
			(*packagedTask)();
		});
		return future;
	}
}

#define ASH_ENQUEUE_RENDER_COMMAND(Name, Lambda) ::AshEngine::enqueue_render_command(Name, Lambda)
