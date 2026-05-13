#include "Core/AssetPresentationUtils.h"

#include "Function/Asset/AssetDatabase.h"
#include "Services/AssetDatabaseService.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace AshEditor
{
	namespace
	{
		std::string FormatLocalTime(const std::filesystem::file_time_type& fileTime)
		{
			const auto systemTimePoint = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			const std::time_t rawTime = std::chrono::system_clock::to_time_t(systemTimePoint);

			std::tm localTime{};
#if defined(_WIN32)
			localtime_s(&localTime, &rawTime);
#else
			localtime_r(&rawTime, &localTime);
#endif

			std::ostringstream output{};
			output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
			return output.str();
		}
	}

	std::string GetAssetDisplayLabel(const AshEngine::AssetInfo& refAsset)
	{
		if (!refAsset.name.empty())
		{
			return refAsset.name;
		}

		return refAsset.relative_path.filename().generic_string();
	}

	std::string FormatAssetFileSize(const uint64_t uBytes)
	{
		static constexpr const char* kUnits[] = { "B", "KB", "MB", "GB", "TB" };
		double fDisplaySize = static_cast<double>(uBytes);
		size_t uUnitIndex = 0;
		while (fDisplaySize >= 1024.0 && uUnitIndex + 1 < std::size(kUnits))
		{
			fDisplaySize /= 1024.0;
			++uUnitIndex;
		}

		std::ostringstream output{};
		if (uUnitIndex == 0)
		{
			output << static_cast<uint64_t>(fDisplaySize) << ' ' << kUnits[uUnitIndex];
		}
		else
		{
			output << std::fixed << std::setprecision(1) << fDisplaySize << ' ' << kUnits[uUnitIndex];
		}
		return output.str();
	}

	std::string FormatAssetLastWriteTime(
		const AssetDatabaseService& refAssetDatabaseService,
		const AshEngine::AssetInfo& refAsset)
	{
		const std::filesystem::path pathResolved = refAssetDatabaseService.ResolveAssetPath(refAsset.relative_path);
		if (pathResolved.empty())
		{
			return "Unknown";
		}

		std::error_code error{};
		const std::filesystem::file_time_type fileTime = std::filesystem::last_write_time(pathResolved, error);
		if (error)
		{
			return "Unknown";
		}

		return FormatLocalTime(fileTime);
	}

	bool SupportsTextAssetPreview(const AshEngine::AssetInfo& refAsset)
	{
		switch (refAsset.type)
		{
		case AshEngine::AssetType::Scene:
		case AshEngine::AssetType::Shader:
		case AshEngine::AssetType::Material:
		case AshEngine::AssetType::Text:
			return true;
		default:
			return false;
		}
	}

	std::string BuildTextAssetPreview(const std::string& strSource, const size_t uMaxCharacters)
	{
		if (strSource.size() <= uMaxCharacters)
		{
			return strSource;
		}

		std::string strPreview = strSource.substr(0, uMaxCharacters);
		strPreview += "\n\n... (preview truncated)";
		return strPreview;
	}
}
