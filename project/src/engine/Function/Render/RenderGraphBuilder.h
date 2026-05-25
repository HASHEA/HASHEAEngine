#pragma once

#include "Function/Render/RenderGraphPass.h"
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	struct RenderGraphCompileResult;

	struct RenderGraphTextureNode
	{
		std::string name{};
		RenderGraphTextureDesc desc{};
		std::shared_ptr<RenderTarget> external_texture = nullptr;
		bool external = false;
		bool extracted = false;
	};

	class RenderGraphBuilder
	{
	public:
		RenderGraphBuilder(Renderer& renderer, const char* name);
		static RenderGraphBuilder create_headless_for_tests(const char* name);

		RenderGraphTextureRef register_external_texture(
			const std::shared_ptr<RenderTarget>& texture,
			const char* name,
			RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
		RenderGraphTextureRef register_external_texture_desc_for_tests(const RenderTargetDesc& desc, const char* name);
		RenderGraphTextureRef create_texture(const RenderGraphTextureDesc& desc, const char* name);
		void extract_texture(RenderGraphTextureRef texture);

		bool add_raster_pass(
			const char* name,
			RenderGraphPassFlags flags,
			const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
			const std::function<bool(RenderGraphRasterContext&)>& execute);

		bool add_compute_pass(
			const char* name,
			RenderGraphPassFlags flags,
			const std::function<void(RenderGraphComputePassBuilder&)>& setup,
			const std::function<bool(RenderGraphComputeContext&)>& execute);

		bool execute();
		bool compile_for_tests(RenderGraphCompileResult& out_result) const;
		bool compile_cached_for_tests(RenderGraphCompileResult& out_result) const;

		size_t get_texture_count_for_tests() const;
		size_t get_pass_count_for_tests() const;
		const std::vector<RenderGraphTextureNode>& get_textures_for_tests() const;
		const std::vector<RenderGraphPassNode>& get_passes_for_tests() const;

	private:
		RenderGraphBuilder(Renderer* renderer, const char* name);

		Renderer* m_renderer = nullptr;
		std::string m_name{};
		std::vector<RenderGraphTextureNode> m_textures{};
		std::vector<RenderGraphPassNode> m_passes{};
	};
}
