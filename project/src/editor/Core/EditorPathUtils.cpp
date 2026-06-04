#include "Core/EditorPathUtils.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace AshEditor
{
	namespace EditorPathUtils
	{
		bool IsSameOrAncestorPath(
			const std::filesystem::path& pathAncestor,
			const std::filesystem::path& pathDescendant)
		{
			const std::filesystem::path pathNormalizedAncestor = pathAncestor.lexically_normal();
			const std::filesystem::path pathNormalizedDescendant = pathDescendant.lexically_normal();
			if (pathNormalizedAncestor.empty())
			{
				return true;
			}

			std::filesystem::path::const_iterator itAncestor = pathNormalizedAncestor.begin();
			std::filesystem::path::const_iterator itDescendant = pathNormalizedDescendant.begin();
			for (; itAncestor != pathNormalizedAncestor.end(); ++itAncestor, ++itDescendant)
			{
				if (itDescendant == pathNormalizedDescendant.end() || *itAncestor != *itDescendant)
				{
					return false;
				}
			}

			return true;
		}

		void SortAndDeduplicatePaths(std::vector<std::filesystem::path>& vecPaths)
		{
			std::sort(
				vecPaths.begin(),
				vecPaths.end(),
				[](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
				{
					return lhs.generic_string() < rhs.generic_string();
				});
			vecPaths.erase(
				std::unique(
					vecPaths.begin(),
					vecPaths.end(),
					[](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
					{
						return lhs.lexically_normal() == rhs.lexically_normal();
					}),
				vecPaths.end());
		}

		std::vector<std::filesystem::path> RemoveNestedDescendantPaths(
			std::vector<std::filesystem::path> vecPaths)
		{
			std::sort(
				vecPaths.begin(),
				vecPaths.end(),
				[](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
				{
					if (lhs == rhs)
					{
						return false;
					}

					const size_t uLhsDepth = static_cast<size_t>(std::distance(lhs.begin(), lhs.end()));
					const size_t uRhsDepth = static_cast<size_t>(std::distance(rhs.begin(), rhs.end()));
					if (uLhsDepth != uRhsDepth)
					{
						return uLhsDepth < uRhsDepth;
					}

					return lhs.generic_string() < rhs.generic_string();
				});

			std::vector<std::filesystem::path> vecFiltered{};
			vecFiltered.reserve(vecPaths.size());
			for (const std::filesystem::path& pathCandidate : vecPaths)
			{
				bool bCoveredByAncestor = false;
				for (const std::filesystem::path& pathKept : vecFiltered)
				{
					if (pathCandidate != pathKept && IsSameOrAncestorPath(pathKept, pathCandidate))
					{
						bCoveredByAncestor = true;
						break;
					}
				}

				if (!bCoveredByAncestor)
				{
					vecFiltered.push_back(pathCandidate);
				}
			}

			return vecFiltered;
		}
	}
}
