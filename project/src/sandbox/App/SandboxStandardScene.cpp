#include "App/SandboxStandardScene.h"

#include "Base/hlog.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <limits>
#include <utility>

namespace AshSandbox
{
	namespace
	{
		static constexpr char k_scene_name[] = "SandboxStandardScene";
		static constexpr char k_scene_root_name[] = "SandboxStandardSceneRoot";
		static constexpr char k_primary_camera_name[] = "SandboxStandardSceneCamera";
		static const glm::vec3 k_primary_camera_position{ 0.0f, 1.5f, -6.0f };
		static const glm::vec3 k_primary_camera_rotation_euler_degrees{ 0.0f, 0.0f, 0.0f };
		static constexpr float k_default_camera_move_speed = 8.0f;

		static auto make_bounds_corner(
			const AshEngine::SceneMeshBounds& bounds,
			uint32_t corner_index) -> glm::vec3
		{
			return glm::vec3(
				(corner_index & 1u) == 0u ? bounds.local_min.x : bounds.local_max.x,
				(corner_index & 2u) == 0u ? bounds.local_min.y : bounds.local_max.y,
				(corner_index & 4u) == 0u ? bounds.local_min.z : bounds.local_max.z);
		}

		static auto make_transform_from_look_at(
			const glm::vec3& camera_position,
			const glm::vec3& look_at_target,
			AshEngine::TransformComponent& out_transform) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			const glm::vec3 forward = look_at_target - camera_position;
			if (glm::length(forward) <= 0.001f)
			{
				ASH_PROCESS_ERROR(false);
			}

			const glm::mat4 view_matrix = glm::lookAtLH(camera_position, look_at_target, glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::mat4 world_matrix = glm::inverse(view_matrix);
			glm::vec3 scale{};
			glm::quat rotation{};
			glm::vec3 translation{};
			glm::vec3 skew{};
			glm::vec4 perspective{};
			ASH_PROCESS_ERROR(glm::decompose(world_matrix, scale, rotation, translation, skew, perspective));

			out_transform.position = translation;
			out_transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
			out_transform.rotation_euler_degrees = glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		static auto make_visible_frame_copy(
			const AshEngine::VisibleRenderFrame& source_frame,
			uint64_t frame_index) -> std::shared_ptr<AshEngine::VisibleRenderFrame>
		{
			auto visible_frame = std::make_shared<AshEngine::VisibleRenderFrame>(source_frame);
			visible_frame->frame_index = frame_index;
			return visible_frame;
		}
	}

	auto SandboxStandardScene::get_canonical_sample_asset_path() -> const std::filesystem::path&
	{
		static const std::filesystem::path k_sample_asset_path = "models/gltfs/Sponza/glTF/Sponza.gltf";
		return k_sample_asset_path;
	}

	auto SandboxStandardScene::start(
		AshEngine::AssetDatabase& asset_database,
		AshEngine::Renderer& renderer,
		AshEngine::RenderAssetManager& render_asset_manager) -> bool
	{
		std::string failure_detail{};
		const std::filesystem::path& sample_asset_path = get_canonical_sample_asset_path();
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		if (!asset_database.is_valid())
		{
			failure_detail = "Sandbox standard scene cannot start because the AssetDatabase is invalid.";
			ASH_PROCESS_ERROR(false);
		}
		if (renderer.get_back_buffer() == nullptr)
		{
			failure_detail = "Sandbox standard scene cannot start because the renderer back buffer is unavailable.";
			ASH_PROCESS_ERROR(false);
		}

		const AshEngine::AssetInfo* asset_info = asset_database.find_asset_by_path(sample_asset_path);
		if (asset_info == nullptr)
		{
			failure_detail = "Sandbox standard scene asset was not found in the AssetDatabase: '" +
				sample_asset_path.generic_string() + "'.";
			ASH_PROCESS_ERROR(false);
		}

		std::shared_future<std::shared_ptr<const AshEngine::Model>> model_future =
			asset_database.load_model_by_path_async(sample_asset_path);
		if (!model_future.valid())
		{
			failure_detail = "Sandbox standard scene failed to queue async model loading for '" +
				sample_asset_path.generic_string() + "'.";
			ASH_PROCESS_ERROR(false);
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_asset_database = &asset_database;
			m_renderer = &renderer;
			m_render_asset_manager = &render_asset_manager;
			m_model_future = std::move(model_future);
			m_snapshot = {};
			m_snapshot.load_state = SandboxStandardSceneLoadState::LoadingModel;
			m_snapshot.sample_asset_path = sample_asset_path;
			m_free_camera_controller.reset();
			m_has_logic_tick_time = false;
		}

		HLogInfo("Sandbox standard scene started async load for '{}'.", sample_asset_path.generic_string());
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_snapshot = {};
			m_snapshot.sample_asset_path = sample_asset_path;
			_set_failure_locked(
				failure_detail.empty()
				? std::string("Sandbox standard scene failed to start.")
				: std::move(failure_detail));
		}
		return bResult;
	}

