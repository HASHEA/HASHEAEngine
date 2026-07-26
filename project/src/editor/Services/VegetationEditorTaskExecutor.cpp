#include "Services/VegetationEditorTaskExecutor.h"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace AshEditor
{
	struct VegetationEditorTaskExecutor::Impl
	{
		struct Task
		{
			std::shared_ptr<std::atomic_bool> cancel_requested =
				std::make_shared<std::atomic_bool>(false);
			std::atomic_bool complete{ false };
			mutable std::mutex exception_mutex{};
			std::exception_ptr exception{};
			std::mutex join_mutex{};
			std::thread worker{};
		};

		mutable std::mutex tasks_mutex{};
		std::atomic_uint64_t next_task_id{ 1 };
		std::map<uint64_t, std::shared_ptr<Task>> tasks{};
	};

	VegetationEditorTaskExecutor::VegetationEditorTaskExecutor()
		: m_impl(std::make_unique<Impl>())
	{
	}

	VegetationEditorTaskExecutor::~VegetationEditorTaskExecutor()
	{
		CancelAndJoinAll();
	}

	uint64_t VegetationEditorTaskExecutor::Submit(
		VegetationEditorTaskSubmission submission)
	{
		if (!m_impl || !submission.work)
		{
			return 0;
		}

		uint64_t task_id =
			m_impl->next_task_id.fetch_add(1, std::memory_order_relaxed);
		if (task_id == 0)
		{
			task_id = m_impl->next_task_id.fetch_add(1, std::memory_order_relaxed);
			if (task_id == 0)
			{
				return 0;
			}
		}

		const std::shared_ptr<Impl::Task> task = std::make_shared<Impl::Task>();
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			if (!m_impl->tasks.emplace(task_id, task).second)
			{
				return 0;
			}
		}

		try
		{
			task->worker = std::thread(
				[task, deadline = submission.deadline,
					work = std::move(submission.work)]() mutable
				{
					AshEngine::VegetationOperationControl control{};
					control.cancel_requested = task->cancel_requested;
					control.deadline = deadline;
					try
					{
						work(std::move(control));
					}
					catch (...)
					{
						std::lock_guard<std::mutex> lock(task->exception_mutex);
						task->exception = std::current_exception();
					}
					task->complete.store(true, std::memory_order_release);
				});
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			m_impl->tasks.erase(task_id);
			return 0;
		}
		return task_id;
	}

	void VegetationEditorTaskExecutor::RequestCancel(const uint64_t task_id)
	{
		std::shared_ptr<Impl::Task> task{};
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			const auto found = m_impl->tasks.find(task_id);
			if (found != m_impl->tasks.end())
			{
				task = found->second;
			}
		}
		if (task)
		{
			task->cancel_requested->store(true, std::memory_order_release);
		}
	}

	bool VegetationEditorTaskExecutor::IsComplete(const uint64_t task_id) const
	{
		std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
		const auto found = m_impl->tasks.find(task_id);
		return found != m_impl->tasks.end() &&
			found->second->complete.load(std::memory_order_acquire);
	}

	std::exception_ptr VegetationEditorTaskExecutor::GetException(
		const uint64_t task_id) const
	{
		std::shared_ptr<Impl::Task> task{};
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			const auto found = m_impl->tasks.find(task_id);
			if (found != m_impl->tasks.end())
			{
				task = found->second;
			}
		}
		if (!task)
		{
			return {};
		}
		std::lock_guard<std::mutex> lock(task->exception_mutex);
		return task->exception;
	}

	void VegetationEditorTaskExecutor::Join(const uint64_t task_id)
	{
		std::shared_ptr<Impl::Task> task{};
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			const auto found = m_impl->tasks.find(task_id);
			if (found != m_impl->tasks.end())
			{
				task = found->second;
			}
		}
		if (!task)
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lock(task->join_mutex);
			if (task->worker.joinable())
			{
				task->worker.join();
			}
		}
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			const auto found = m_impl->tasks.find(task_id);
			if (found != m_impl->tasks.end() &&
				found->second == task)
			{
				m_impl->tasks.erase(found);
			}
		}
	}

	void VegetationEditorTaskExecutor::CancelAndJoinAll()
	{
		if (!m_impl)
		{
			return;
		}
		std::vector<std::pair<uint64_t, std::shared_ptr<Impl::Task>>> tasks{};
		{
			std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
			tasks.reserve(m_impl->tasks.size());
			for (const auto& task : m_impl->tasks)
			{
				tasks.push_back(task);
			}
		}
		for (const auto& task : tasks)
		{
			task.second->cancel_requested->store(true, std::memory_order_release);
		}
		for (const auto& task : tasks)
		{
			Join(task.first);
		}
	}

	bool VegetationEditorTaskExecutor::IsIdle() const
	{
		std::lock_guard<std::mutex> lock(m_impl->tasks_mutex);
		for (const auto& task : m_impl->tasks)
		{
			if (!task.second->complete.load(std::memory_order_acquire))
			{
				return false;
			}
		}
		return true;
	}
}
