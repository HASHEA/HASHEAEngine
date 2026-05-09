#include "Core/EditorScenePathUtils.h"

#include "Services/EditorSettingsService.h"

namespace AshEditor
{
	std::string MakeScenePathForSettings(
		EditorSettingsService& refSettingsService,
		const std::filesystem::path& refPath)
	{
		if (refPath.empty())
		{
			return {};
		}

		const std::filesystem::path pathWorkspaceRoot = refSettingsService.GetWorkspaceRoot();
		const std::filesystem::path pathRelative = std::filesystem::relative(refPath, pathWorkspaceRoot);
		return pathRelative.generic_string();
	}
}
