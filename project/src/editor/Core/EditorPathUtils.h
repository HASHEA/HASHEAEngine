#pragma once

#include <filesystem>
#include <vector>

namespace AshEditor
{
	namespace EditorPathUtils
	{
		bool IsSameOrAncestorPath(
			const std::filesystem::path& pathAncestor,
			const std::filesystem::path& pathDescendant);
		void SortAndDeduplicatePaths(std::vector<std::filesystem::path>& vecPaths);
		// Keeps only the highest-level paths so batch folder operations do not repeat work for descendants.
		std::vector<std::filesystem::path> RemoveNestedDescendantPaths(
			std::vector<std::filesystem::path> vecPaths);
	}
}
