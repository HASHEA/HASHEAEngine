#include "Function/Render/RenderDebugView.h"

#include "Base/IniConfig.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_render_debug_view_shader_path =
			"project/src/engine/Shaders/Debug/RenderDebugView.hlsl";
		static constexpr const char* k_vertex_decl_locations_shader_path =
			"project/src/engine/Graphics/Shaders/AshVertexDeclLocations.hlsli";

		struct RenderDebugViewRootConstants
		{
			glm::vec4 params0{ 0.0f, 0.0f, 0.0f, 32.0f };
			glm::vec4 params1{ 0.0f };
		};

		static_assert(sizeof(RenderDebugViewRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto normalize_token(std::string value) -> std::string
		{
			value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			}), value.end());
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		auto runtime_config_storage() -> RenderDebugViewConfig&
		{
			static RenderDebugViewConfig config = make_default_render_debug_view_config();
			return config;
		}

		auto runtime_config_mutex() -> std::mutex&
		{
			static std::mutex mutex;
			return mutex;
		}

		auto build_shader_source_hash() -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, k_render_debug_view_shader_path);
			RHI::hash_shader_file_signature(hash_value, k_vertex_decl_locations_shader_path);
			return hash_value;
		}

		auto output_needs_manual_srgb_encode(RenderTextureFormat format) -> bool
		{
			switch (format)
			{
			case RenderTextureFormat::RGBA8_UNORM:
			case RenderTextureFormat::BGRA8_UNORM:
				return true;
			default:
				return false;
			}
		}

		auto make_program_desc() -> GraphicsProgramDesc
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::None;
			state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			state.depth_test = false;
			state.depth_write = false;
			state.blend_mode = RenderBlendMode::Opaque;

			GraphicsProgramDesc desc{};
			desc.shader_path = k_render_debug_view_shader_path;
			desc.base_shader_path = k_render_debug_view_shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_shader_source_hash();
			desc.name = "SceneRenderDebugView";
			desc.state = state;
			return desc;
		}

		void attach_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const RenderDebugViewRootConstants& constants)
		{
			RHI::ShaderParameterBlockLayout layout{};
			if (!program || !program->get_parameter_block_layout("AshRootConstants", layout) || layout.byte_size == 0)
			{
				return;
			}

			draw_desc.const_data_size = std::min<uint32_t>(
				static_cast<uint32_t>(sizeof(constants)),
				std::min<uint32_t>(layout.byte_size, GraphicsDrawDesc::InlineConstDataCapacity));
			draw_desc.inline_const_data_valid = true;
			std::memcpy(draw_desc.inline_const_data.data(), &constants, draw_desc.const_data_size);
		}

		void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext* view_context)
		{
			if (!view_context)
			{
				return;
			}
			draw_desc.reverse_z = view_context->reverse_z;
			draw_desc.has_viewport = view_context->has_viewport;
			if (view_context->has_viewport)
			{
				draw_desc.viewport = view_context->viewport;
			}
			draw_desc.has_scissor = view_context->has_scissor;
			if (view_context->has_scissor)
			{
				draw_desc.scissor = view_context->scissor;
			}
		}

		auto make_root_constants(
			RenderDebugVisualization visualization,
			bool reverse_z,
			bool manual_srgb_encode) -> RenderDebugViewRootConstants
		{
			RenderDebugViewRootConstants constants{};
			constants.params0.x = static_cast<float>(static_cast<uint8_t>(visualization));
			constants.params0.y = reverse_z ? 1.0f : 0.0f;
			constants.params0.z = manual_srgb_encode ? 1.0f : 0.0f;
			constants.params0.w = 32.0f;
			return constants;
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const RenderDebugViewRootConstants& constants,
			const SceneRenderViewContext* view_context) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.vertex_count = 3u;
			draw_desc.instance_count = 1u;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}
	}

	void RenderDebugViewFrameRegistry::begin_frame()
	{
		m_items.clear();
	}

	void RenderDebugViewFrameRegistry::register_item(const RenderDebugViewItem& item)
	{
		if (item.name.empty() || !item.texture)
		{
			return;
		}

		for (RenderDebugViewItem& existing : m_items)
		{
			if (existing.name == item.name)
			{
				existing = item;
				return;
			}
		}
		m_items.push_back(item);
	}

	const RenderDebugViewItem* RenderDebugViewFrameRegistry::find_item(const std::string& name) const
	{
		for (const RenderDebugViewItem& item : m_items)
		{
			if (item.name == name)
			{
				return &item;
			}
		}
		return nullptr;
	}

	const std::vector<RenderDebugViewItem>& RenderDebugViewFrameRegistry::get_items() const
	{
		return m_items;
	}

	const char* render_debug_visualization_name(RenderDebugVisualization visualization)
	{
		switch (visualization)
		{
		case RenderDebugVisualization::LinearHDR:
			return "LinearHDR";
		case RenderDebugVisualization::Depth:
			return "Depth";
		case RenderDebugVisualization::Normal:
			return "Normal";
		case RenderDebugVisualization::MotionVector:
			return "MotionVector";
		case RenderDebugVisualization::AO:
			return "AO";
		case RenderDebugVisualization::Scalar:
			return "Scalar";
		case RenderDebugVisualization::Color:
		default:
			return "Color";
		}
	}

	const char* render_debug_texture_format_name(RenderTextureFormat format)
	{
		switch (format)
		{
		case RenderTextureFormat::RGBA8_UNORM:
			return "RGBA8_UNORM";
		case RenderTextureFormat::RGBA8_SRGB:
			return "RGBA8_SRGB";
		case RenderTextureFormat::BGRA8_UNORM:
			return "BGRA8_UNORM";
		case RenderTextureFormat::BGRA8_SRGB:
			return "BGRA8_SRGB";
		case RenderTextureFormat::RGBA16_SFLOAT:
			return "RGBA16_SFLOAT";
		case RenderTextureFormat::RGBA32_SFLOAT:
			return "RGBA32_SFLOAT";
		case RenderTextureFormat::D24_UNORM_S8_UINT:
			return "D24_UNORM_S8_UINT";
		case RenderTextureFormat::D32_SFLOAT:
			return "D32_SFLOAT";
		case RenderTextureFormat::BC1_RGB_UNORM:
			return "BC1_RGB_UNORM";
		case RenderTextureFormat::BC1_RGB_SRGB_UNORM:
			return "BC1_RGB_SRGB_UNORM";
		case RenderTextureFormat::BC1_RGBA_UNORM:
			return "BC1_RGBA_UNORM";
		case RenderTextureFormat::BC1_RGBA_SRGB_UNORM:
			return "BC1_RGBA_SRGB_UNORM";
		case RenderTextureFormat::BC2_UNORM:
			return "BC2_UNORM";
		case RenderTextureFormat::BC2_SRGB_UNORM:
			return "BC2_SRGB_UNORM";
		case RenderTextureFormat::BC3_UNORM:
			return "BC3_UNORM";
		case RenderTextureFormat::BC3_SRGB_UNORM:
			return "BC3_SRGB_UNORM";
		case RenderTextureFormat::BC4_UNORM:
			return "BC4_UNORM";
		case RenderTextureFormat::BC4_SNORM:
			return "BC4_SNORM";
		case RenderTextureFormat::BC5_UNORM:
			return "BC5_UNORM";
		case RenderTextureFormat::BC5_SNORM:
			return "BC5_SNORM";
		case RenderTextureFormat::BC6H_UFLOAT:
			return "BC6H_UFLOAT";
		case RenderTextureFormat::BC6H_SFLOAT:
			return "BC6H_SFLOAT";
		case RenderTextureFormat::BC7_UNORM:
			return "BC7_UNORM";
		case RenderTextureFormat::BC7_SRGB_UNORM:
			return "BC7_SRGB_UNORM";
		case RenderTextureFormat::Unknown:
		default:
			return "Unknown";
		}
	}

	RenderDebugViewConfig make_default_render_debug_view_config()
	{
		return RenderDebugViewConfig{};
	}

	RenderDebugViewConfig load_runtime_render_debug_view_config(const char* config_path)
	{
		RenderDebugViewConfig config = make_default_render_debug_view_config();
		IniConfig ini_config{};
		if (!ini_config.load(config_path))
		{
			HLogInfo(
				"Render debug view config file '{}' was not found. Using default render debug view config.",
				resolve_runtime_config_path(config_path).string());
			return config;
		}

		if (ini_config.has_value("RenderDebugView", "Enabled"))
		{
			bool enabled = config.enabled;
			if (ini_config.try_get_bool("RenderDebugView", "Enabled", enabled))
			{
				config.enabled = enabled;
			}
			else
			{
				HLogWarning("RenderDebugView.Enabled is invalid. Keeping default '{}'.", config.enabled);
			}
		}

		if (ini_config.has_value("RenderDebugView", "Selected"))
		{
			config.selected = ini_config.get_string("RenderDebugView", "Selected", config.selected.c_str());
			if (config.selected.empty())
			{
				config.selected = "Off";
			}
		}

		HLogInfo(
			"Runtime render debug view config loaded. enabled={} selected={}.",
			config.enabled,
			config.selected);
		return config;
	}

	void set_runtime_render_debug_view_config(const RenderDebugViewConfig& config)
	{
		std::lock_guard<std::mutex> lock(runtime_config_mutex());
		runtime_config_storage() = config;
	}

	RenderDebugViewConfig get_runtime_render_debug_view_config()
	{
		std::lock_guard<std::mutex> lock(runtime_config_mutex());
		return runtime_config_storage();
	}

	bool RenderDebugView::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("RenderDebugView::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(create_resources(*m_renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderDebugView::create_resources(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("RenderDebugView::create_resources", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(sampler_desc, "RenderDebugViewPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		m_program = renderer.create_graphics_program(make_program_desc());
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void RenderDebugView::shutdown()
	{
		m_program.reset();
		m_point_clamp_sampler.reset();
		m_registry.begin_frame();
		m_last_selected_available = false;
		m_last_selected.clear();
		m_renderer = nullptr;
	}

	void RenderDebugView::begin_frame()
	{
		m_registry.begin_frame();
		m_last_selected_available = false;
		m_last_selected = get_runtime_render_debug_view_config().selected;
	}

	void RenderDebugView::register_item(const RenderDebugViewItem& item)
	{
		m_registry.register_item(item);
	}

	bool RenderDebugView::should_bypass_debug_pass(const std::string& selected)
	{
		const std::string token = normalize_token(selected);
		return token.empty() || token == "off" || token == "none" || token == "sceneoutput";
	}

	bool RenderDebugView::add_pass_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef selected,
		RenderGraphTextureRef output)
	{
		if (!selected || !output || selected == output)
		{
			return true;
		}

		return graph.add_raster_pass(
			"SceneRenderDebugViewPass",
			RenderGraphPassFlags::None,
			[selected, output](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(selected, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, output, RenderLoadAction::Load, {});
			},
			[](RenderGraphRasterContext&)
			{
				return true;
			});
	}

	bool RenderDebugView::add_pass(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("RenderDebugView::add_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const RenderDebugViewConfig config = get_runtime_render_debug_view_config();
		m_last_selected = config.selected;
		m_last_selected_available = false;
		if (!config.enabled || should_bypass_debug_pass(config.selected))
		{
			break;
		}

		const RenderDebugViewItem* item = m_registry.find_item(config.selected);
		if (!item)
		{
			break;
		}

		m_last_selected_available = true;
		ASH_PROCESS_ERROR(add_pass_internal(graph, *item, output_target, &view_context));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderDebugView::add_pass_internal(
		RenderGraphBuilder& graph,
		const RenderDebugViewItem& item,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext* view_context)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_program && m_point_clamp_sampler);
		ASH_PROCESS_ERROR(item.texture);
		ASH_PROCESS_ERROR(output_target);
		if (item.texture == output_target)
		{
			break;
		}

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneRenderDebugViewPass",
			RenderGraphPassFlags::None,
			[item, output_target](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(item.texture, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, output_target, RenderLoadAction::Load, {});
			},
			[this, item, output_target, view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneRenderDebugViewPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> input = context.get_texture(item.texture);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(input && output);
				const bool reverse_z = view_context ? view_context->reverse_z : false;
				const bool manual_srgb = output_needs_manual_srgb_encode(output->get_format());
				const RenderDebugViewRootConstants constants =
					make_root_constants(item.visualization, reverse_z, manual_srgb);
				ASH_PROCESS_ERROR(m_program->set_texture("RenderDebugInput", input));
				ASH_PROCESS_ERROR(m_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void RenderDebugView::draw_ui(UIContext& ui_context)
	{
		const RenderDebugViewConfig config = get_runtime_render_debug_view_config();
		if (!config.enabled)
		{
			return;
		}

		ui_context.set_next_window_position({ 10.0f, 96.0f }, UIConditionFlagBits::FirstUseEver);
		ui_context.set_next_window_size({ 340.0f, 0.0f }, UIConditionFlagBits::FirstUseEver);
		const UIWindowFlags flags =
			UIWindowFlagBits::NoDocking |
			UIWindowFlagBits::AlwaysAutoResize;
		const bool visible = ui_context.begin_window("Render Debug View", nullptr, flags);
		if (visible)
		{
			std::vector<std::string> items{};
			items.reserve(m_registry.get_items().size() + 1u);
			items.push_back("Off");
			for (const RenderDebugViewItem& item : m_registry.get_items())
			{
				items.push_back(item.name);
			}

			int32_t selected_index = 0;
			for (int32_t index = 0; index < static_cast<int32_t>(items.size()); ++index)
			{
				if (items[static_cast<size_t>(index)] == config.selected)
				{
					selected_index = index;
					break;
				}
			}

			ui_context.set_next_item_width(260.0f);
			if (ui_context.combo("RT", selected_index, items))
			{
				RenderDebugViewConfig updated = config;
				updated.selected = items[static_cast<size_t>(selected_index)];
				set_runtime_render_debug_view_config(updated);
			}

			const RenderDebugViewConfig latest = get_runtime_render_debug_view_config();
			const RenderDebugViewItem* selected_item = m_registry.find_item(latest.selected);
			if (should_bypass_debug_pass(latest.selected))
			{
				ui_context.text("Selected: %s", latest.selected.c_str());
				ui_context.text("Status: normal output");
			}
			else if (selected_item)
			{
				ui_context.text("Selected: %s", selected_item->display_name.empty() ? selected_item->name.c_str() : selected_item->display_name.c_str());
				ui_context.text(
					"%ux%u  %s  %s",
					selected_item->width,
					selected_item->height,
					render_debug_texture_format_name(selected_item->format),
					render_debug_visualization_name(selected_item->visualization));
			}
			else
			{
				ui_context.text("Selected: %s", latest.selected.c_str());
				ui_context.text("Status: unavailable this frame");
			}

			if (!m_last_selected.empty() && !should_bypass_debug_pass(m_last_selected) && !m_last_selected_available)
			{
				ui_context.text("Last render: unavailable");
			}
		}
		ui_context.end_window();
	}
}
