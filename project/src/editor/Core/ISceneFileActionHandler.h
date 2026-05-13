#pragma once

#include <filesystem>

namespace AshEditor
{
	class ISceneFileActionHandler
	{
	public:
		virtual ~ISceneFileActionHandler() = default;

		virtual bool OpenSceneFromDialog(const char* pSource) = 0;
		virtual bool OpenSceneFromPath(const std::filesystem::path& pathScene, const char* pSource) = 0;
	};
}
