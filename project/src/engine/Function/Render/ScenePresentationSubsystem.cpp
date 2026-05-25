#include "Function/Render/ScenePresentationSubsystem.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Function/Render/Material.h"
#include "Function/Render/MaterialRenderProxy.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneRenderView.h"
#include "Function/Render/SceneRenderer.h"
#include "Function/Render/SceneView.h"
#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	class ScenePresentationSubsystem::Impl
	{
	public:
		struct OutputState
		{
			SceneOutputHandle handle{};
			UISurfaceHandle surface{};
			std::string debug_name{};
			SceneOutputKind kind = SceneOutputKind::Offscreen;
			uint32_t width = 1;
			uint32_t height = 1;
			SceneOutputFormat format = SceneOutputFormat::Auto;
			bool srgb = true;
			std::shared_ptr<RenderTarget> render_target = nullptr;
			uint32_t allocated_width = 0;
			uint32_t allocated_height = 0;
			uint32_t allocated_format = 0;
		};

		struct BindingState
		{
			SceneViewBindingHandle handle{};
			std::string debug_name{};
			Scene* scene = nullptr;
			SceneCameraSelector camera{};
			SceneOutputHandle output{};
			SceneViewOverrides overrides{};
			bool enabled = true;
			int32_t sort_order = 0;
			bool refresh_requested = true;
		};

		struct SceneState
		{
			Scene* scene = nullptr;
			uint64_t last_primitive_version = 0;
			uint64_t last_transform_version = 0;
			uint64_t last_light_version = 0;
			bool render_scene_valid = false;
			RenderScene render_scene{};
		};

		struct PreparedPacket
		{
			SceneViewBindingHandle binding{};
			SceneOutputHandle output{};
			std::string debug_name{};
			SceneViewOverrides overrides{};
			uint32_t output_width = 1;
			uint32_t output_height = 1;
			int32_t sort_order = 0;
			std::shared_ptr<VisibleRenderFrame> visible_frame = nullptr;
		};

	public:
		Renderer* renderer = nullptr;
		RenderAssetManager* render_asset_manager = nullptr;
		SceneRenderer* scene_renderer = nullptr;
		mutable std::mutex state_mutex{};
		std::unordered_map<uint32_t, OutputState> outputs{};
		std::unordered_map<uint32_t, BindingState> bindings{};
		std::unordered_map<Scene*, SceneState> scene_states{};
		std::vector<PreparedPacket> prepared_packets{};
		std::unordered_set<uint32_t> logged_binding_submits{};
		uint32_t next_output_id = 1;
		uint32_t next_binding_id = 1;
		uint32_t next_surface_id = 1;

	public:
		static void apply_output_desc(OutputState& state, const SceneOutputDesc& desc);
		static void apply_binding_desc(BindingState& state, const SceneViewBindingDesc& desc);
		static bool resolve_output_extent(const OutputState& output_state, uint32_t& out_width, uint32_t& out_height);
		static bool build_binding_scene_view(const BindingState& binding_state, uint32_t output_width, uint32_t output_height, SceneView& out_view);
	};

	namespace
	{
		static auto make_debug_name(const char* value, const char* fallback) -> std::string
		{
			return (value != nullptr && value[0] != '\0') ? std::string(value) : std::string(fallback);
		}

		static auto clamp_output_extent(uint32_t value) -> uint32_t
		{
			return std::max<uint32_t>(1u, std::min<uint32_t>(value, static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
		}

		static auto resolve_output_format(
			SceneOutputKind kind,
			SceneOutputFormat format,
			bool srgb,
			const std::shared_ptr<RenderTarget>& back_buffer) -> RenderTextureFormat
		{
			switch (format)
			{
			case SceneOutputFormat::SRGB8:
				return RenderTextureFormat::RGBA8_SRGB;
			case SceneOutputFormat::RGBA16F:
				return RenderTextureFormat::RGBA16_SFLOAT;
			case SceneOutputFormat::Auto:
			default:
				if (kind == SceneOutputKind::Window && back_buffer)
				{
					return back_buffer->get_format();
				}
				return srgb ? RenderTextureFormat::RGBA8_SRGB : RenderTextureFormat::RGBA8_UNORM;
			}
		}

		static auto resolve_load_action(SceneClearMode clear_mode, bool first_use_on_output) -> RenderLoadAction
		{
			switch (clear_mode)
			{
			case SceneClearMode::Clear:
				return RenderLoadAction::Clear;
			case SceneClearMode::Preserve:
				return RenderLoadAction::Load;
			case SceneClearMode::DontCare:
				return RenderLoadAction::DontCare;
			case SceneClearMode::Default:
			default:
				return first_use_on_output ? RenderLoadAction::Clear : RenderLoadAction::Load;
			}
		}

		static auto rect_is_valid(const ScenePixelRect& rect, uint32_t output_width, uint32_t output_height) -> bool
		{
			if (rect.width == 0 ||
				rect.height == 0 ||
				rect.x < 0 ||
				rect.y < 0 ||
				rect.x > std::numeric_limits<int16_t>::max() ||
				rect.y > std::numeric_limits<int16_t>::max())
			{
				return false;
			}

			const uint32_t right = static_cast<uint32_t>(rect.x) + rect.width;
			const uint32_t bottom = static_cast<uint32_t>(rect.y) + rect.height;
			return right <= output_width && bottom <= output_height;
		}

		static bool prepare_visible_frame_material_proxies(
			VisibleRenderFrame& frame,
			RenderAssetManager& asset_manager,
			Renderer& renderer)
		{
			ASH_PROFILE_SCOPE_NC("ScenePresentation::PrepareMaterialProxies", AshEngine::Profile::Color::Scene);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.static_mesh_draws.size()));
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			uint64_t prepared_section_count = 0;
			for (VisibleStaticMeshDraw& draw : frame.static_mesh_draws)
			{
				for (ResolvedStaticMeshSection& section : draw.sections)
				{
					++prepared_section_count;
					if (!section.material)
					{
						section.material = asset_manager.request_material_asset(k_builtin_default_surface_material_path);
					}
					if (!section.material)
					{
						HLogError("ScenePresentationSubsystem: static mesh section is missing a resolved material.");
						ASH_PROCESS_ERROR(false);
					}

					if (section.material_proxy && !section.material_proxy->needs_surface_staticmesh_preparation())
					{
						continue;
					}

					std::shared_ptr<MaterialRenderProxy> material_proxy = section.material_proxy;
					if (!material_proxy)
					{
						material_proxy = asset_manager.request_material_render_proxy(section.material);
					}
					if (!material_proxy)
					{
						HLogError(
							"ScenePresentationSubsystem: failed to request MaterialRenderProxy for '{}'.",
							section.material->get_asset_path().generic_string());
						ASH_PROCESS_ERROR(false);
					}

					if (!material_proxy->prepare_surface_staticmesh(asset_manager, renderer))
					{
						HLogError(
							"ScenePresentationSubsystem: failed to prepare MaterialRenderProxy for '{}'.",
							section.material->get_asset_path().generic_string());
						ASH_PROCESS_ERROR(false);
					}

					section.material_proxy = std::move(material_proxy);
				}
			}

			ASH_PROFILE_PLOT("Scene/PreparedMaterialSections", static_cast<int64_t>(prepared_section_count));
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

	}

	void ScenePresentationSubsystem::Impl::apply_output_desc(OutputState& state, const SceneOutputDesc& desc)
	{
		state.debug_name = make_debug_name(desc.debug_name, "SceneOutput");
		state.kind = desc.kind;
		state.width = clamp_output_extent(desc.width);
		state.height = clamp_output_extent(desc.height);
		state.format = desc.format;
		state.srgb = desc.srgb;
	}

	void ScenePresentationSubsystem::Impl::apply_binding_desc(BindingState& state, const SceneViewBindingDesc& desc)
	{
		state.debug_name = make_debug_name(desc.debug_name, "SceneViewBinding");
		state.scene = desc.scene;
		state.camera = desc.camera;
		state.output = desc.output;
		state.overrides = desc.overrides;
		state.enabled = desc.enabled;
		state.sort_order = desc.sort_order;
	}

	bool ScenePresentationSubsystem::Impl::resolve_output_extent(
		const OutputState& output_state,
		uint32_t& out_width,
		uint32_t& out_height)
	{
		out_width = 0;
		out_height = 0;
		if (output_state.kind == SceneOutputKind::Window)
		{
			Window* window = Application::get() ? Application::get_window() : nullptr;
			if (!window)
			{
				return false;
			}

			out_width = clamp_output_extent(window->get_width());
			out_height = clamp_output_extent(window->get_height());
			return out_width > 0 && out_height > 0;
		}

		out_width = clamp_output_extent(output_state.width);
		out_height = clamp_output_extent(output_state.height);
		return out_width > 0 && out_height > 0;
	}

	bool ScenePresentationSubsystem::Impl::build_binding_scene_view(
		const BindingState& binding_state,
		uint32_t output_width,
		uint32_t output_height,
		SceneView& out_view)
	{
		if (binding_state.scene == nullptr || !binding_state.scene->is_valid())
		{
			return false;
		}

		SceneViewDesc view_desc{};
		view_desc.viewport_width = output_width;
		view_desc.viewport_height = output_height;

		switch (binding_state.camera.source)
		{
		case SceneCameraSource::Override:
			return binding_state.camera.override_view.enabled &&
				build_scene_view_from_matrices(
					view_desc,
					binding_state.camera.override_view.view,
					binding_state.camera.override_view.projection,
					binding_state.camera.override_view.camera_position,
					binding_state.camera.override_view.reverse_z,
					out_view);
		case SceneCameraSource::EntityId:
			return binding_state.camera.entity_id != 0 &&
				build_scene_view_for_camera_entity(*binding_state.scene, binding_state.camera.entity_id, view_desc, out_view);
		case SceneCameraSource::PrimaryCamera:
		default:
			return build_primary_scene_view(*binding_state.scene, view_desc, out_view);
		}
	}

	ScenePresentationSubsystem::ScenePresentationSubsystem()
		: m_impl(std::make_unique<Impl>())
	{
	}

	ScenePresentationSubsystem::~ScenePresentationSubsystem() = default;

	bool ScenePresentationSubsystem::initialize(
		Renderer* renderer,
		RenderAssetManager* render_asset_manager,
		SceneRenderer* scene_renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl != nullptr);

		m_impl->renderer = renderer;
		m_impl->render_asset_manager = render_asset_manager;
		m_impl->scene_renderer = scene_renderer;
		bResult =
			m_impl->renderer != nullptr &&
			m_impl->render_asset_manager != nullptr &&
			m_impl->scene_renderer != nullptr;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void ScenePresentationSubsystem::shutdown()
	{
		if (!m_impl)
		{
			return;
		}

		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		m_impl->prepared_packets.clear();
		m_impl->scene_states.clear();
		m_impl->bindings.clear();
		m_impl->outputs.clear();
		m_impl->logged_binding_submits.clear();
		m_impl->next_output_id = 1;
		m_impl->next_binding_id = 1;
		m_impl->next_surface_id = 1;
		m_impl->scene_renderer = nullptr;
		m_impl->render_asset_manager = nullptr;
		m_impl->renderer = nullptr;
	}

	SceneOutputHandle ScenePresentationSubsystem::create_output(const SceneOutputDesc& desc)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		Impl::OutputState state{};
		state.handle.value = m_impl->next_output_id++;
		Impl::apply_output_desc(state, desc);
		if (state.kind == SceneOutputKind::Offscreen)
		{
			state.surface.value = m_impl->next_surface_id++;
		}
		const SceneOutputHandle handle = state.handle;
		m_impl->outputs.emplace(handle.value, std::move(state));
		return handle;
	}

	bool ScenePresentationSubsystem::update_output(SceneOutputHandle handle, const SceneOutputDesc& desc)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		const auto found = m_impl->outputs.find(handle.value);
		if (found == m_impl->outputs.end())
		{
			return false;
		}

		const UISurfaceHandle existing_surface = found->second.surface;
		const std::shared_ptr<RenderTarget> existing_target = found->second.render_target;
		const uint32_t existing_allocated_width = found->second.allocated_width;
		const uint32_t existing_allocated_height = found->second.allocated_height;
		const uint32_t existing_allocated_format = found->second.allocated_format;
		Impl::apply_output_desc(found->second, desc);
		found->second.surface = existing_surface;
		found->second.render_target = existing_target;
		found->second.allocated_width = existing_allocated_width;
		found->second.allocated_height = existing_allocated_height;
		found->second.allocated_format = existing_allocated_format;
		if (found->second.kind == SceneOutputKind::Window)
		{
			found->second.surface = {};
			found->second.render_target.reset();
			found->second.allocated_width = 0;
			found->second.allocated_height = 0;
			found->second.allocated_format = 0;
		}
		else if (!found->second.surface.is_valid())
		{
			found->second.surface.value = m_impl->next_surface_id++;
		}
		return true;
	}

	void ScenePresentationSubsystem::destroy_output(SceneOutputHandle handle)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		m_impl->outputs.erase(handle.value);
	}

	SceneViewBindingHandle ScenePresentationSubsystem::create_view_binding(const SceneViewBindingDesc& desc)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		Impl::BindingState state{};
		state.handle.value = m_impl->next_binding_id++;
		Impl::apply_binding_desc(state, desc);
		const SceneViewBindingHandle handle = state.handle;
		m_impl->bindings.emplace(handle.value, std::move(state));
		return handle;
	}

	bool ScenePresentationSubsystem::update_view_binding(SceneViewBindingHandle handle, const SceneViewBindingDesc& desc)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		const auto found = m_impl->bindings.find(handle.value);
		if (found == m_impl->bindings.end())
		{
			return false;
		}

		Impl::apply_binding_desc(found->second, desc);
		found->second.refresh_requested = true;
		return true;
	}

	void ScenePresentationSubsystem::destroy_view_binding(SceneViewBindingHandle handle)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		m_impl->bindings.erase(handle.value);
	}

	bool ScenePresentationSubsystem::set_binding_enabled(SceneViewBindingHandle handle, bool enabled)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		const auto found = m_impl->bindings.find(handle.value);
		if (found == m_impl->bindings.end())
		{
			return false;
		}

		found->second.enabled = enabled;
		return true;
	}

	bool ScenePresentationSubsystem::request_refresh(SceneViewBindingHandle handle)
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		const auto found = m_impl->bindings.find(handle.value);
		if (found == m_impl->bindings.end())
		{
			return false;
		}

		found->second.refresh_requested = true;
		return true;
	}

	UISurfaceHandle ScenePresentationSubsystem::get_ui_surface(SceneOutputHandle handle) const
	{
		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		const auto found = m_impl->outputs.find(handle.value);
		return found != m_impl->outputs.end() ? found->second.surface : UISurfaceHandle{};
	}

	bool ScenePresentationSubsystem::update_presentations()
	{
		ASH_PROFILE_SCOPE_NC("ScenePresentationSubsystem::update_presentations", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl != nullptr);
		ASH_PROCESS_ERROR(m_impl->renderer != nullptr);
		ASH_PROCESS_ERROR(m_impl->render_asset_manager != nullptr);
		ASH_PROCESS_ERROR(m_impl->scene_renderer != nullptr);

		std::vector<Impl::BindingState> bindings{};
		std::unordered_map<uint32_t, Impl::OutputState> outputs{};
		{
			std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
			bindings.reserve(m_impl->bindings.size());
			for (const auto& [handle, binding] : m_impl->bindings)
			{
				(void)handle;
				bindings.push_back(binding);
			}
			outputs = m_impl->outputs;
		}
		ASH_PROFILE_PLOT("ScenePresentation/Bindings", static_cast<int64_t>(bindings.size()));
		ASH_PROFILE_PLOT("ScenePresentation/Outputs", static_cast<int64_t>(outputs.size()));

		{
			ASH_PROFILE_SCOPE_NC("ScenePresentation::SortBindings", AshEngine::Profile::Color::Submit);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(bindings.size()));
			std::sort(bindings.begin(), bindings.end(), [](const Impl::BindingState& lhs, const Impl::BindingState& rhs) {
				if (lhs.sort_order != rhs.sort_order)
				{
					return lhs.sort_order < rhs.sort_order;
				}
				return lhs.handle.value < rhs.handle.value;
			});
		}

		std::unordered_set<Scene*> referenced_scenes{};
		referenced_scenes.reserve(bindings.size());
		for (const Impl::BindingState& binding : bindings)
		{
			if (binding.scene != nullptr)
			{
				referenced_scenes.insert(binding.scene);
			}
		}

		for (auto it = m_impl->scene_states.begin(); it != m_impl->scene_states.end();)
		{
			if (referenced_scenes.find(it->first) == referenced_scenes.end())
			{
				it = m_impl->scene_states.erase(it);
				continue;
			}
			++it;
		}

		std::vector<Impl::PreparedPacket> prepared_packets{};
		prepared_packets.reserve(bindings.size());
		for (const Impl::BindingState& binding : bindings)
		{
			if (!binding.enabled)
			{
				continue;
			}

			const auto output_found = outputs.find(binding.output.value);
			if (output_found == outputs.end())
			{
				continue;
			}

			uint32_t output_width = 0;
			uint32_t output_height = 0;
			if (!Impl::resolve_output_extent(output_found->second, output_width, output_height) ||
				output_width == 0 ||
				output_height == 0)
			{
				continue;
			}

			Impl::PreparedPacket packet{};
			packet.binding = binding.handle;
			packet.output = binding.output;
			packet.debug_name = binding.debug_name;
			packet.overrides = binding.overrides;
			packet.output_width = output_width;
			packet.output_height = output_height;
			packet.sort_order = binding.sort_order;
			packet.visible_frame = std::make_shared<VisibleRenderFrame>();
			packet.visible_frame->frame_index = Application::get() ? Application::get()->get_frame_index() : 0;

			SceneView scene_view{};
			bool scene_view_valid = false;
			if (binding.scene != nullptr && binding.scene->is_valid())
			{
				ASH_PROFILE_SCOPE_NC("ScenePresentation::BuildVisibleFramePacket", AshEngine::Profile::Color::Visibility);
				ASH_PROFILE_SCOPE_TEXT(binding.debug_name.c_str(), binding.debug_name.size());
				const auto state_found = m_impl->scene_states.find(binding.scene);
				Impl::SceneState* scene_state = nullptr;
				if (state_found == m_impl->scene_states.end())
				{
					Impl::SceneState new_state{};
					new_state.scene = binding.scene;
					scene_state = &m_impl->scene_states.emplace(binding.scene, std::move(new_state)).first->second;
				}
				else
				{
					scene_state = &state_found->second;
				}

				const uint64_t scene_primitive_version = binding.scene->get_render_primitive_version();
				const uint64_t scene_transform_version = binding.scene->get_render_transform_version();
				const uint64_t scene_light_version = binding.scene->get_render_light_version();
				if (binding.refresh_requested ||
					!scene_state->render_scene_valid ||
					scene_state->last_primitive_version != scene_primitive_version)
				{
					ASH_PROFILE_SCOPE_NC("ScenePresentation::RebuildRenderScene", AshEngine::Profile::Color::Scene);
					ASH_PROFILE_SCOPE_TEXT(binding.debug_name.c_str(), binding.debug_name.size());
					scene_state->render_scene_valid = scene_state->render_scene.rebuild_from_scene(*binding.scene, *m_impl->render_asset_manager);
					scene_state->last_primitive_version = scene_primitive_version;
					scene_state->last_transform_version = scene_transform_version;
					scene_state->last_light_version = scene_light_version;
					if (!scene_state->render_scene_valid)
					{
						HLogError(
							"ScenePresentationSubsystem: failed to rebuild RenderScene for binding '{}' and scene '{}'.",
							binding.debug_name,
							binding.scene->get_name());
					}
				}
				else if (scene_state->last_transform_version != scene_transform_version)
				{
					ASH_PROFILE_SCOPE_NC("ScenePresentation::UpdateRenderSceneTransforms", AshEngine::Profile::Color::Scene);
					ASH_PROFILE_SCOPE_TEXT(binding.debug_name.c_str(), binding.debug_name.size());
					scene_state->render_scene_valid =
						scene_state->render_scene.update_transforms_from_scene(*binding.scene) &&
						scene_state->render_scene.rebuild_lights_from_scene(*binding.scene);
					scene_state->last_transform_version = scene_transform_version;
					scene_state->last_light_version = scene_light_version;
					if (!scene_state->render_scene_valid)
					{
						HLogError(
							"ScenePresentationSubsystem: failed to update RenderScene transforms for binding '{}' and scene '{}'.",
							binding.debug_name,
							binding.scene->get_name());
					}
				}
				else if (scene_state->last_light_version != scene_light_version)
				{
					ASH_PROFILE_SCOPE_NC("ScenePresentation::RebuildRenderSceneLights", AshEngine::Profile::Color::Scene);
					ASH_PROFILE_SCOPE_TEXT(binding.debug_name.c_str(), binding.debug_name.size());
					scene_state->render_scene_valid = scene_state->render_scene.rebuild_lights_from_scene(*binding.scene);
					scene_state->last_light_version = scene_light_version;
					if (!scene_state->render_scene_valid)
					{
						HLogError(
							"ScenePresentationSubsystem: failed to rebuild RenderScene lights for binding '{}' and scene '{}'.",
							binding.debug_name,
							binding.scene->get_name());
					}
				}

				scene_view_valid = Impl::build_binding_scene_view(binding, output_width, output_height, scene_view);
				if (scene_view_valid && scene_state->render_scene_valid)
				{
					VisibleRenderFrame visible_frame{};
					if (scene_state->render_scene.build_visible_render_frame(packet.visible_frame->frame_index, scene_view, visible_frame))
					{
						packet.visible_frame = std::make_shared<VisibleRenderFrame>(std::move(visible_frame));
					}
					else
					{
						HLogError(
							"ScenePresentationSubsystem: failed to build VisibleRenderFrame for binding '{}' and scene '{}'.",
							binding.debug_name,
							binding.scene->get_name());
					}
				}
			}

			if (!scene_view_valid)
			{
				packet.visible_frame->frame_index = Application::get() ? Application::get()->get_frame_index() : 0;
			}

			prepared_packets.push_back(std::move(packet));
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
			for (const Impl::BindingState& binding : bindings)
			{
				const auto found = m_impl->bindings.find(binding.handle.value);
				if (found != m_impl->bindings.end())
				{
					found->second.refresh_requested = false;
				}
			}
			m_impl->prepared_packets = std::move(prepared_packets);
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool ScenePresentationSubsystem::submit_presentations()
	{
		ASH_PROFILE_SCOPE_NC("ScenePresentationSubsystem::submit_presentations", AshEngine::Profile::Color::Submit);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl != nullptr);
		ASH_PROCESS_ERROR(m_impl->renderer != nullptr);
		ASH_PROCESS_ERROR(m_impl->render_asset_manager != nullptr);
		ASH_PROCESS_ERROR(m_impl->scene_renderer != nullptr);

		std::vector<Impl::PreparedPacket> prepared_packets{};
		std::unordered_map<uint32_t, Impl::OutputState> outputs{};
		{
			std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
			prepared_packets = m_impl->prepared_packets;
			outputs = m_impl->outputs;
		}
		ASH_PROFILE_PLOT("ScenePresentation/PreparedPackets", static_cast<int64_t>(prepared_packets.size()));

		if (prepared_packets.empty())
		{
			break;
		}

		m_impl->render_asset_manager->finalize_pending_assets();
		const std::shared_ptr<RenderTarget> back_buffer = m_impl->renderer->get_back_buffer();
		std::unordered_set<uint32_t> touched_outputs{};
		touched_outputs.reserve(prepared_packets.size());

		for (Impl::PreparedPacket& packet : prepared_packets)
		{
			auto output_found = outputs.find(packet.output.value);
			if (output_found == outputs.end())
			{
				continue;
			}

			Impl::OutputState& output_state = output_found->second;
			std::shared_ptr<RenderTarget> output_target = nullptr;
			if (output_state.kind == SceneOutputKind::Window)
			{
				output_target = back_buffer;
			}
			else
			{
				const RenderTextureFormat desired_format =
					resolve_output_format(output_state.kind, output_state.format, output_state.srgb, back_buffer);
				if (!output_state.render_target ||
					output_state.allocated_width != packet.output_width ||
					output_state.allocated_height != packet.output_height ||
					output_state.allocated_format != static_cast<uint32_t>(desired_format))
				{
					ASH_PROFILE_SCOPE_NC("ScenePresentation::AllocateOffscreenOutput", AshEngine::Profile::Color::Upload);
					ASH_PROFILE_SCOPE_TEXT(output_state.debug_name.c_str(), output_state.debug_name.size());
					RenderTargetDesc render_target_desc{};
					render_target_desc.width = static_cast<uint16_t>(packet.output_width);
					render_target_desc.height = static_cast<uint16_t>(packet.output_height);
					render_target_desc.format = desired_format;
					render_target_desc.shader_resource = true;
					render_target_desc.unordered_access = false;
					render_target_desc.name = output_state.debug_name.c_str();
					render_target_desc.use_optimized_clear_value = true;
					render_target_desc.optimized_clear_color = {
						packet.overrides.clear_color.r,
						packet.overrides.clear_color.g,
						packet.overrides.clear_color.b,
						packet.overrides.clear_color.a
					};
					output_state.render_target = m_impl->renderer->create_render_target(render_target_desc);
					if (!output_state.render_target)
					{
						HLogError(
							"ScenePresentationSubsystem: failed to allocate offscreen output '{}' ({}x{}, format={}).",
							output_state.debug_name,
							packet.output_width,
							packet.output_height,
							static_cast<int32_t>(desired_format));
					}
					output_state.allocated_width = packet.output_width;
					output_state.allocated_height = packet.output_height;
					output_state.allocated_format = static_cast<uint32_t>(desired_format);
				}

				output_target = output_state.render_target;
			}

			if (!output_target || !packet.visible_frame)
			{
				continue;
			}

			bool should_log_binding_submit = false;
			{
				std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
				should_log_binding_submit = m_impl->logged_binding_submits.insert(packet.binding.value).second;
			}
			if (should_log_binding_submit)
			{
				size_t total_section_count = 0;
				for (const VisibleStaticMeshDraw& draw : packet.visible_frame->static_mesh_draws)
				{
					total_section_count += draw.sections.size();
				}

				HLogInfo(
					"ScenePresentationSubsystem: submitting binding '{}' to output '{}' ({}x{}), draws={}, sections={}.",
					packet.debug_name,
					output_state.debug_name,
					packet.output_width,
					packet.output_height,
					packet.visible_frame->static_mesh_draws.size(),
					total_section_count);
			}

			ASH_PROFILE_SCOPE_NC("ScenePresentation::SubmitBinding", AshEngine::Profile::Color::Submit);
			ASH_PROFILE_SCOPE_TEXT(packet.debug_name.c_str(), packet.debug_name.size());
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(packet.visible_frame->static_mesh_draws.size()));

			if (!prepare_visible_frame_material_proxies(
				*packet.visible_frame,
				*m_impl->render_asset_manager,
				*m_impl->renderer))
			{
				HLogError(
					"ScenePresentationSubsystem: material preparation failed for binding '{}'.",
					packet.debug_name);
				continue;
			}

			const bool first_use_on_output = touched_outputs.insert(output_state.handle.value).second;
			SceneRenderViewContext view_context{};
			view_context.view_id = packet.binding.value;
			view_context.debug_name = packet.debug_name.c_str();
			view_context.output_target = output_target;
			view_context.color_load_action = resolve_load_action(packet.overrides.color_clear_mode, first_use_on_output);
			view_context.color_clear_value = {
				packet.overrides.clear_color.r,
				packet.overrides.clear_color.g,
				packet.overrides.clear_color.b,
				packet.overrides.clear_color.a
			};
			view_context.reverse_z = packet.visible_frame->reverse_z;
			view_context.depth_clear_value = {
				resolve_scene_view_depth_clear_value(packet.overrides.clear_depth, packet.visible_frame->reverse_z),
				0u
			};

			if (packet.overrides.rect_mode == SceneViewRectMode::PixelRect &&
				rect_is_valid(packet.overrides.rect, packet.output_width, packet.output_height))
			{
				view_context.has_viewport = true;
				view_context.viewport = {
					static_cast<int16_t>(packet.overrides.rect.x),
					static_cast<int16_t>(packet.overrides.rect.y),
					static_cast<uint16_t>(packet.overrides.rect.width),
					static_cast<uint16_t>(packet.overrides.rect.height),
					0.0f,
					1.0f
				};
				view_context.has_scissor = true;
				view_context.scissor = {
					static_cast<int16_t>(packet.overrides.rect.x),
					static_cast<int16_t>(packet.overrides.rect.y),
					static_cast<uint16_t>(packet.overrides.rect.width),
					static_cast<uint16_t>(packet.overrides.rect.height)
				};
			}

			if (!m_impl->scene_renderer->render_visible_frame(*packet.visible_frame, view_context))
			{
				HLogError("ScenePresentationSubsystem: scene submit failed for binding '{}'.", packet.debug_name);
			}
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
			for (const auto& [output_id, local_state] : outputs)
			{
				const auto found = m_impl->outputs.find(output_id);
				if (found != m_impl->outputs.end())
				{
					found->second.render_target = local_state.render_target;
					found->second.allocated_width = local_state.allocated_width;
					found->second.allocated_height = local_state.allocated_height;
					found->second.allocated_format = local_state.allocated_format;
				}
			}
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_ptr<RenderTarget> ScenePresentationSubsystem::resolve_surface_render_target(UISurfaceHandle surface) const
	{
		if (!m_impl || !surface.is_valid())
		{
			return nullptr;
		}

		std::scoped_lock<std::mutex> lock(m_impl->state_mutex);
		for (const auto& [handle, output_state] : m_impl->outputs)
		{
			(void)handle;
			if (output_state.surface.value == surface.value)
			{
				return output_state.render_target;
			}
		}
		return nullptr;
	}
}
