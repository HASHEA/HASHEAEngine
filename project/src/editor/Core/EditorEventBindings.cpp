#include "Core/EditorEventBindings.h"

#include "Core/EditorEventBus.h"

namespace AshEditor
{
	EditorEventBindings::~EditorEventBindings()
	{
		Clear();
	}

	void EditorEventBindings::Bind(EditorEventBus* pEventBus)
	{
		if (_pEventBus == pEventBus)
		{
			return;
		}

		Clear();
		_pEventBus = pEventBus;
	}

	void EditorEventBindings::Clear()
	{
		if (_pEventBus)
		{
			for (const EditorEventSubscriptionId uSubscriptionId : _vecSubscriptionIds)
			{
				if (uSubscriptionId != 0)
				{
					_pEventBus->Unsubscribe(uSubscriptionId);
				}
			}
		}

		_vecSubscriptionIds.clear();
		_pEventBus = nullptr;
	}

	bool EditorEventBindings::IsBoundTo(const EditorEventBus* pEventBus) const
	{
		return _pEventBus == pEventBus;
	}
}
