#pragma once

#include "Core/EditorEvents.h"

#include "spdlog/common.h"

#include <memory>
#include <mutex>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;

	class EditorLogBridge final
	{
	public:
		EditorLogBridge();
		~EditorLogBridge();

		EditorLogBridge(const EditorLogBridge&) = delete;
		EditorLogBridge& operator=(const EditorLogBridge&) = delete;

	public:
		void Attach(EditorEventBus& refEventBus);
		void Detach();
		void FlushPending();

	private:
		void Enqueue(EditorLogEvent event);

	private:
		class Sink;

		EditorEventBus* _pEventBus = nullptr;
		std::shared_ptr<Sink> _spSink{};
		std::mutex _mutexPending{};
		std::vector<EditorLogEvent> _vecPendingEvents{};
	};
}
