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
		EditorSelectionKind kind = EditorSelectionKind::None;
		uint64_t id = 0;
		std::string label{};
		std::string path{};

		bool operator==(const EditorSelection& other) const
		{
			return kind == other.kind && id == other.id && label == other.label && path == other.path;
		}

		bool operator!=(const EditorSelection& other) const
		{
			return !(*this == other);
		}

		bool is_empty() const
		{
			return kind == EditorSelectionKind::None;
		}

		void clear()
		{
			kind = EditorSelectionKind::None;
			id = 0;
			label.clear();
			path.clear();
		}
	};
}
