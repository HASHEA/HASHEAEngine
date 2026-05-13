#include "Core/EditorScenePathUtils.h"

#include "Services/EditorSettingsService.h"

#include <cstdint>
#include <system_error>

namespace AshEditor
{
	namespace
	{
		std::string SanitizeSceneFileStem(std::string_view svSceneName)
		{
			std::string strStem{};
			strStem.reserve(svSceneName.size());
			for (const char chValue : svSceneName)
			{
				switch (chValue)
				{
				case '<':
				case '>':
				case ':':
				case '"':
				case '/':
				case '\\':
				case '|':
				case '?':
				case '*':
					strStem.push_back('_');
					break;
				default:
					strStem.push_back(chValue);
					break;
				}
			}

			const size_t uFirstNonSpace = strStem.find_first_not_of(' ');
			if (uFirstNonSpace == std::string::npos)
			{
				return "Untitled Scene";
			}

			const size_t uLastNonSpace = strStem.find_last_not_of(' ');
			strStem = strStem.substr(uFirstNonSpace, uLastNonSpace - uFirstNonSpace + 1);
			return strStem.empty() ? "Untitled Scene" : strStem;
		}
	}

	std::string MakeScenePathForSettings(
		EditorSettingsService& refSettingsService,
		const std::filesystem::path& refPath)
	{
		if (refPath.empty())
		{
			return {};
		}

		const std::filesystem::path pathWorkspaceRoot = refSettingsService.GetWorkspaceRoot();
		std::error_code errorCode{};
		const std::filesystem::path pathRelative = std::filesystem::relative(refPath, pathWorkspaceRoot, errorCode);
		if (errorCode)
		{
			return refPath.generic_string();
		}

		return pathRelative.generic_string();
	}

	std::filesystem::path MakeUniqueSceneAssetPath(
		EditorSettingsService& refSettingsService,
		std::string_view svSceneName)
	{
		const std::filesystem::path pathSceneDirectory =
			refSettingsService.GetAssetsRootPath() / "scenes";
		const std::string strFileStem = SanitizeSceneFileStem(svSceneName);

		std::error_code errorCode{};
		std::filesystem::create_directories(pathSceneDirectory, errorCode);
		if (errorCode)
		{
			return {};
		}

		std::filesystem::path pathCandidate = pathSceneDirectory / (strFileStem + ".scene.json");
		for (uint32_t uIndex = 2; std::filesystem::exists(pathCandidate, errorCode); ++uIndex)
		{
			if (errorCode)
			{
				return {};
			}

			pathCandidate = pathSceneDirectory / (strFileStem + " " + std::to_string(uIndex) + ".scene.json");
		}

		return errorCode ? std::filesystem::path{} : pathCandidate;
	}
}
