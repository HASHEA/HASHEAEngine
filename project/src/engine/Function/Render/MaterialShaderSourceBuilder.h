#pragma once

#include "Base/hcore.h"
#include "Function/Render/EngineShaderFamilyRegistry.h"
#include "Function/Render/Material.h"
#include <filesystem>

namespace AshEngine
{
	struct ASH_API MaterialGeneratedSourcePaths
	{
		std::filesystem::path bindings_include_path{};
		uint64_t combined_source_hash = 0;
	};

	class ASH_API MaterialShaderSourceBuilder
	{
	public:
		MaterialGeneratedSourcePaths build_source(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			const EngineShaderFamilyRegistry& family_registry,
			const std::filesystem::path& output_root,
			std::string* out_error = nullptr) const;
	};
}
