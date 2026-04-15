#include "hthreading.h"

#include "hlog.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		struct QueuedCommand
		{
			std::string debug_name{};
			std::function<void()> function{};
			std::promise<void> completion{};
		};

		class CommandQueue
		{
		public:
			auto enqueue(const char* debug_name, std::function<void()> function) -> ThreadCommandFuture
			{
				QueuedCommand command{};
				command.debug_name = debug_name ? debug_name : "UnnamedCommand";
				command.function = std::move(function);
				ThreadCommandFuture future = command.completion.get_future().share();

				{
					std::scoped_lock<std::mutex> lock(m_mutex);
					m_commands.push_back(std::move(command));
				}
				return future;
			}

			auto try_pop(QueuedCommand& out_command) -> bool
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				if (m_commands.empty())
				{
					return false;
				}

				out_command = std::move(m_commands.front());
				m_commands.pop_front();
				return true;
			}

			auto empty() const -> bool
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				return m_commands.empty();
			}

			auto clear_pending_with_exception(const std::string& message) -> void
			{
				std::deque<QueuedCommand> pending{};
				{
					std::scoped_lock<std::mutex> lock(m_mutex);
					pending.swap(m_commands);
				}

				for (QueuedCommand& command : pending)
				{
					try
					{
						throw std::runtime_error(message);
					}
					catch (...)
					{
						command.completion.set_exception(std::current_exception());
					}
				}
			}

		private:
			mutable std::mutex m_mutex{};
			std::deque<QueuedCommand> m_commands{};
		};

		struct ThreadingState
		{
			EngineThreadingConfig config{};
			CommandQueue render_queue{};
			CommandQueue worker_queue{};
			std::condition_variable worker_condition{};
			std::mutex worker_condition_mutex{};
			std::vector<std::thread> worker_threads{};
			std::atomic<bool> initialized{ false };
			std::atomic<bool> shutting_down{ false };
			std::atomic<bool> worker_stop_requested{ false };
		};

		thread_local EngineThreadRole g_current_thread_role = EngineThreadRole::Unknown;
		ThreadingState g_threading_state{};

		static auto make_ready_future() -> ThreadCommandFuture
		{
			std::promise<void> promise{};
			promise.set_value();
			return promise.get_future().share();
		}

		static auto make_exception_future(const char* message) -> ThreadCommandFuture
		{
			std::promise<void> promise{};
			try
			{
				throw std::runtime_error(message ? message : "Threaded command failed.");
			}
			catch (...)
			{
				promise.set_exception(std::current_exception());
			}
			return promise.get_future().share();
		}

		static auto execute_command(QueuedCommand& command, EngineThreadRole execution_role) -> void
		{
			const EngineThreadRole previous_role = g_current_thread_role;
			g_current_thread_role = execution_role;

			try
			{
				if (command.function)
				{
					command.function();
				}
				command.completion.set_value();
			}
			catch (const std::exception& exception)
			{
				HLogError("Threaded command '{}' failed: {}", command.debug_name, exception.what());
				command.completion.set_exception(std::current_exception());
			}
			catch (...)
			{
				HLogError("Threaded command '{}' failed with an unknown exception.", command.debug_name);
				command.completion.set_exception(std::current_exception());
			}

			g_current_thread_role = previous_role;
		}

		static auto worker_thread_main(uint32_t worker_index) -> void
		{
			register_current_thread_role(EngineThreadRole::Worker);
			HLogInfo("Worker thread {} started.", worker_index);

			while (true)
			{
				QueuedCommand command{};
				if (g_threading_state.worker_queue.try_pop(command))
				{
					execute_command(command, EngineThreadRole::Worker);
					continue;
				}

				std::unique_lock<std::mutex> lock(g_threading_state.worker_condition_mutex);
				g_threading_state.worker_condition.wait(lock, []
				{
					return g_threading_state.worker_stop_requested.load(std::memory_order_acquire) ||
						!g_threading_state.worker_queue.empty();
				});
				lock.unlock();

				if (g_threading_state.worker_stop_requested.load(std::memory_order_acquire) &&
					g_threading_state.worker_queue.empty())
				{
					break;
				}
			}

			HLogInfo("Worker thread {} stopped.", worker_index);
			register_current_thread_role(EngineThreadRole::Unknown);
		}
	}

	auto initialize_threading(const EngineThreadingConfig& config) -> bool
	{
		if (g_threading_state.initialized.load(std::memory_order_acquire))
		{
			register_current_thread_role(EngineThreadRole::Render);
			return true;
		}

		g_threading_state.config = config;
		g_threading_state.shutting_down.store(false, std::memory_order_release);
		g_threading_state.worker_stop_requested.store(false, std::memory_order_release);

		uint32_t worker_count = config.worker_thread_count;
		if (worker_count == 0)
		{
			const uint32_t hardware_threads = std::max(1u, std::thread::hardware_concurrency());
			worker_count = std::max(1u, hardware_threads > 1 ? hardware_threads - 1 : 1u);
		}
		g_threading_state.config.worker_thread_count = worker_count;

		try
		{
			g_threading_state.worker_threads.reserve(worker_count);
			for (uint32_t worker_index = 0; worker_index < worker_count; ++worker_index)
			{
				g_threading_state.worker_threads.emplace_back(worker_thread_main, worker_index);
			}
		}
		catch (const std::exception& exception)
		{
			HLogError("Failed to initialize worker threads: {}", exception.what());
			g_threading_state.worker_stop_requested.store(true, std::memory_order_release);
			g_threading_state.worker_condition.notify_all();
			for (std::thread& thread : g_threading_state.worker_threads)
			{
				if (thread.joinable())
				{
					thread.join();
				}
			}
			g_threading_state.worker_threads.clear();
			return false;
		}

		g_threading_state.initialized.store(true, std::memory_order_release);
		register_current_thread_role(EngineThreadRole::Render);
		HLogInfo(
			"Threading initialized. logic_thread={}, worker_threads={}.",
			config.enable_logic_thread ? "enabled" : "disabled",
			g_threading_state.config.worker_thread_count);
		return true;
	}

	auto shutdown_threading() -> void
	{
		if (!g_threading_state.initialized.load(std::memory_order_acquire))
		{
			return;
		}

		g_threading_state.shutting_down.store(true, std::memory_order_release);
		g_threading_state.worker_stop_requested.store(true, std::memory_order_release);
		g_threading_state.worker_condition.notify_all();

		for (std::thread& thread : g_threading_state.worker_threads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
		g_threading_state.worker_threads.clear();

		g_threading_state.render_queue.clear_pending_with_exception("Render command queue was shut down.");
		g_threading_state.worker_queue.clear_pending_with_exception("Worker command queue was shut down.");
		g_threading_state.initialized.store(false, std::memory_order_release);
		g_threading_state.shutting_down.store(false, std::memory_order_release);
		HLogInfo("Threading shutdown complete.");
	}

	auto get_threading_config() -> const EngineThreadingConfig&
	{
		return g_threading_state.config;
	}

	auto register_current_thread_role(EngineThreadRole role) -> void
	{
		g_current_thread_role = role;
	}

	auto get_current_thread_role() -> EngineThreadRole
	{
		return g_current_thread_role;
	}

	auto thread_role_to_string(EngineThreadRole role) -> const char*
	{
		switch (role)
		{
		case EngineThreadRole::Render:
			return "Render";
		case EngineThreadRole::Logic:
			return "Logic";
		case EngineThreadRole::Worker:
			return "Worker";
		case EngineThreadRole::RHI:
			return "RHI";
		case EngineThreadRole::Unknown:
		default:
			return "Unknown";
		}
	}

	auto is_in_render_thread() -> bool
	{
		return g_current_thread_role == EngineThreadRole::Render;
	}

	auto is_in_logic_thread() -> bool
	{
		return g_current_thread_role == EngineThreadRole::Logic;
	}

	auto is_in_worker_thread() -> bool
	{
		return g_current_thread_role == EngineThreadRole::Worker;
	}

	auto is_in_rhi_thread() -> bool
	{
		return g_current_thread_role == EngineThreadRole::RHI;
	}

	auto enqueue_render_command(const char* debug_name, std::function<void()> command) -> ThreadCommandFuture
	{
		if (!command)
		{
			return make_ready_future();
		}

		if (is_in_render_thread())
		{
			QueuedCommand immediate{};
			immediate.debug_name = debug_name ? debug_name : "ImmediateRenderCommand";
			immediate.function = std::move(command);
			ThreadCommandFuture future = immediate.completion.get_future().share();
			execute_command(immediate, EngineThreadRole::Render);
			return future;
		}

		if (g_threading_state.shutting_down.load(std::memory_order_acquire))
		{
			HLogWarning("Rejected render command '{}' because the threading system is shutting down.", debug_name ? debug_name : "UnnamedCommand");
			return make_exception_future("Render command queue is shutting down.");
		}

		if (!g_threading_state.initialized.load(std::memory_order_acquire))
		{
			QueuedCommand immediate{};
			immediate.debug_name = debug_name ? debug_name : "ImmediateRenderCommand";
			immediate.function = std::move(command);
			ThreadCommandFuture future = immediate.completion.get_future().share();
			execute_command(immediate, EngineThreadRole::Render);
			return future;
		}

		return g_threading_state.render_queue.enqueue(debug_name, std::move(command));
	}

	auto pump_render_commands(uint32_t max_command_count) -> uint32_t
	{
		if (!is_in_render_thread())
		{
			return 0;
		}

		uint32_t executed_count = 0;
		QueuedCommand command{};
		while ((max_command_count == 0 || executed_count < max_command_count) &&
			g_threading_state.render_queue.try_pop(command))
		{
			execute_command(command, EngineThreadRole::Render);
			++executed_count;
		}
		return executed_count;
	}

	auto flush_render_commands() -> void
	{
		if (is_in_render_thread())
		{
			while (pump_render_commands() > 0)
			{
			}
			return;
		}

		ThreadCommandFuture fence = enqueue_render_command("RenderCommandFence", []() {});
		fence.wait();
	}

	auto has_pending_render_commands() -> bool
	{
		return !g_threading_state.render_queue.empty();
	}

	auto is_threading_shutting_down() -> bool
	{
		return g_threading_state.shutting_down.load(std::memory_order_acquire);
	}

	namespace Detail
	{
		auto enqueue_worker_command(const char* debug_name, std::function<void()> command) -> ThreadCommandFuture
		{
			if (!command)
			{
				return make_ready_future();
			}

			if (g_threading_state.shutting_down.load(std::memory_order_acquire))
			{
				HLogWarning("Rejected worker command '{}' because the threading system is shutting down.", debug_name ? debug_name : "UnnamedCommand");
				return make_exception_future("Worker command queue is shutting down.");
			}

			if (!g_threading_state.initialized.load(std::memory_order_acquire) || g_threading_state.worker_threads.empty())
			{
				QueuedCommand immediate{};
				immediate.debug_name = debug_name ? debug_name : "ImmediateWorkerCommand";
				immediate.function = std::move(command);
				ThreadCommandFuture future = immediate.completion.get_future().share();
				execute_command(immediate, EngineThreadRole::Worker);
				return future;
			}

			ThreadCommandFuture future = g_threading_state.worker_queue.enqueue(debug_name, std::move(command));
			g_threading_state.worker_condition.notify_one();
			return future;
		}
	}
}
