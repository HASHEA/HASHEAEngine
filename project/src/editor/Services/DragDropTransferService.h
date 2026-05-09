#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace AshEditor
{
	using DragDropTransferId = uint64_t;

	struct DragDropTransferData
	{
		std::string strPayloadType{};
		std::vector<uint64_t> vecEntityIds{};
		std::any extraData{};
	};

	class DragDropTransferService final
	{
	public:
		DragDropTransferId Register(DragDropTransferData data);
		const DragDropTransferData* Resolve(DragDropTransferId uTransferId) const;
		void GarbageCollect(bool bDragging);

	private:
		uint64_t _uNextId = 1;
		std::unordered_map<DragDropTransferId, DragDropTransferData> _mapPending{};
	};
}
