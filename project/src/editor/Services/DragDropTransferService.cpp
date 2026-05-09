#include "Services/DragDropTransferService.h"

#include <utility>

namespace AshEditor
{
	DragDropTransferId DragDropTransferService::Register(DragDropTransferData data)
	{
		const DragDropTransferId uId = _uNextId++;
		_mapPending[uId] = std::move(data);
		return uId;
	}

	const DragDropTransferData* DragDropTransferService::Resolve(DragDropTransferId uTransferId) const
	{
		const auto it = _mapPending.find(uTransferId);
		return it != _mapPending.end() ? &it->second : nullptr;
	}

	void DragDropTransferService::GarbageCollect(bool bDragging)
	{
		if (!bDragging && !_mapPending.empty())
		{
			_mapPending.clear();
		}
	}
}
