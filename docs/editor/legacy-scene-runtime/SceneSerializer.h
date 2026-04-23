#pragma once
#include <filesystem>

namespace AshEditor
{
	class Scene;

	class SceneSerializer
	{
	public:
		bool save(const Scene& scene, const std::filesystem::path& path) const;
		bool load(const std::filesystem::path& path, Scene& scene) const;
	};
}
