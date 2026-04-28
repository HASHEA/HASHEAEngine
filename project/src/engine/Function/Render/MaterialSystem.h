#pragma once

#include "Base/hcore.h"
#include "Function/Render/EngineShaderFamilyRegistry.h"
#include "Function/Render/MaterialShaderMap.h"
#include <memory>
#include <unordered_map>

namespace AshEngine
{
	class Renderer;

	class ASH_API MaterialSystem
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		const MaterialResource* get_or_create_resource(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			std::string* out_error = nullptr);
		const MaterialInterface* get_domain_fallback(MaterialDomain domain) const;

	private:
		Renderer* m_renderer = nullptr;
		EngineShaderFamilyRegistry m_family_registry{};
		MaterialShaderMap m_shader_map{};
		std::unordered_map<MaterialDomain, std::shared_ptr<MaterialInterface>> m_domain_fallbacks{};
	};
}