	auto SandboxStandardScene::reset() -> void
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_snapshot = {};
		m_snapshot.sample_asset_path = get_canonical_sample_asset_path();
		m_asset_database = nullptr;
		m_renderer = nullptr;
		m_render_asset_manager = nullptr;
		m_model_future = {};
		m_free_camera_controller.reset();
		m_has_logic_tick_time = false;
	}

	auto SandboxStandardScene::update_logic(const AshEngine::InputState& input, uint64_t frame_index) -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		AshEngine::AssetDatabase* asset_database = nullptr;
		AshEngine::Renderer* renderer = nullptr;
		AshEngine::RenderAssetManager* render_asset_manager = nullptr;
		std::shared_future<std::shared_ptr<const AshEngine::Model>> model_future{};
		SandboxStandardSceneLoadState load_state = SandboxStandardSceneLoadState::Idle;
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			asset_database = m_asset_database;
			renderer = m_renderer;
			render_asset_manager = m_render_asset_manager;
			model_future = m_model_future;
			load_state = m_snapshot.load_state;
		}

		ASH_PROCESS_ERROR(asset_database != nullptr);
		ASH_PROCESS_ERROR(renderer != nullptr);
		ASH_PROCESS_ERROR(render_asset_manager != nullptr);

		if (load_state == SandboxStandardSceneLoadState::Failed)
		{
			ASH_PROCESS_ERROR(false);
		}

		if (load_state == SandboxStandardSceneLoadState::LoadingModel)
		{
			ASH_PROCESS_ERROR(model_future.valid());
			if (model_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				break;
			}

			std::shared_ptr<const AshEngine::Model> model{};
			try
			{
				model = model_future.get();
			}
			catch (const std::exception& exception)
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				_set_failure_locked("Standard scene model load threw: " + std::string(exception.what()));
				ASH_PROCESS_ERROR(false);
			}

			if (!model || !model->is_valid())
			{
				std::string asset_error = asset_database->get_last_error();
				const AshEngine::AssetInfo* asset_info = asset_database->find_asset_by_path(get_canonical_sample_asset_path());
				if (asset_info != nullptr)
				{
					const std::string per_asset_error = asset_database->get_asset_last_error(asset_info->id);
					if (!per_asset_error.empty())
					{
						asset_error = per_asset_error;
					}
				}

				std::scoped_lock<std::mutex> lock(m_mutex);
				_set_failure_locked(
					"Failed to load standard scene model '" + get_canonical_sample_asset_path().generic_string() +
					"': " + (asset_error.empty() ? std::string("Unknown async load failure.") : asset_error));
				ASH_PROCESS_ERROR(false);
			}

			SandboxStandardSceneSnapshot ready_snapshot{};
			std::string build_error{};
			if (!_build_runtime_snapshot(model, frame_index, ready_snapshot, build_error))
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				_set_failure_locked(build_error.empty() ? "Failed to build Sandbox standard scene runtime state." : std::move(build_error));
				ASH_PROCESS_ERROR(false);
			}

			uint32_t entity_count = ready_snapshot.scene.get_entity_count();
			size_t visible_draw_count =
				ready_snapshot.visible_frame.latest_snapshot
				? ready_snapshot.visible_frame.latest_snapshot->static_mesh_draws.size()
				: 0u;
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				m_snapshot = std::move(ready_snapshot);
				m_free_camera_controller.reset();
				m_free_camera_controller.bind_camera_entity(m_snapshot.primary_camera_entity_id);
				m_free_camera_controller.set_move_speed(m_snapshot.recommended_camera_move_speed);
				m_has_logic_tick_time = false;
			}

			HLogInfo(
				"Sandbox standard scene is ready: sample='{}', entities={}, visible_draws={}.",
				get_canonical_sample_asset_path().generic_string(),
				entity_count,
				visible_draw_count);

			break;
		}

		if (load_state == SandboxStandardSceneLoadState::Ready)
		{
			SandboxStandardSceneSnapshot working_snapshot{};
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				working_snapshot = m_snapshot;
			}
			if (m_free_camera_controller.get_camera_entity_id() == 0 && working_snapshot.primary_camera_entity_id != 0)
			{
				m_free_camera_controller.bind_camera_entity(working_snapshot.primary_camera_entity_id);
				m_free_camera_controller.set_move_speed(working_snapshot.recommended_camera_move_speed);
			}

			std::string build_error{};
			const double logic_delta_seconds = _consume_logic_delta_seconds();
			if (!m_free_camera_controller.update(working_snapshot.scene, input, logic_delta_seconds, build_error))
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				_set_failure_locked(build_error.empty() ? "Failed to update the Sandbox standard scene free camera." : std::move(build_error));
				ASH_PROCESS_ERROR(false);
			}
			if (!_rebuild_visible_frame(working_snapshot, frame_index, build_error))
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				_set_failure_locked(build_error.empty() ? "Failed to rebuild Sandbox standard scene visible frame." : std::move(build_error));
				ASH_PROCESS_ERROR(false);
			}

			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				m_snapshot.latest_scene_view = working_snapshot.latest_scene_view;
				m_snapshot.visible_frame = std::move(working_snapshot.visible_frame);
			}
		}

		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			if (m_snapshot.load_state != SandboxStandardSceneLoadState::Failed)
			{
				_set_failure_locked("Sandbox standard scene logic update failed.");
			}
		}
		return bResult;
	}

	auto SandboxStandardScene::take_pending_visible_frame(
		std::shared_ptr<AshEngine::VisibleRenderFrame>& out_visible_frame,
		uint64_t& out_version) -> bool
	{
		out_visible_frame.reset();
		out_version = 0;

		std::scoped_lock<std::mutex> lock(m_mutex);
		if (!m_snapshot.visible_frame.pending_handoff)
		{
			return false;
		}

		out_visible_frame = m_snapshot.visible_frame.pending_handoff;
		out_version = m_snapshot.visible_frame.latest_snapshot_version;
		m_snapshot.visible_frame.pending_handoff.reset();
		m_snapshot.visible_frame.latest_handoff_version = out_version;
		return true;
	}

	auto SandboxStandardScene::note_visible_frame_submitted(uint64_t version) -> void
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_snapshot.visible_frame.latest_submitted_version =
			std::max(m_snapshot.visible_frame.latest_submitted_version, version);
	}

	auto SandboxStandardScene::snapshot() const -> SandboxStandardSceneSnapshot
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	auto SandboxStandardScene::get_load_state() const -> SandboxStandardSceneLoadState
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_snapshot.load_state;
	}

	auto SandboxStandardScene::get_failure_detail() const -> std::string
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_snapshot.failure_detail;
	}

	auto SandboxStandardScene::is_ready() const -> bool
	{
		return get_load_state() == SandboxStandardSceneLoadState::Ready;
	}

	auto SandboxStandardScene::_build_runtime_snapshot(
		const std::shared_ptr<const AshEngine::Model>& model,
		uint64_t frame_index,
		SandboxStandardSceneSnapshot& out_snapshot,
		std::string& out_error) -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(model && model->is_valid());
		ASH_PROCESS_ERROR(m_render_asset_manager != nullptr);
		ASH_PROCESS_ERROR(m_asset_database != nullptr);

		out_snapshot = {};
		out_snapshot.sample_asset_path = get_canonical_sample_asset_path();
		out_snapshot.scene = AshEngine::Scene::create(k_scene_name);
		if (!out_snapshot.scene.is_valid())
		{
			out_error = "Failed to create Sandbox standard scene container.";
			ASH_PROCESS_ERROR(false);
		}

		const AshEngine::Entity root_entity =
			out_snapshot.scene.instantiate_model(*model, {}, k_scene_root_name);
		if (!root_entity.is_valid())
		{
			out_error = "Failed to instantiate the Sponza model into the Sandbox standard scene.";
			ASH_PROCESS_ERROR(false);
		}
		ASH_PROCESS_ERROR(_create_primary_camera(
			out_snapshot.scene,
			out_snapshot.primary_camera_entity_id,
			out_snapshot.recommended_camera_move_speed,
			out_error));
		if (!out_snapshot.render_scene.rebuild_from_scene(out_snapshot.scene, *m_render_asset_manager))
		{
			out_error = "Failed to rebuild RenderScene from the Sandbox standard scene.";
			ASH_PROCESS_ERROR(false);
		}
		ASH_PROCESS_ERROR(_rebuild_visible_frame(out_snapshot, frame_index, out_error));
		out_snapshot.load_state = SandboxStandardSceneLoadState::Ready;
		out_snapshot.failure_detail.clear();
		out_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_compute_scene_world_bounds(
		const AshEngine::Scene& scene,
		glm::vec3& out_world_min,
		glm::vec3& out_world_max,
		std::string& out_error) const -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_asset_database != nullptr);
		ASH_PROCESS_ERROR(scene.is_valid());

		glm::vec3 world_min{
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max()
		};
		glm::vec3 world_max{
			std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest()
		};
		bool has_bounds = false;

		for (const AshEngine::SceneMeshExtractionDesc& mesh_desc : scene.extract_visible_mesh_entities())
		{
			AshEngine::MeshComponent mesh_component{};
			mesh_component.asset_path = mesh_desc.asset_path;
			mesh_component.mesh_index = mesh_desc.mesh_index;
			mesh_component.visible = mesh_desc.visible;
			mesh_component.mobility = mesh_desc.mobility;
			mesh_component.layer_mask = mesh_desc.layer_mask;

			AshEngine::SceneMeshBounds local_bounds{};
			if (!scene.try_get_mesh_local_bounds(*m_asset_database, mesh_component, local_bounds) || !local_bounds.is_valid)
			{
				out_error = "Failed to compute mesh bounds while preparing the Sandbox standard scene camera.";
				ASH_PROCESS_ERROR(false);
			}

			for (uint32_t corner_index = 0; corner_index < 8; ++corner_index)
			{
				const glm::vec3 local_corner = make_bounds_corner(local_bounds, corner_index);
				const glm::vec4 world_corner =
					mesh_desc.world_transform * glm::vec4(local_corner.x, local_corner.y, local_corner.z, 1.0f);
				world_min.x = std::min(world_min.x, world_corner.x);
				world_min.y = std::min(world_min.y, world_corner.y);
				world_min.z = std::min(world_min.z, world_corner.z);
				world_max.x = std::max(world_max.x, world_corner.x);
				world_max.y = std::max(world_max.y, world_corner.y);
				world_max.z = std::max(world_max.z, world_corner.z);
				has_bounds = true;
			}
		}

		if (!has_bounds)
		{
			out_error = "The Sandbox standard scene did not produce any visible mesh bounds.";
			ASH_PROCESS_ERROR(false);
		}

		out_world_min = world_min;
		out_world_max = world_max;
		out_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_make_default_camera_transform(
		const glm::vec3& bounds_min,
		const glm::vec3& bounds_max,
		AshEngine::TransformComponent& out_transform,
		float& out_recommended_move_speed) const -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		const glm::vec3 center = (bounds_min + bounds_max) * 0.5f;
		const glm::vec3 extent = glm::max(bounds_max - bounds_min, glm::vec3(0.1f, 0.1f, 0.1f));
		const float radius = std::max({ extent.x, extent.y, extent.z }) * 0.5f;
		const glm::vec3 target = center + glm::vec3(0.0f, extent.y * 0.1f, 0.0f);
		const glm::vec3 position = target + glm::vec3(
			0.0f,
			std::max(radius * 0.2f, 1.5f),
			-std::max(radius * 1.75f, 5.0f));

		if (!make_transform_from_look_at(position, target, out_transform))
		{
			out_transform.position = k_primary_camera_position;
			out_transform.rotation_euler_degrees = k_primary_camera_rotation_euler_degrees;
			out_transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
		}

		out_recommended_move_speed = std::clamp(radius * 0.2f, 4.0f, 48.0f);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_rebuild_visible_frame(
		SandboxStandardSceneSnapshot& io_snapshot,
		uint64_t frame_index,
		std::string& out_error) -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(io_snapshot.scene.is_valid());

		const std::shared_ptr<AshEngine::RenderTarget> back_buffer = m_renderer->get_back_buffer();
		if (!back_buffer)
		{
			out_error = "Renderer back buffer is unavailable for Sandbox standard scene visibility.";
			ASH_PROCESS_ERROR(false);
		}

		AshEngine::SceneViewDesc view_desc{};
		view_desc.viewport_width = back_buffer->get_width();
		view_desc.viewport_height = back_buffer->get_height();
		if (!AshEngine::build_primary_scene_view(io_snapshot.scene, view_desc, io_snapshot.latest_scene_view))
		{
			out_error = "Failed to build the primary SceneView for the Sandbox standard scene.";
			ASH_PROCESS_ERROR(false);
		}

		AshEngine::VisibleRenderFrame visible_frame{};
		if (!io_snapshot.render_scene.build_visible_render_frame(
			frame_index,
			io_snapshot.latest_scene_view,
			back_buffer,
			visible_frame))
		{
			out_error = "Failed to build a VisibleRenderFrame for the Sandbox standard scene.";
			ASH_PROCESS_ERROR(false);
		}
		if (visible_frame.static_mesh_draws.empty())
		{
			out_error = "Sandbox standard scene produced an empty VisibleRenderFrame.";
			ASH_PROCESS_ERROR(false);
		}

		io_snapshot.visible_frame.latest_snapshot = make_visible_frame_copy(visible_frame, frame_index);
		io_snapshot.visible_frame.pending_handoff = io_snapshot.visible_frame.latest_snapshot;
		++io_snapshot.visible_frame.latest_snapshot_version;
		out_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_create_primary_camera(
		AshEngine::Scene& scene,
		AshEngine::EntityId& out_camera_entity_id,
		float& out_recommended_move_speed,
		std::string& out_error) const -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		AshEngine::Entity camera_entity = scene.create_entity(k_primary_camera_name);
		if (!camera_entity.is_valid())
		{
			out_error = "Failed to create the Sandbox standard scene primary camera entity.";
			ASH_PROCESS_ERROR(false);
		}

		AshEngine::TransformComponent transform = camera_entity.get_transform_component();
		glm::vec3 bounds_min{};
		glm::vec3 bounds_max{};
		out_recommended_move_speed = k_default_camera_move_speed;
		if (_compute_scene_world_bounds(scene, bounds_min, bounds_max, out_error))
		{
			ASH_PROCESS_ERROR(_make_default_camera_transform(
				bounds_min,
				bounds_max,
				transform,
				out_recommended_move_speed));
		}
		else
		{
			transform.position = k_primary_camera_position;
			transform.rotation_euler_degrees = k_primary_camera_rotation_euler_degrees;
			transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
			out_error.clear();
		}
		if (!camera_entity.set_transform_component(transform))
		{
			out_error = "Failed to configure the Sandbox standard scene primary camera transform.";
			ASH_PROCESS_ERROR(false);
		}

		AshEngine::CameraComponent camera{};
		camera.primary = true;
		camera.fov_y_degrees = 60.0f;
		camera.near_plane = 0.1f;
		camera.far_plane = 5000.0f;
		if (!camera_entity.add_camera_component(camera))
		{
			out_error = "Failed to attach the Sandbox standard scene camera component.";
			ASH_PROCESS_ERROR(false);
		}

		out_camera_entity_id = camera_entity.get_id();
		out_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_consume_logic_delta_seconds() -> double
	{
		const auto now = std::chrono::steady_clock::now();
		double delta_seconds = 1.0 / 60.0;
		if (m_has_logic_tick_time)
		{
			delta_seconds = std::chrono::duration<double>(now - m_last_logic_tick_time).count();
		}
		m_last_logic_tick_time = now;
		m_has_logic_tick_time = true;
		return std::clamp(delta_seconds, 1.0 / 240.0, 0.1);
	}

	auto SandboxStandardScene::_set_failure_locked(std::string detail) -> void
	{
		m_snapshot.load_state = SandboxStandardSceneLoadState::Failed;
		m_snapshot.failure_detail = std::move(detail);
		HLogError("Sandbox standard scene failure: {}", m_snapshot.failure_detail);
	}
}
