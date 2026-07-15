#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Scene/SceneComponents.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

namespace AshEngine
{
	class ComputeProgram;
	class GraphicsProgram;
	class Renderer;
	struct SceneRenderViewContext;
	struct VisibleParticleEmitter;
	struct VisibleRenderFrame;

	ASH_API uint32_t calculate_particle_capture_warmup_spawn_count(const ParticleComponent& particle);
	ASH_API uint32_t make_particle_random_seed(uint32_t random_seed, uint64_t entity_id);

	// Must match AshParticleData in ParticleSystem.hlsl (32 bytes).
	struct ParticleGPUData
	{
		glm::vec3 position{ 0.0f };
		float age = 0.0f;
		glm::vec3 velocity{ 0.0f };
		float lifetime = 0.0f;
	};
	static_assert(sizeof(ParticleGPUData) == 32u);

	// SDD-2026-07-10-gpu-particles：stable classify/scan/scatter compaction +
	// indirect billboard draw。每个 compute 阶段通过 SRV/UAV 状态变化形成跨后端合法的
	// pass 外 barrier；稳定前缀和保证 survivors 优先且输出顺序可重复。
	class ParticleSystemPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		void release_scene(uint64_t scene_runtime_id);
		bool is_capture_ready(const VisibleRenderFrame& frame) const;

		bool add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			RenderGraphTextureRef depth,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context);

	private:
		struct EmitterKey
		{
			uint64_t scene_runtime_id = 0;
			uint64_t entity_id = 0;

			bool operator==(const EmitterKey& other) const
			{
				return scene_runtime_id == other.scene_runtime_id && entity_id == other.entity_id;
			}
		};

		struct EmitterKeyHash
		{
			size_t operator()(const EmitterKey& key) const
			{
				size_t hash_value = std::hash<uint64_t>{}(key.scene_runtime_id);
				hash_value ^= std::hash<uint64_t>{}(key.entity_id) +
					0x9e3779b97f4a7c15ull + (hash_value << 6u) + (hash_value >> 2u);
				return hash_value;
			}
		};

		struct EmitterGPUState
		{
			uint32_t capacity = 0;
			// Index of the pool holding the latest simulated data (draw reads this one).
			uint32_t parity = 0;
			float spawn_accumulator = 0.0f;
			uint32_t total_spawned = 0;
			uint64_t scene_content_epoch = 0;
			uint64_t simulation_config_hash = 0;
			uint64_t last_simulated_frame = 0;
			bool has_simulated = false;
			std::array<std::shared_ptr<StorageBuffer>, 2> pools{};
			std::shared_ptr<StorageBuffer> block_counts = nullptr;
			std::shared_ptr<StorageBuffer> block_offsets = nullptr;
			std::shared_ptr<StorageBuffer> counter = nullptr;
			std::shared_ptr<StorageBuffer> draw_args = nullptr;
		};

		bool create_programs(Renderer& renderer);
		bool ensure_emitter_state(
			const EmitterKey& key,
			const VisibleParticleEmitter& emitter,
			uint64_t scene_content_epoch,
			EmitterGPUState*& out_state);
		void prune_stale_states(const VisibleRenderFrame& frame);
		void clear_soft_depth_bindings();
		void clear_program_buffer_bindings();

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<ComputeProgram> m_classify_program = nullptr;
		std::unique_ptr<ComputeProgram> m_scan_blocks_program = nullptr;
		std::unique_ptr<ComputeProgram> m_scatter_program = nullptr;
		std::unique_ptr<ComputeProgram> m_write_args_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_draw_additive_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_draw_alpha_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_draw_additive_soft_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_draw_alpha_soft_program = nullptr;
		std::shared_ptr<RenderSampler> m_sprite_sampler = nullptr;
		std::unordered_map<EmitterKey, EmitterGPUState, EmitterKeyHash> m_emitter_states{};
	};
}
