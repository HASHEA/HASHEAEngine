#include "Core/EditorEventBus.h"

#include <algorithm>

namespace AshEditor
{
	bool EditorEventBus::Unsubscribe(EditorEventSubscriptionId uSubscriptionId)
	{
		const std::vector<Listener>::iterator it = std::find_if(
			_vecListeners.begin(),
			_vecListeners.end(),
			[uSubscriptionId](const Listener& refListener) { return refListener.uId == uSubscriptionId; });
		if (it == _vecListeners.end())
		{
			return false;
		}

		_vecListeners.erase(it);
		return true;
	}

	void EditorEventBus::Clear()
	{
		_vecListeners.clear();
	}
}
