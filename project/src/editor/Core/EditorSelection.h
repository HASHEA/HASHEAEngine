#pragma once
#include <cstdint>
#include <string>

namespace AshEditor
{
	enum class EditorSelectionKind : uint8_t
	{
		None = 0,
		Entity,
		Asset
	};

	struct EditorSelection
	{
		EditorSelectionKind eKind = EditorSelectionKind::None;
		uint64_t uId = 0;
		std::string strLabel{};
		std::string strPath{};

		bool operator==(const EditorSelection& refOther) const
		{
			return
				eKind == refOther.eKind &&
				uId == refOther.uId &&
				strLabel == refOther.strLabel &&
				strPath == refOther.strPath;
		}

		bool operator!=(const EditorSelection& refOther) const
		{
			return !(*this == refOther);
		}

		bool IsEmpty() const
		{
			return eKind == EditorSelectionKind::None;
		}

		void Clear()
		{
			eKind = EditorSelectionKind::None;
			uId = 0;
			strLabel.clear();
			strPath.clear();
		}
	};
}
