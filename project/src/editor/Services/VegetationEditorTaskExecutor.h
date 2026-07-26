#pragma once

#include "Function/Asset/VegetationSurface.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

namespace AshEditor
{
	struct VegetationEditorTaskSubmission
	{
		std::chrono::steady_clock::time_point deadline{};
		std::function<void(AshEngine::VegetationOperationControl)> work{};
	};

	class IVegetationEditorTaskExecutor
	{
	public:
		virtual ~IVegetationEditorTaskExecutor() = default;
		virtual uint64_t Submit(VegetationEditorTaskSubmission submission) = 0;
		virtual void RequestCancel(uint64_t task_id) = 0;
		virtual bool IsComplete(uint64_t task_id) const = 0;
		// Completion and exception results are consumed before Join. Join waits
		// for the worker and releases the executor's record for that task.
		virtual std::exception_ptr GetException(uint64_t task_id) const = 0;
		virtual void Join(uint64_t task_id) = 0;
		virtual void CancelAndJoinAll() = 0;
		virtual bool IsIdle() const = 0;
	};

	class VegetationEditorTaskExecutor final :
		public IVegetationEditorTaskExecutor
	{
	public:
		VegetationEditorTaskExecutor();
		~VegetationEditorTaskExecutor() override;

		VegetationEditorTaskExecutor(const VegetationEditorTaskExecutor&) = delete;
		VegetationEditorTaskExecutor& operator=(
			const VegetationEditorTaskExecutor&) = delete;

		uint64_t Submit(VegetationEditorTaskSubmission submission) override;
		void RequestCancel(uint64_t task_id) override;
		bool IsComplete(uint64_t task_id) const override;
		std::exception_ptr GetException(uint64_t task_id) const override;
		void Join(uint64_t task_id) override;
		void CancelAndJoinAll() override;
		bool IsIdle() const override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl{};
	};
}
