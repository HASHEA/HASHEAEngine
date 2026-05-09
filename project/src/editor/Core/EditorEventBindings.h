#pragma once

#include "Core/EditorEventTypes.h"
#include <functional>
#include <utility>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;

	class EditorEventBindings final
	{
	public:
		EditorEventBindings() = default;
		EditorEventBindings(const EditorEventBindings&) = delete;
		EditorEventBindings& operator=(const EditorEventBindings&) = delete;

		~EditorEventBindings();

	public:
		// Binds to the provided event bus (can be nullptr). Existing subscriptions are not moved.
		void Bind(EditorEventBus* pEventBus);

		template<typename EventType, typename Callback>
		// Subscribes to EventType and remembers the subscription id for later Clear().
		// Safe to call multiple times; no-op if not bound.
		void Subscribe(Callback&& fnCallback)
		{
			if (!_pEventBus)
			{
				return;
			}

			const EditorEventSubscriptionId uSubscriptionId =
				_pEventBus->Subscribe<EventType>(std::function<void(const EventType&)>(std::forward<Callback>(fnCallback)));
			if (uSubscriptionId != 0)
			{
				_vecSubscriptionIds.push_back(uSubscriptionId);
			}
		}

		// Unsubscribes all recorded subscriptions and detaches from the event bus.
		void Clear();

		bool IsBoundTo(const EditorEventBus* pEventBus) const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		std::vector<EditorEventSubscriptionId> _vecSubscriptionIds{};
	};
}
