#pragma once

#include "Core/EditorEventTypes.h"

#include <functional>
#include <typeindex>
#include <utility>
#include <vector>

namespace AshEditor
{
	class EditorEventBus
	{
	public:
		// A lightweight synchronous event bus used by the editor UI. Not thread-safe.
		// Subscribes a callback for a specific event type.
		// Returns a subscription id that must be passed to Unsubscribe(), or 0 if subscription failed.
		template<typename EventType>
		EditorEventSubscriptionId Subscribe(std::function<void(const EventType&)> fnCallback)
		{
			if (!fnCallback)
			{
				return 0;
			}

			const EditorEventSubscriptionId uSubscriptionId = _uNextSubscriptionId++;
			_vecListeners.push_back(Listener{
				uSubscriptionId,
				std::type_index(typeid(EventType)),
				[fnHandler = std::move(fnCallback)](const void* pEvent)
				{
					fnHandler(*static_cast<const EventType*>(pEvent));
				}
			});
			return uSubscriptionId;
		}

		// Unsubscribes a previously registered handler. Returns true if the id existed.
		bool Unsubscribe(EditorEventSubscriptionId uSubscriptionId);

		// Publishes an event to current subscribers. Callbacks run synchronously on the calling thread.
		template<typename EventType>
		void Publish(const EventType& refEvent) const
		{
			const std::type_index typeEvent{ typeid(EventType) };
			std::vector<EditorEventSubscriptionId> vecListenerIds{};
			vecListenerIds.reserve(_vecListeners.size());
			for (const Listener& refListener : _vecListeners)
			{
				if (refListener.typeEvent == typeEvent)
				{
					vecListenerIds.push_back(refListener.uId);
				}
			}

			for (const EditorEventSubscriptionId uListenerId : vecListenerIds)
			{
				for (const Listener& refListener : _vecListeners)
				{
					if (refListener.uId == uListenerId && refListener.fnDispatch)
					{
						refListener.fnDispatch(&refEvent);
						break;
					}
				}
			}
		}

		// Removes all listeners.
		void Clear();

	private:
		struct Listener
		{
			EditorEventSubscriptionId uId = 0;
			std::type_index typeEvent{ typeid(void) };
			std::function<void(const void*)> fnDispatch{};
		};

	private:
		std::vector<Listener> _vecListeners{};
		EditorEventSubscriptionId _uNextSubscriptionId = 1;
	};
}
