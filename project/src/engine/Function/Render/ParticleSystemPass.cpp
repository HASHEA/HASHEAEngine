#include "Function/Render/ParticleSystemPass.h"

#include "Base/hprofiler.h"
#include "Function/Render/ParticleSystemMath.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_particle_shader_path = "project/src/engine/Shaders/Particles/ParticleSystem.hlsl";
		static constexpr const char* k_particle_compute_macro = "PARTICLE_COMPUTE=1";
		static constexpr uint32_t k_compaction_group_size = 64u;

		// Must match the PARTICLE_COMPUTE AshRootConstants cbuffer in ParticleSystem.hlsl.
		struct ParticleSimulateConstants
		{
			glm::mat4 emitter_transform{ 1.0f };
			uint32_t spawn_count = 0;
			uint32_t max_particles = 0;
			float delta_seconds = 0.0f;
			float lifetime = 0.0f;
			float lifetime_variance = 0.0f;
			float initial_speed = 0.0f;
			float spread_angle_radians = 0.0f;
			uint32_t random_seed = 0;
			uint32_t total_spawned = 0;
			uint32_t candidate_group_count = 0;
			uint32_t pad0[2]{};
			glm::vec4 constant_acceleration{ 0.0f };
		};
		static_assert(sizeof(ParticleSimulateConstants) == 128u);

		// Must match the draw-variant AshRootConstants cbuffer in ParticleSystem.hlsl.
		struct ParticleDrawConstants
		{
			glm::mat4 view_projection{ 1.0f };
			glm::vec4 projection_scale_start_end_size{ 0.0f };
			glm::uvec4 packed_start_end_color{ 0u };
			glm::vec4 depth_reconstruct{ 0.0f };
			glm::vec3 radial_soft_parameters{ 0.0f };
			uint32_t flags = 0;
		};
		static_assert(sizeof(ParticleDrawConstants) == 128u);
		static_assert(sizeof(ParticleDrawConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		static auto build_source_hash() -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, k_particle_shader_path);
			return hash_value;
		}

		static void apply_view_context_to_draw_desc(
			GraphicsDrawDesc& draw_desc,
			const SceneRenderViewContext& view_context)
		{
			draw_desc.reverse_z = view_context.reverse_z;
			draw_desc.has_viewport = view_context.has_viewport;
			if (view_context.has_viewport)
			{
				draw_desc.viewport = view_context.viewport;
			}
			draw_desc.has_scissor = view_context.has_scissor;
			if (view_context.has_scissor)
			{
				draw_desc.scissor = view_context.scissor;
			}
		}

		static bool attach_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const ParticleDrawConstants& constants)
		{
			RHI::ShaderParameterBlockLayout layout{};
			if (!program ||
				!program->get_parameter_block_layout("AshRootConstants", layout) ||
				layout.byte_size != sizeof(constants) ||
				layout.byte_size > GraphicsDrawDesc::InlineConstDataCapacity)
			{
				return false;
			}
			draw_desc.const_data_size = static_cast<uint32_t>(sizeof(constants));
			draw_desc.inline_const_data_valid = true;
			std::memcpy(draw_desc.inline_const_data.data(), &constants, draw_desc.const_data_size);
			return true;
		}

		static auto make_simulate_constants(
			const VisibleParticleEmitter& emitter,
			uint32_t capacity,
			uint32_t spawn_count,
			uint32_t total_spawned,
			float delta_seconds) -> ParticleSimulateConstants
		{
			ParticleSimulateConstants constants{};
			constants.emitter_transform = emitter.world_transform;
			constants.spawn_count = spawn_count;
			constants.max_particles = capacity;
			constants.delta_seconds = delta_seconds;
			constants.lifetime = std::max(emitter.particle.lifetime, 0.01f);
			constants.lifetime_variance = std::max(emitter.particle.lifetime_variance, 0.0f);
			constants.initial_speed = emitter.particle.initial_speed;
			constants.spread_angle_radians =
				glm::radians(std::clamp(emitter.particle.spread_angle_degrees, 0.0f, 180.0f));
			// Mix in entity_id so co-located emitters with default seeds diverge;
			// entity ids are serialized, so this stays frame-dump deterministic.
			constants.random_seed = make_particle_random_seed(
				emitter.particle.random_seed,
				static_cast<uint64_t>(emitter.entity_id));
			constants.total_spawned = total_spawned;
			constants.candidate_group_count =
				(capacity + spawn_count + k_compaction_group_size - 1u) / k_compaction_group_size;
			constants.constant_acceleration = glm::vec4(emitter.particle.constant_acceleration, 0.0f);
			return constants;
		}

		static auto make_draw_constants(
			const VisibleRenderFrame& frame,
			const VisibleParticleEmitter& emitter) -> ParticleDrawConstants
		{
			ParticleDrawConstants constants{};
			constants.view_projection = frame.view_projection;
			constants.projection_scale_start_end_size = glm::vec4(
				frame.projection[0][0],
				frame.projection[1][1],
				std::max(emitter.particle.start_size, 0.0f),
				std::max(emitter.particle.end_size, 0.0f));
			constants.packed_start_end_color = glm::uvec4(
				glm::packHalf2x16(glm::vec2(emitter.particle.start_color.x, emitter.particle.start_color.y)),
				glm::packHalf2x16(glm::vec2(emitter.particle.start_color.z, emitter.particle.start_color.w)),
				glm::packHalf2x16(glm::vec2(emitter.particle.end_color.x, emitter.particle.end_color.y)),
				glm::packHalf2x16(glm::vec2(emitter.particle.end_color.z, emitter.particle.end_color.w)));
			constants.depth_reconstruct =
				ParticleSystemInternal::make_depth_reconstruct_coefficients(frame.projection);
			constants.radial_soft_parameters = glm::vec3(
				emitter.particle.radial_falloff,
				emitter.particle.radial_sharpness,
				emitter.particle.soft_fade_distance);
			constants.flags = (frame.reverse_z ? 1u : 0u) | (emitter.particle.soft_particles ? 2u : 0u);
			return constants;
		}
	}

	uint32_t calculate_particle_capture_warmup_spawn_count(const ParticleComponent& particle)
	{
		if (!particle.emitting || particle.max_particles == 0 || particle.spawn_rate <= 0.0f)
		{
			return 0;
		}
		const double maximum_lifetime = std::max(
			static_cast<double>(particle.lifetime) + static_cast<double>(particle.lifetime_variance),
			0.01);
		const double steady_state_spawn_count = std::ceil(
			static_cast<double>(particle.spawn_rate) * maximum_lifetime);
		return static_cast<uint32_t>(std::min(
			steady_state_spawn_count,
			static_cast<double>(particle.max_particles)));
	}

	uint32_t make_particle_random_seed(uint32_t random_seed, uint64_t entity_id)
	{
		const uint32_t entity_low = static_cast<uint32_t>(entity_id);
		const uint32_t entity_high = static_cast<uint32_t>(entity_id >> 32u);
		uint32_t entity_mix = entity_low * 2654435761u;
		entity_mix ^= entity_high * 2246822519u + 3266489917u +
			(entity_mix << 6u) + (entity_mix >> 2u);
		return random_seed ^ entity_mix;
	}

	bool ParticleSystemPass::is_capture_ready(const VisibleRenderFrame& frame) const
	{
		for (const VisibleParticleEmitter& emitter : frame.particle_emitters)
		{
			const uint32_t required_spawn_count =
				calculate_particle_capture_warmup_spawn_count(emitter.particle);
			if (required_spawn_count == 0)
			{
				continue;
			}
			const EmitterKey key{ frame.scene_runtime_id, static_cast<uint64_t>(emitter.entity_id) };
			const auto found = m_emitter_states.find(key);
			if (found == m_emitter_states.end() ||
				!found->second.has_simulated ||
				found->second.total_spawned < required_spawn_count)
			{
				return false;
			}
		}
		return true;
	}

	bool ParticleSystemPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("ParticleSystemPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_programs(*renderer));
		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Linear;
		sampler_desc.mag_filter = RenderSamplerFilter::Linear;
		sampler_desc.mip_filter = RenderSamplerFilter::Linear;
		m_sprite_sampler = renderer->create_sampler(sampler_desc, "SceneParticleSpriteSampler");
		ASH_PROCESS_ERROR(m_sprite_sampler != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void ParticleSystemPass::shutdown()
	{
		clear_program_buffer_bindings();
		m_emitter_states.clear();
		m_draw_alpha_soft_program.reset();
		m_draw_additive_soft_program.reset();
		m_draw_alpha_program.reset();
		m_draw_additive_program.reset();
		m_sprite_sampler.reset();
		m_write_args_program.reset();
		m_scatter_program.reset();
		m_scan_blocks_program.reset();
		m_classify_program.reset();
		m_renderer = nullptr;
	}

	void ParticleSystemPass::release_scene(uint64_t scene_runtime_id)
	{
		bool released_any = false;
		for (auto iterator = m_emitter_states.begin(); iterator != m_emitter_states.end();)
		{
			if (iterator->first.scene_runtime_id == scene_runtime_id)
			{
				iterator = m_emitter_states.erase(iterator);
				released_any = true;
				continue;
			}
			++iterator;
		}
		if (released_any)
		{
			clear_program_buffer_bindings();
		}
	}

	bool ParticleSystemPass::create_programs(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("ParticleSystemPass::create_programs", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const uint64_t source_hash = build_source_hash();

		ComputeProgramDesc classify_desc{};
		classify_desc.shader_path = k_particle_shader_path;
		classify_desc.compute_entry = "CSClassify";
		classify_desc.shader_macro = k_particle_compute_macro;
		classify_desc.source_hash = source_hash;
		classify_desc.name = "SceneParticleClassify";
		m_classify_program = renderer.create_compute_program(classify_desc);
		ASH_PROCESS_ERROR(m_classify_program != nullptr);

		ComputeProgramDesc scan_blocks_desc{};
		scan_blocks_desc.shader_path = k_particle_shader_path;
		scan_blocks_desc.compute_entry = "CSScanBlocks";
		scan_blocks_desc.shader_macro = k_particle_compute_macro;
		scan_blocks_desc.source_hash = source_hash;
		scan_blocks_desc.name = "SceneParticleScanBlocks";
		m_scan_blocks_program = renderer.create_compute_program(scan_blocks_desc);
		ASH_PROCESS_ERROR(m_scan_blocks_program != nullptr);

		ComputeProgramDesc scatter_desc{};
		scatter_desc.shader_path = k_particle_shader_path;
		scatter_desc.compute_entry = "CSScatter";
		scatter_desc.shader_macro = k_particle_compute_macro;
		scatter_desc.source_hash = source_hash;
		scatter_desc.name = "SceneParticleScatter";
		m_scatter_program = renderer.create_compute_program(scatter_desc);
		ASH_PROCESS_ERROR(m_scatter_program != nullptr);

		ComputeProgramDesc write_args_desc{};
		write_args_desc.shader_path = k_particle_shader_path;
		write_args_desc.compute_entry = "CSWriteArgs";
		write_args_desc.shader_macro = k_particle_compute_macro;
		write_args_desc.source_hash = source_hash;
		write_args_desc.name = "SceneParticleWriteArgs";
		m_write_args_program = renderer.create_compute_program(write_args_desc);
		ASH_PROCESS_ERROR(m_write_args_program != nullptr);

		GraphicsProgramState state{};
		state.cull_mode = RenderCullMode::None;
		state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		state.depth_test = true;
		state.depth_write = false;
		state.depth_compare = RenderCompareOp::LessEqual;
		state.blend_mode = RenderBlendMode::Additive;

		GraphicsProgramDesc draw_desc{};
		draw_desc.shader_path = k_particle_shader_path;
		draw_desc.base_shader_path = k_particle_shader_path;
		draw_desc.vertex_entry = "VSMain";
		draw_desc.fragment_entry = "PSMain";
		draw_desc.source_hash = source_hash;
		draw_desc.state = state;
		draw_desc.shader_macro = "PARTICLE_ADDITIVE=1";
		draw_desc.name = "SceneParticleDrawAdditive";
		m_draw_additive_program = renderer.create_graphics_program(draw_desc);
		ASH_PROCESS_ERROR(m_draw_additive_program != nullptr);

		draw_desc.state.blend_mode = RenderBlendMode::AlphaBlend;
		draw_desc.shader_macro = "PARTICLE_ALPHA_BLEND=1";
		draw_desc.name = "SceneParticleDrawAlpha";
		m_draw_alpha_program = renderer.create_graphics_program(draw_desc);
		ASH_PROCESS_ERROR(m_draw_alpha_program != nullptr);

		draw_desc.state.blend_mode = RenderBlendMode::Additive;
		draw_desc.shader_macro = "PARTICLE_ADDITIVE=1;PARTICLE_SOFT_DEPTH=1";
		draw_desc.name = "SceneParticleDrawAdditiveSoft";
		m_draw_additive_soft_program = renderer.create_graphics_program(draw_desc);
		ASH_PROCESS_ERROR(m_draw_additive_soft_program != nullptr);

		draw_desc.state.blend_mode = RenderBlendMode::AlphaBlend;
		draw_desc.shader_macro = "PARTICLE_ALPHA_BLEND=1;PARTICLE_SOFT_DEPTH=1";
		draw_desc.name = "SceneParticleDrawAlphaSoft";
		m_draw_alpha_soft_program = renderer.create_graphics_program(draw_desc);
		ASH_PROCESS_ERROR(m_draw_alpha_soft_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool ParticleSystemPass::ensure_emitter_state(
		const EmitterKey& key,
		const VisibleParticleEmitter& emitter,
		uint64_t scene_content_epoch,
		EmitterGPUState*& out_state)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_state = nullptr;
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		const uint32_t capacity =
			std::clamp<uint32_t>(emitter.particle.max_particles, 1u, k_max_particles_per_emitter);
		const uint64_t simulation_config_hash =
			ParticleSystemInternal::build_simulation_config_hash(emitter.particle);
		EmitterGPUState& state = m_emitter_states[key];
		if (state.capacity != capacity ||
			state.scene_content_epoch != scene_content_epoch ||
			state.simulation_config_hash != simulation_config_hash)
		{
			clear_program_buffer_bindings();
			state = {};
			state.capacity = capacity;
			state.scene_content_epoch = scene_content_epoch;
			state.simulation_config_hash = simulation_config_hash;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(state.pools.size()); ++index)
		{
			if (!state.pools[index])
			{
				StorageBufferDesc pool_desc{};
				pool_desc.size = capacity * static_cast<uint32_t>(sizeof(ParticleGPUData));
				pool_desc.stride = static_cast<uint32_t>(sizeof(ParticleGPUData));
				pool_desc.name = index == 0 ? "SceneParticlePoolA" : "SceneParticlePoolB";
				state.pools[index] = m_renderer->create_storage_buffer(pool_desc);
				ASH_PROCESS_ERROR(state.pools[index] != nullptr);
			}
		}

		const uint32_t maximum_candidate_group_count =
			(capacity * 2u + k_compaction_group_size - 1u) / k_compaction_group_size;
		if (!state.block_counts)
		{
			StorageBufferDesc block_counts_desc{};
			block_counts_desc.size = maximum_candidate_group_count * static_cast<uint32_t>(sizeof(uint32_t));
			block_counts_desc.stride = sizeof(uint32_t);
			block_counts_desc.name = "SceneParticleBlockCounts";
			state.block_counts = m_renderer->create_storage_buffer(block_counts_desc);
			ASH_PROCESS_ERROR(state.block_counts != nullptr);
		}

		if (!state.block_offsets)
		{
			StorageBufferDesc block_offsets_desc{};
			block_offsets_desc.size = maximum_candidate_group_count * static_cast<uint32_t>(sizeof(uint32_t));
			block_offsets_desc.stride = sizeof(uint32_t);
			block_offsets_desc.name = "SceneParticleBlockOffsets";
			state.block_offsets = m_renderer->create_storage_buffer(block_offsets_desc);
			ASH_PROCESS_ERROR(state.block_offsets != nullptr);
		}

		if (!state.counter)
		{
			const uint32_t zero = 0;
			StorageBufferDesc counter_desc{};
			counter_desc.size = sizeof(uint32_t);
			counter_desc.stride = sizeof(uint32_t);
			counter_desc.initial_data = &zero;
			counter_desc.name = "SceneParticleCounter";
			state.counter = m_renderer->create_storage_buffer(counter_desc);
			ASH_PROCESS_ERROR(state.counter != nullptr);
		}

		if (!state.draw_args)
		{
			// AshDrawIndirectArgs { vertexCount=6, instanceCount=0, firstVertex=0, firstInstance=0 }
			const uint32_t initial_args[4] = { 6u, 0u, 0u, 0u };
			StorageBufferDesc args_desc{};
			args_desc.size = sizeof(initial_args);
			args_desc.stride = 0;
			args_desc.indirect_args = true;
			args_desc.initial_data = initial_args;
			args_desc.name = "SceneParticleDrawArgs";
			state.draw_args = m_renderer->create_storage_buffer(args_desc);
			ASH_PROCESS_ERROR(state.draw_args != nullptr);
		}

		out_state = &state;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void ParticleSystemPass::prune_stale_states(const VisibleRenderFrame& frame)
	{
		if (m_emitter_states.empty())
		{
			return;
		}
		std::unordered_set<uint64_t> live_ids{};
		live_ids.reserve(frame.particle_emitters.size());
		for (const VisibleParticleEmitter& emitter : frame.particle_emitters)
		{
			live_ids.insert(static_cast<uint64_t>(emitter.entity_id));
		}
		bool removed_any = false;
		for (auto iterator = m_emitter_states.begin(); iterator != m_emitter_states.end();)
		{
			const bool missing_from_current_scene =
				iterator->first.scene_runtime_id == frame.scene_runtime_id &&
				live_ids.count(iterator->first.entity_id) == 0;
			if (!missing_from_current_scene)
			{
				++iterator;
				continue;
			}
			iterator = m_emitter_states.erase(iterator);
			removed_any = true;
		}
		if (removed_any)
		{
			clear_program_buffer_bindings();
		}
	}

	void ParticleSystemPass::clear_soft_depth_bindings()
	{
		if (m_draw_additive_soft_program)
		{
			m_draw_additive_soft_program->set_texture("SceneDepth", nullptr);
		}
		if (m_draw_alpha_soft_program)
		{
			m_draw_alpha_soft_program->set_texture("SceneDepth", nullptr);
		}
	}

	void ParticleSystemPass::clear_program_buffer_bindings()
	{
		if (m_classify_program)
		{
			m_classify_program->set_storage_buffer("ParticlePoolIn", nullptr);
			m_classify_program->set_storage_buffer("ParticleArgsIn", nullptr);
			m_classify_program->set_rw_storage_buffer("ParticleBlockCounts", nullptr);
		}
		if (m_scan_blocks_program)
		{
			m_scan_blocks_program->set_storage_buffer("ParticleArgsIn", nullptr);
			m_scan_blocks_program->set_storage_buffer("ParticleBlockCountsIn", nullptr);
			m_scan_blocks_program->set_rw_storage_buffer("ParticleBlockOffsets", nullptr);
			m_scan_blocks_program->set_rw_storage_buffer("ParticleCounter", nullptr);
		}
		if (m_scatter_program)
		{
			m_scatter_program->set_storage_buffer("ParticlePoolIn", nullptr);
			m_scatter_program->set_storage_buffer("ParticleArgsIn", nullptr);
			m_scatter_program->set_storage_buffer("ParticleBlockOffsetsIn", nullptr);
			m_scatter_program->set_rw_storage_buffer("ParticlePoolOut", nullptr);
		}
		if (m_write_args_program)
		{
			m_write_args_program->set_storage_buffer("ParticleCounterIn", nullptr);
			m_write_args_program->set_rw_storage_buffer("ParticleDrawArgs", nullptr);
		}

		const auto clear_draw_bindings = [](std::unique_ptr<GraphicsProgram>& program)
		{
			if (!program)
			{
				return;
			}
			program->set_storage_buffer("ParticlePool", nullptr);
			program->set_texture("ParticleSprite", nullptr);
			program->set_sampler("ParticleSpriteSampler", std::shared_ptr<RenderSampler>{});
		};
		clear_draw_bindings(m_draw_additive_program);
		clear_draw_bindings(m_draw_alpha_program);
		clear_draw_bindings(m_draw_additive_soft_program);
		clear_draw_bindings(m_draw_alpha_soft_program);
		clear_soft_depth_bindings();
	}

	bool ParticleSystemPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		RenderGraphTextureRef depth,
		RenderGraphTextureRef scene_hdr_linear,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("ParticleSystemPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		prune_stale_states(frame);
		clear_soft_depth_bindings();
		if (frame.particle_emitters.empty())
		{
			return true;
		}

		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_classify_program && m_scan_blocks_program && m_scatter_program && m_write_args_program);
		ASH_PROCESS_ERROR(
			m_draw_additive_program &&
			m_draw_alpha_program &&
			m_draw_additive_soft_program &&
			m_draw_alpha_soft_program);
		ASH_PROCESS_ERROR(m_sprite_sampler != nullptr);
		ASH_PROCESS_ERROR(depth);
		ASH_PROCESS_ERROR(scene_hdr_linear);
		ASH_PROCESS_ERROR(frame.scene_runtime_id != 0);
		ASH_PROCESS_ERROR(frame.scene_content_epoch != 0);

		for (const VisibleParticleEmitter& emitter : frame.particle_emitters)
		{
			const EmitterKey key{ frame.scene_runtime_id, static_cast<uint64_t>(emitter.entity_id) };
			EmitterGPUState* state = nullptr;
			ASH_PROCESS_ERROR(ensure_emitter_state(key, emitter, frame.scene_content_epoch, state));
			ASH_PROCESS_ERROR(state != nullptr);

			// Simulate once per frame; extra views of the same frame only re-draw.
			const bool needs_simulate = !state->has_simulated || state->last_simulated_frame != frame.frame_index;
			const uint32_t draw_parity = needs_simulate ? (state->parity ^ 1u) : state->parity;
			if (needs_simulate)
			{
				const float delta_seconds = std::max(frame.delta_seconds, 0.0f);
				uint32_t spawn_count = 0;
				float next_spawn_accumulator = state->spawn_accumulator;
				if (emitter.particle.emitting && emitter.particle.spawn_rate > 0.0f)
				{
					next_spawn_accumulator += emitter.particle.spawn_rate * delta_seconds;
					const float spawn_floor = std::floor(next_spawn_accumulator);
					next_spawn_accumulator -= spawn_floor;
					spawn_count = static_cast<uint32_t>(std::min(spawn_floor, static_cast<float>(state->capacity)));
				}

				const ParticleSimulateConstants constants =
					make_simulate_constants(emitter, state->capacity, spawn_count, state->total_spawned, delta_seconds);
				const std::shared_ptr<StorageBuffer> pool_in = state->pools[state->parity];
				const std::shared_ptr<StorageBuffer> pool_out = state->pools[draw_parity];
				const std::shared_ptr<StorageBuffer> block_counts = state->block_counts;
				const std::shared_ptr<StorageBuffer> block_offsets = state->block_offsets;
				const std::shared_ptr<StorageBuffer> counter = state->counter;
				const std::shared_ptr<StorageBuffer> draw_args = state->draw_args;
				const uint32_t group_count = constants.candidate_group_count;
				const uint32_t next_total_spawned = state->total_spawned + spawn_count;

				ASH_PROCESS_ERROR(graph.add_compute_pass(
					"SceneParticleSimulatePass",
					RenderGraphPassFlags::NeverCull,
					RHI::GpuTimingMetric::Particles,
					[](RenderGraphComputePassBuilder&) {},
					[this,
						key,
						constants,
						pool_in,
						pool_out,
						block_counts,
						block_offsets,
						counter,
						draw_args,
						group_count,
						next_spawn_accumulator,
						next_total_spawned,
						frame_index = frame.frame_index](
						RenderGraphComputeContext& context) -> bool
					{
						ASH_PROFILE_SCOPE_NC("SceneParticleSimulatePass", AshEngine::Profile::Color::Draw);
						ASH_PROCESS_GUARD_RETURN(bool, pass_result, true, false);
						ASH_PROCESS_ERROR(m_classify_program->set_const_data_block(sizeof(constants), &constants));
						ASH_PROCESS_ERROR(m_classify_program->set_storage_buffer("ParticlePoolIn", pool_in));
						ASH_PROCESS_ERROR(m_classify_program->set_storage_buffer("ParticleArgsIn", draw_args));
						ASH_PROCESS_ERROR(m_classify_program->set_rw_storage_buffer("ParticleBlockCounts", block_counts));
						ComputeDispatchDesc classify_dispatch{};
						classify_dispatch.program = m_classify_program.get();
						classify_dispatch.group_count_x = group_count;
						ASH_PROCESS_ERROR(context.dispatch(classify_dispatch));

						ASH_PROCESS_ERROR(m_scan_blocks_program->set_const_data_block(sizeof(constants), &constants));
						ASH_PROCESS_ERROR(m_scan_blocks_program->set_storage_buffer("ParticleArgsIn", draw_args));
						ASH_PROCESS_ERROR(m_scan_blocks_program->set_storage_buffer("ParticleBlockCountsIn", block_counts));
						ASH_PROCESS_ERROR(m_scan_blocks_program->set_rw_storage_buffer("ParticleBlockOffsets", block_offsets));
						ASH_PROCESS_ERROR(m_scan_blocks_program->set_rw_storage_buffer("ParticleCounter", counter));
						ComputeDispatchDesc scan_blocks_dispatch{};
						scan_blocks_dispatch.program = m_scan_blocks_program.get();
						ASH_PROCESS_ERROR(context.dispatch(scan_blocks_dispatch));

						ASH_PROCESS_ERROR(m_scatter_program->set_const_data_block(sizeof(constants), &constants));
						ASH_PROCESS_ERROR(m_scatter_program->set_storage_buffer("ParticlePoolIn", pool_in));
						ASH_PROCESS_ERROR(m_scatter_program->set_storage_buffer("ParticleArgsIn", draw_args));
						ASH_PROCESS_ERROR(m_scatter_program->set_storage_buffer("ParticleBlockOffsetsIn", block_offsets));
						ASH_PROCESS_ERROR(m_scatter_program->set_rw_storage_buffer("ParticlePoolOut", pool_out));
						ComputeDispatchDesc scatter_dispatch{};
						scatter_dispatch.program = m_scatter_program.get();
						scatter_dispatch.group_count_x = group_count;
						ASH_PROCESS_ERROR(context.dispatch(scatter_dispatch));

						ASH_PROCESS_ERROR(m_write_args_program->set_const_data_block(sizeof(constants), &constants));
						ASH_PROCESS_ERROR(m_write_args_program->set_storage_buffer("ParticleCounterIn", counter));
						ASH_PROCESS_ERROR(m_write_args_program->set_rw_storage_buffer("ParticleDrawArgs", draw_args));
						ComputeDispatchDesc write_args_dispatch{};
						write_args_dispatch.program = m_write_args_program.get();
						ASH_PROCESS_ERROR(context.dispatch(write_args_dispatch));

						const auto state_found = m_emitter_states.find(key);
						ASH_PROCESS_ERROR(state_found != m_emitter_states.end());
						ASH_PROCESS_ERROR(state_found->second.capacity == constants.max_particles);
						state_found->second.parity ^= 1u;
						state_found->second.spawn_accumulator = next_spawn_accumulator;
						state_found->second.total_spawned = next_total_spawned;
						state_found->second.last_simulated_frame = frame_index;
						state_found->second.has_simulated = true;
						ASH_PROCESS_GUARD_RETURN_END(pass_result, false);
					}));
			}

			// One raster pass per emitter: queued draws resolve program bindings at
			// pass end, so sharing one pass across emitters would alias the pool bind.
			const bool soft_particles = emitter.particle.soft_particles;
			GraphicsProgram* draw_program = nullptr;
			if (emitter.particle.blend_mode == ParticleBlendMode::AlphaBlend)
			{
				draw_program = soft_particles
					? m_draw_alpha_soft_program.get()
					: m_draw_alpha_program.get();
			}
			else
			{
				draw_program = soft_particles
					? m_draw_additive_soft_program.get()
					: m_draw_additive_program.get();
			}
			const ParticleDrawConstants draw_constants = make_draw_constants(frame, emitter);
			const std::shared_ptr<StorageBuffer> particle_pool = state->pools[draw_parity];
			const std::shared_ptr<StorageBuffer> draw_args = state->draw_args;
			const std::shared_ptr<TextureAsset> sprite_texture = emitter.sprite_texture;

			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneParticleDrawPass",
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::Particles,
				[depth, scene_hdr_linear, soft_particles](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_depth(
						depth,
						soft_particles
							? RenderGraphDepthReadMode::DepthTestAndShaderResource
							: RenderGraphDepthReadMode::DepthTestOnly);
					pass.write_color(0, scene_hdr_linear, RenderLoadAction::Load, {});
				},
				[this,
					draw_program,
					draw_constants,
					particle_pool,
					draw_args,
					sprite_texture,
					depth,
					soft_particles,
					&view_context](
					RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneParticleDrawPass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, pass_result, true, false);
					ASH_PROCESS_ERROR(sprite_texture && sprite_texture->resource);
					ASH_PROCESS_ERROR(draw_program->set_storage_buffer("ParticlePool", particle_pool));
					ASH_PROCESS_ERROR(draw_program->set_texture("ParticleSprite", sprite_texture->resource));
					ASH_PROCESS_ERROR(draw_program->set_sampler("ParticleSpriteSampler", m_sprite_sampler));
					if (soft_particles)
					{
						std::shared_ptr<RenderTarget> depth_target = context.get_texture(depth);
						ASH_PROCESS_ERROR(depth_target != nullptr);
						ASH_PROCESS_ERROR(draw_program->set_texture("SceneDepth", depth_target));
					}
					GraphicsDrawDesc draw_desc{};
					draw_desc.program = draw_program;
					draw_desc.indirect_kind = GraphicsIndirectKind::NonIndexed;
					draw_desc.indirect_args_buffer = draw_args;
					draw_desc.indirect_args_offset = 0;
					ASH_PROCESS_ERROR(attach_root_constants(draw_desc, draw_program, draw_constants));
					apply_view_context_to_draw_desc(draw_desc, view_context);
					ASH_PROCESS_ERROR(context.draw(draw_desc));
					ASH_PROCESS_GUARD_RETURN_END(pass_result, false);
				}));
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
