#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace AshEngine
{
	namespace
	{
		bool usage_is_write(const RenderGraphTextureUsage& usage)
		{
			return usage.access == RenderGraphAccess::ColorAttachmentWrite ||
				usage.access == RenderGraphAccess::DepthStencilWrite ||
				usage.access == RenderGraphAccess::GraphicsUAV ||
				usage.access == RenderGraphAccess::ComputeUAV ||
				usage.access == RenderGraphAccess::CopyDst;
		}

		bool usage_is_read(const RenderGraphTextureUsage& usage)
		{
			return usage.access == RenderGraphAccess::GraphicsSRV ||
				usage.access == RenderGraphAccess::ComputeSRV ||
				usage.access == RenderGraphAccess::DepthStencilRead ||
				usage.access == RenderGraphAccess::VertexBufferRead ||
				usage.access == RenderGraphAccess::IndexBufferRead ||
				usage.access == RenderGraphAccess::ConstantBufferRead ||
				usage.access == RenderGraphAccess::CopySrc ||
				usage.access == RenderGraphAccess::Present;
		}

		bool usage_loads_existing_attachment(const RenderGraphTextureUsage& usage)
		{
			return usage.load_action == RenderLoadAction::Load &&
				(usage.access == RenderGraphAccess::ColorAttachmentWrite ||
					usage.access == RenderGraphAccess::DepthStencilWrite);
		}

		struct RenderGraphCompileCacheState
		{
			std::mutex mutex{};
			struct TextureTopologyKey
			{
				std::string name{};
				bool external = false;
				bool extracted = false;
			};

			struct TextureUsageTopologyKey
			{
				uint32_t texture_index = 0;
				RenderGraphAccess access = RenderGraphAccess::Unknown;
				uint8_t color_slot = UINT8_MAX;
				bool depth = false;
				RenderLoadAction load_action = RenderLoadAction::Load;
				RenderGraphDepthReadMode depth_read_mode = RenderGraphDepthReadMode::DepthTestOnly;
			};

			struct PassTopologyKey
			{
				std::string name{};
				RenderGraphPassKind kind = RenderGraphPassKind::Raster;
				RenderGraphPassFlags flags = RenderGraphPassFlags::None;
				std::vector<TextureUsageTopologyKey> texture_usages{};
			};

			struct TopologyKey
			{
				std::vector<TextureTopologyKey> textures{};
				std::vector<PassTopologyKey> passes{};
			};

			struct Entry
			{
				TopologyKey key{};
				RenderGraphCompileResult result{};
			};

			std::unordered_map<size_t, std::vector<Entry>> results{};
			size_t entry_count = 0;
			RenderGraphCompileCacheStats stats{};
		};

		static RenderGraphCompileCacheState& compile_cache_state()
		{
			static RenderGraphCompileCacheState state{};
			return state;
		}

		static void hash_texture_topology(size_t& hash_value, const RenderGraphTextureNode& texture)
		{
			ASH_HASH::hash_combine(hash_value, texture.name);
			ASH_HASH::hash_combine(hash_value, texture.external);
			ASH_HASH::hash_combine(hash_value, texture.extracted);
		}

		static void hash_pass_topology(size_t& hash_value, const RenderGraphPassNode& pass)
		{
			ASH_HASH::hash_combine(hash_value, pass.name);
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(pass.kind));
			ASH_HASH::hash_combine(hash_value, static_cast<uint32_t>(pass.flags));
			ASH_HASH::hash_combine(hash_value, pass.texture_usages.size());
			for (const RenderGraphTextureUsage& usage : pass.texture_usages)
			{
				ASH_HASH::hash_combine(hash_value, usage.texture.index);
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.access));
				ASH_HASH::hash_combine(hash_value, usage.color_slot);
				ASH_HASH::hash_combine(hash_value, usage.depth);
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.load_action));
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.depth_read_mode));
			}
		}

		static size_t hash_render_graph_topology(
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphPassNode>& passes)
		{
			size_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, textures.size());
			for (const RenderGraphTextureNode& texture : textures)
			{
				hash_texture_topology(hash_value, texture);
			}
			ASH_HASH::hash_combine(hash_value, passes.size());
			for (const RenderGraphPassNode& pass : passes)
			{
				hash_pass_topology(hash_value, pass);
			}
			return hash_value;
		}

		static RenderGraphCompileCacheState::TopologyKey make_render_graph_topology_key(
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphPassNode>& passes)
		{
			RenderGraphCompileCacheState::TopologyKey key{};
			key.textures.reserve(textures.size());
			for (const RenderGraphTextureNode& texture : textures)
			{
				key.textures.push_back({ texture.name, texture.external, texture.extracted });
			}

			key.passes.reserve(passes.size());
			for (const RenderGraphPassNode& pass : passes)
			{
				RenderGraphCompileCacheState::PassTopologyKey pass_key{};
				pass_key.name = pass.name;
				pass_key.kind = pass.kind;
				pass_key.flags = pass.flags;
				pass_key.texture_usages.reserve(pass.texture_usages.size());
				for (const RenderGraphTextureUsage& usage : pass.texture_usages)
				{
					pass_key.texture_usages.push_back({
						usage.texture.index,
						usage.access,
						usage.color_slot,
						usage.depth,
						usage.load_action,
						usage.depth_read_mode });
				}
				key.passes.push_back(std::move(pass_key));
			}
			return key;
		}

		static bool render_graph_topology_matches(
			const RenderGraphCompileCacheState::TopologyKey& key,
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphPassNode>& passes)
		{
			if (key.textures.size() != textures.size() || key.passes.size() != passes.size())
			{
				return false;
			}

			for (size_t texture_index = 0; texture_index < textures.size(); ++texture_index)
			{
				const RenderGraphCompileCacheState::TextureTopologyKey& cached = key.textures[texture_index];
				const RenderGraphTextureNode& texture = textures[texture_index];
				if (cached.name != texture.name ||
					cached.external != texture.external ||
					cached.extracted != texture.extracted)
				{
					return false;
				}
			}

			for (size_t pass_index = 0; pass_index < passes.size(); ++pass_index)
			{
				const RenderGraphCompileCacheState::PassTopologyKey& cached = key.passes[pass_index];
				const RenderGraphPassNode& pass = passes[pass_index];
				if (cached.name != pass.name ||
					cached.kind != pass.kind ||
					cached.flags != pass.flags ||
					cached.texture_usages.size() != pass.texture_usages.size())
				{
					return false;
				}

				for (size_t usage_index = 0; usage_index < pass.texture_usages.size(); ++usage_index)
				{
					const RenderGraphCompileCacheState::TextureUsageTopologyKey& cached_usage = cached.texture_usages[usage_index];
					const RenderGraphTextureUsage& usage = pass.texture_usages[usage_index];
					if (cached_usage.texture_index != usage.texture.index ||
						cached_usage.access != usage.access ||
						cached_usage.color_slot != usage.color_slot ||
						cached_usage.depth != usage.depth ||
						cached_usage.load_action != usage.load_action ||
						cached_usage.depth_read_mode != usage.depth_read_mode)
					{
						return false;
					}
				}
			}

			return true;
		}

		static const RenderGraphCompileCacheState::Entry* find_cached_compile_entry(
			const std::vector<RenderGraphCompileCacheState::Entry>& entries,
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphPassNode>& passes)
		{
			for (const RenderGraphCompileCacheState::Entry& entry : entries)
			{
				if (render_graph_topology_matches(entry.key, textures, passes))
				{
					return &entry;
				}
			}
			return nullptr;
		}
	}

	bool RenderGraphCompiler::compile(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RenderGraphCompileResult& out_result)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphCompiler::compile", AshEngine::Profile::Color::Submit);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(passes.size()));
		ASH_PROFILE_PLOT("RenderGraph/DeclaredPasses", static_cast<int64_t>(passes.size()));
		ASH_PROFILE_PLOT("RenderGraph/DeclaredTextures", static_cast<int64_t>(textures.size()));

		out_result = {};
		out_result.texture_lifetimes.resize(textures.size());
		out_result.pass_barriers.resize(passes.size());

		std::vector<int32_t> producer_for_texture(textures.size(), -1);
		std::vector<std::vector<uint32_t>> pass_read_dependencies(passes.size());
		bool valid = true;

		for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
		{
			const RenderGraphPassNode& pass = passes[pass_index];
			bool pass_reads = false;
			bool pass_writes = false;

			for (const RenderGraphTextureUsage& usage : pass.texture_usages)
			{
				if (!usage.texture || usage.texture.index >= textures.size())
				{
					HLogError("RenderGraphCompiler: pass '{}' references an invalid texture.", pass.name);
					valid = false;
					break;
				}

				if (usage_is_read(usage) || usage_loads_existing_attachment(usage))
				{
					pass_reads = true;
					const int32_t producer = producer_for_texture[usage.texture.index];
					if (producer >= 0)
					{
						pass_read_dependencies[pass_index].push_back(static_cast<uint32_t>(producer));
					}
					else if (!textures[usage.texture.index].external)
					{
						HLogError(
							"RenderGraphCompiler: pass '{}' reads transient texture '{}' before it is produced.",
							pass.name,
							textures[usage.texture.index].name);
						valid = false;
					}
				}

				if (usage_is_write(usage))
				{
					pass_writes = true;
					producer_for_texture[usage.texture.index] = static_cast<int32_t>(pass_index);
				}
			}

			if (!valid)
			{
				break;
			}

			if (!pass_reads && !pass_writes && !has_render_graph_pass_flag(pass.flags, RenderGraphPassFlags::NeverCull))
			{
				HLogError("RenderGraphCompiler: pass '{}' has no resource usage and is not NeverCull.", pass.name);
				valid = false;
				break;
			}
		}

		if (!valid)
		{
			return false;
		}

		std::vector<bool> live_passes(passes.size(), false);
		auto mark_live = [&](auto& self, uint32_t pass_index) -> void
		{
			if (pass_index >= passes.size() || live_passes[pass_index])
			{
				return;
			}

			live_passes[pass_index] = true;
			for (const uint32_t producer_pass : pass_read_dependencies[pass_index])
			{
				self(self, producer_pass);
			}
		};

		for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
		{
			if (has_render_graph_pass_flag(passes[pass_index].flags, RenderGraphPassFlags::NeverCull))
			{
				mark_live(mark_live, pass_index);
			}
		}

		for (uint32_t texture_index = 0; texture_index < textures.size(); ++texture_index)
		{
			if (!textures[texture_index].external && !textures[texture_index].extracted)
			{
				continue;
			}

			const int32_t producer = producer_for_texture[texture_index];
			if (producer >= 0)
			{
				mark_live(mark_live, static_cast<uint32_t>(producer));
			}
		}

		uint32_t culled_pass_count = 0;
		for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
		{
			if (!live_passes[pass_index])
			{
				++culled_pass_count;
				HLogInfo("RenderGraphCompiler: culled pass '{}'.", passes[pass_index].name);
				continue;
			}

			out_result.live_pass_indices.push_back(pass_index);
			for (const RenderGraphTextureUsage& usage : passes[pass_index].texture_usages)
			{
				RenderGraphTextureLifetime& lifetime = out_result.texture_lifetimes[usage.texture.index];
				lifetime.used = true;
				lifetime.first_pass = std::min(lifetime.first_pass, pass_index);
				lifetime.last_pass = lifetime.last_pass == UINT32_MAX ? pass_index : std::max(lifetime.last_pass, pass_index);

				RenderGraphPassBarrierPlan& barrier_plan = out_result.pass_barriers[pass_index];
				if (barrier_plan.texture_states.size() < textures.size())
				{
					barrier_plan.texture_states.resize(textures.size(), RHI::AshResourceState::Unknown);
				}

				RHI::AshResourceState state = RHI::AshResourceState::Unknown;
				if (usage.depth && usage.access == RenderGraphAccess::DepthStencilRead)
				{
					state = render_graph_depth_read_state(usage.depth_read_mode);
				}
				else
				{
					state = render_graph_access_to_rhi_state(usage.access);
				}

				barrier_plan.texture_states[usage.texture.index] = state;
				if (state != RHI::AshResourceState::Unknown)
				{
					barrier_plan.transitions.push_back({ usage.texture, state });
				}
			}
		}

		ASH_PROFILE_PLOT("RenderGraph/LivePasses", static_cast<int64_t>(out_result.live_pass_indices.size()));
		ASH_PROFILE_PLOT("RenderGraph/CulledPasses", static_cast<int64_t>(culled_pass_count));
		return true;
	}

	bool RenderGraphCompiler::compile_cached(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RenderGraphCompileResult& out_result)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphCompiler::compile_cached", AshEngine::Profile::Color::Submit);
		const size_t topology_hash = hash_render_graph_topology(textures, passes);
		RenderGraphCompileCacheState& cache = compile_cache_state();
		{
			std::scoped_lock<std::mutex> lock(cache.mutex);
			const auto found = cache.results.find(topology_hash);
			if (found != cache.results.end())
			{
				if (const RenderGraphCompileCacheState::Entry* entry = find_cached_compile_entry(found->second, textures, passes))
				{
					out_result = entry->result;
					++cache.stats.hits;
					return true;
				}
			}
		}

		RenderGraphCompileResult compiled{};
		if (!compile(textures, passes, compiled))
		{
			return false;
		}

		{
			std::scoped_lock<std::mutex> lock(cache.mutex);
			const auto found = cache.results.find(topology_hash);
			if (found != cache.results.end())
			{
				if (const RenderGraphCompileCacheState::Entry* entry = find_cached_compile_entry(found->second, textures, passes))
				{
					out_result = entry->result;
					++cache.stats.hits;
					return true;
				}
			}

			if (cache.entry_count >= 64u)
			{
				cache.results.clear();
				cache.entry_count = 0;
			}
			std::vector<RenderGraphCompileCacheState::Entry>& entries = cache.results[topology_hash];
			entries.push_back({ make_render_graph_topology_key(textures, passes), compiled });
			++cache.entry_count;
			++cache.stats.misses;
		}
		out_result = std::move(compiled);
		return true;
	}

	void RenderGraphCompiler::reset_compile_cache_for_tests()
	{
		RenderGraphCompileCacheState& cache = compile_cache_state();
		std::scoped_lock<std::mutex> lock(cache.mutex);
		cache.results.clear();
		cache.entry_count = 0;
		cache.stats = {};
	}

	RenderGraphCompileCacheStats RenderGraphCompiler::get_compile_cache_stats_for_tests()
	{
		RenderGraphCompileCacheState& cache = compile_cache_state();
		std::scoped_lock<std::mutex> lock(cache.mutex);
		return cache.stats;
	}
}
