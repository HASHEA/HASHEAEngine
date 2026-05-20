#pragma once

#include "Base/hcore.h"
#include "Function/Render/Material.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AshEngine
{
	enum class EngineShaderFamily : uint8_t
	{
		SurfaceStaticMesh = 0
	};

	enum class PassFamily : uint8_t
	{
		DepthOnly = 0,
		BasePass,
		GBuffer
	};

	enum class MaterialCapability : uint64_t
	{
		None = 0,
		VertexColor = 1ull << 0,
		UV1 = 1ull << 1
	};

	struct ASH_API MaterialUsageDesc
	{
		MaterialDomain domain = MaterialDomain::Surface;
		EngineShaderFamily family = EngineShaderFamily::SurfaceStaticMesh;
		PassFamily pass = PassFamily::BasePass;
		uint64_t capability_mask = 0;

		bool operator==(const MaterialUsageDesc& rhs) const
		{
			return domain == rhs.domain &&
				family == rhs.family &&
				pass == rhs.pass &&
				capability_mask == rhs.capability_mask;
		}

		bool operator!=(const MaterialUsageDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	struct ASH_API EngineShaderFamilyDesc
	{
		EngineShaderFamily family = EngineShaderFamily::SurfaceStaticMesh;
		MaterialDomain domain = MaterialDomain::Surface;
		std::string name{};
		std::string base_pass_shader_path{};
		std::string depth_only_shader_path{};
		std::string gbuffer_shader_path{};
		uint64_t supported_capability_mask = 0;

		std::string_view resolve_host_shader_path(PassFamily pass) const;
	};

	class ASH_API EngineShaderFamilyRegistry
	{
	public:
		bool initialize();
		void shutdown();

		const EngineShaderFamilyDesc* find(EngineShaderFamily family) const;
		bool validate_usage(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			std::string* out_error = nullptr) const;
		uint64_t resolve_required_capability_mask(
			const MaterialInterface& material,
			std::string* out_error = nullptr) const;

		static uint64_t capability_from_name(std::string_view name);

	private:
		std::vector<EngineShaderFamilyDesc> m_families{};
	};

	ASH_API void hash_engine_shader_family_file_signatures(uint64_t& hash_value, EngineShaderFamily family);
}
