#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace AshEditor
{
	class EditorSettingsService;

	std::string MakeScenePathForSettings(
		EditorSettingsService& refSettingsService,
		const std::filesystem::path& refPath);

	std::filesystem::path MakeUniqueSceneAssetPath(
		EditorSettingsService& refSettingsService,
		std::string_view svSceneName);
}
