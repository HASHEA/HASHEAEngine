#pragma once

#include <filesystem>
#include <string>

namespace AshEditor
{
	class EditorSettingsService;

	std::string MakeScenePathForSettings(
		EditorSettingsService& refSettingsService,
		const std::filesystem::path& refPath);
}
