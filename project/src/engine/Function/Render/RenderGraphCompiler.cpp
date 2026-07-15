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

			struct BufferTopologyKey
			{
				std::string name{};
				RenderGraphBufferDesc desc{};
				bool external = false;
				bool extracted = false;
				RenderGraphAccess initial_access = RenderGraphAccess::Unknown;
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

			struct BufferUsageTopologyKey
			{
				uint32_t buffer_index = 0;
				RenderGraphAccess access = RenderGraphAccess::Unknown;
				bool write = false;
			};

			struct PassTopologyKey
			{
				std::string name{};
				RenderGraphPassKind kind = RenderGraphPassKind::Raster;
				RenderGraphPassFlags flags = RenderGraphPassFlags::None;
				RHI::GpuTimingMetric timing_metric = RHI::GpuTimingMetric::Invalid;
				std::vector<TextureUsageTopologyKey> texture_usages{};
				std::vector<BufferUsageTopologyKey> buffer_usages{};
			};

			struct TopologyKey
			{
				std::vector<TextureTopologyKey> textures{};
				std::vector<BufferTopologyKey> buffers{};
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

		static void hash_buffer_topology(size_t& hash_value, const RenderGraphBufferNode& buffer)
		{
			ASH_HASH::hash_combine(hash_value, buffer.name);
			ASH_HASH::hash_combine(hash_value, buffer.desc.size);
			ASH_HASH::hash_combine(hash_value, buffer.desc.stride);
			ASH_HASH::hash_combine(hash_value, buffer.desc.shader_resource);
			ASH_HASH::hash_combine(hash_value, buffer.desc.unordered_access);
			ASH_HASH::hash_combine(hash_value, buffer.desc.indirect_args);
			ASH_HASH::hash_combine(hash_value, buffer.external);
			ASH_HASH::hash_combine(hash_value, buffer.extracted);
			ASH_HASH::hash_combine(hash_value, static_cast<uint16_t>(buffer.initial_access));
		}

		static void hash_pass_topology(size_t& hash_value, const RenderGraphPassNode& pass)
		{
			ASH_HASH::hash_combine(hash_value, pass.name);
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(pass.kind));
			ASH_HASH::hash_combine(hash_value, static_cast<uint32_t>(pass.flags));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(pass.timing_metric));
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
			if (!pass.buffer_usages.empty())
			{
				ASH_HASH::hash_combine(hash_value, static_cast<size_t>(0x42554646u));
				ASH_HASH::hash_combine(hash_value, pass.buffer_usages.size());
				for (const RenderGraphBufferUsage& usage : pass.buffer_usages)
				{
					ASH_HASH::hash_combine(hash_value, usage.buffer.index);
					ASH_HASH::hash_combine(hash_value, static_cast<uint16_t>(usage.access));
					ASH_HASH::hash_combine(hash_value, usage.write);
				}
			}
		}

		static size_t hash_render_graph_topology(
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphBufferNode>& buffers,
			const std::vector<RenderGraphPassNode>& passes)
		{
			size_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, textures.size());
			for (const RenderGraphTextureNode& texture : textures)
			{
				hash_texture_topology(hash_value, texture);
			}
			if (!buffers.empty())
			{
				ASH_HASH::hash_combine(hash_value, static_cast<size_t>(0x42554652u));
				ASH_HASH::hash_combine(hash_value, buffers.size());
				for (const RenderGraphBufferNode& buffer : buffers)
				{
					hash_buffer_topology(hash_value, buffer);
				}
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
			const std::vector<RenderGraphBufferNode>& buffers,
			const std::vector<RenderGraphPassNode>& passes)
		{
			RenderGraphCompileCacheState::TopologyKey key{};
			key.textures.reserve(textures.size());
			for (const RenderGraphTextureNode& texture : textures)
			{
				key.textures.push_back({ texture.name, texture.external, texture.extracted });
			}

			key.buffers.reserve(buffers.size());
			for (const RenderGraphBufferNode& buffer : buffers)
			{
				key.buffers.push_back({
					buffer.name,
					buffer.desc,
					buffer.external,
					buffer.extracted,
					buffer.initial_access });
			}

			key.passes.reserve(passes.size());
			for (const RenderGraphPassNode& pass : passes)
			{
				RenderGraphCompileCacheState::PassTopologyKey pass_key{};
				pass_key.name = pass.name;
				pass_key.kind = pass.kind;
				pass_key.flags = pass.flags;
				pass_key.timing_metric = pass.timing_metric;
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
				pass_key.buffer_usages.reserve(pass.buffer_usages.size());
				for (const RenderGraphBufferUsage& usage : pass.buffer_usages)
				{
					pass_key.buffer_usages.push_back({ usage.buffer.index, usage.access, usage.write });
				}
				key.passes.push_back(std::move(pass_key));
			}
			return key;
		}

		static bool render_graph_topology_matches(
			const RenderGraphCompileCacheState::TopologyKey& key,
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphBufferNode>& buffers,
			const std::vector<RenderGraphPassNode>& passes)
		{
			if (key.textures.size() != textures.size() ||
				key.buffers.size() != buffers.size() ||
				key.passes.size() != passes.size())
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

			for (size_t buffer_index = 0; buffer_index < buffers.size(); ++buffer_index)
			{
				const RenderGraphCompileCacheState::BufferTopologyKey& cached = key.buffers[buffer_index];
				const RenderGraphBufferNode& buffer = buffers[buffer_index];
				if (cached.name != buffer.name ||
					cached.desc.size != buffer.desc.size ||
					cached.desc.stride != buffer.desc.stride ||
					cached.desc.shader_resource != buffer.desc.shader_resource ||
					cached.desc.unordered_access != buffer.desc.unordered_access ||
					cached.desc.indirect_args != buffer.desc.indirect_args ||
					cached.external != buffer.external ||
					cached.extracted != buffer.extracted ||
					cached.initial_access != buffer.initial_access)
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
					cached.timing_metric != pass.timing_metric ||
					cached.texture_usages.size() != pass.texture_usages.size() ||
					cached.buffer_usages.size() != pass.buffer_usages.size())
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

				for (size_t usage_index = 0; usage_index < pass.buffer_usages.size(); ++usage_index)
				{
					const RenderGraphCompileCacheState::BufferUsageTopologyKey& cached_usage = cached.buffer_usages[usage_index];
					const RenderGraphBufferUsage& usage = pass.buffer_usages[usage_index];
					if (cached_usage.buffer_index != usage.buffer.index ||
						cached_usage.access != usage.access ||
						cached_usage.write != usage.write)
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
			const std::vector<RenderGraphBufferNode>& buffers,
			const std::vector<RenderGraphPassNode>& passes)
		{
			for (const RenderGraphCompileCacheState::Entry& entry : entries)
			{
				if (render_graph_topology_matches(entry.key, textures, buffers, passes))
				{
					return &entry;
				}
			}
			return nullptr;
		}
	}

	size_t RenderGraphCompiler::hash_topology(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes)
	{
		return hash_render_graph_topology(textures, buffers, passes);
	}

	size_t RenderGraphCompiler::hash_topology_for_tests(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes)
	{
		return hash_topology(textures, {}, passes);
	}

	size_t RenderGraphCompiler::hash_topology_for_tests(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes)
	{
		return hash_topology(textures, buffers, passes);
	}

	bool RenderGraphCompiler::compile_cached_in_bucket_for_tests(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		size_t topology_hash,
		RenderGraphCompileResult& out_result)
	{
		return compile_cached_in_bucket(textures, {}, passes, topology_hash, out_result);
	}

	bool RenderGraphCompiler::compile_cached_in_bucket_for_tests(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		size_t topology_hash,
		RenderGraphCompileResult& out_result)
	{
		return compile_cached_in_bucket(textures, buffers, passes, topology_hash, out_result);
	}

	bool RenderGraphCompiler::compile(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RenderGraphCompileResult& out_result)
	{
		return compile(textures, {}, passes, out_result);
	}

	bool RenderGraphCompiler::compile(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		RenderGraphCompileResult& out_result)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphCompiler::compile", AshEngine::Profile::Color::Submit);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(passes.size()));
		ASH_PROFILE_PLOT("RenderGraph/DeclaredPasses", static_cast<int64_t>(passes.size()));
		ASH_PROFILE_PLOT("RenderGraph/DeclaredTextures", static_cast<int64_t>(textures.size()));
		if (!buffers.empty())
		{
			ASH_PROFILE_PLOT("RenderGraph/DeclaredBuffers", static_cast<int64_t>(buffers.size()));
		}

		out_result = {};
		out_result.texture_lifetimes.resize(textures.size());
		out_result.buffer_lifetimes.resize(buffers.size());
		out_result.pass_barriers.resize(passes.size());

		std::vector<int32_t> producer_for_texture(textures.size(), -1);
		std::vector<int32_t> producer_for_buffer(buffers.size(), -1);
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

			std::vector<RHI::AshResourceState> pass_buffer_states(buffers.size(), RHI::AshResourceState::Unknown);
			for (const RenderGraphBufferUsage& usage : pass.buffer_usages)
			{
				if (!usage.buffer || usage.buffer.index >= buffers.size())
				{
					HLogError("RenderGraphCompiler: pass '{}' references an invalid buffer.", pass.name);
					valid = false;
					break;
				}

				const RenderGraphBufferNode& buffer = buffers[usage.buffer.index];
				if (usage.write)
				{
					const RenderGraphAccess expected_access = pass.kind == RenderGraphPassKind::Raster ?
						RenderGraphAccess::GraphicsUAV : RenderGraphAccess::ComputeUAV;
					if (usage.access != expected_access || !buffer.desc.unordered_access)
					{
						HLogError(
							"RenderGraphCompiler: pass '{}' has invalid write access '{}' for buffer '{}'.",
							pass.name,
							render_graph_access_name(usage.access),
							buffer.name);
						valid = false;
						break;
					}
				}
				else if (usage.access == RenderGraphAccess::IndirectArgs)
				{
					if (!buffer.desc.indirect_args)
					{
						HLogError(
							"RenderGraphCompiler: pass '{}' reads buffer '{}' as IndirectArgs without indirect usage.",
							pass.name,
							buffer.name);
						valid = false;
						break;
					}
				}
				else
				{
					const RenderGraphAccess expected_access = pass.kind == RenderGraphPassKind::Raster ?
						RenderGraphAccess::GraphicsSRV : RenderGraphAccess::ComputeSRV;
					if (usage.access != expected_access || !buffer.desc.shader_resource)
					{
						HLogError(
							"RenderGraphCompiler: pass '{}' has invalid read access '{}' for buffer '{}'.",
							pass.name,
							render_graph_access_name(usage.access),
							buffer.name);
						valid = false;
						break;
					}
				}

				const RHI::AshResourceState state = render_graph_access_to_rhi_state(usage.access);
				RHI::AshResourceState& declared_state = pass_buffer_states[usage.buffer.index];
				if (declared_state != RHI::AshResourceState::Unknown && declared_state != state)
				{
					HLogError(
						"RenderGraphCompiler: pass '{}' declares conflicting states for buffer '{}'.",
						pass.name,
						buffer.name);
					valid = false;
					break;
				}
				declared_state = state;

				if (usage.write)
				{
					pass_writes = true;
					producer_for_buffer[usage.buffer.index] = static_cast<int32_t>(pass_index);
				}
				else
				{
					pass_reads = true;
					const int32_t producer = producer_for_buffer[usage.buffer.index];
					if (producer >= 0)
					{
						const uint32_t producer_index = static_cast<uint32_t>(producer);
						std::vector<uint32_t>& dependencies = pass_read_dependencies[pass_index];
						if (std::find(dependencies.begin(), dependencies.end(), producer_index) == dependencies.end())
						{
							dependencies.push_back(producer_index);
						}
					}
					else if (!buffer.external)
					{
						HLogError(
							"RenderGraphCompiler: pass '{}' reads transient buffer '{}' before it is produced.",
							pass.name,
							buffer.name);
						valid = false;
						break;
					}
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

		for (uint32_t buffer_index = 0; buffer_index < buffers.size(); ++buffer_index)
		{
			if (!buffers[buffer_index].external && !buffers[buffer_index].extracted)
			{
				continue;
			}

			const int32_t producer = producer_for_buffer[buffer_index];
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

			for (const RenderGraphBufferUsage& usage : passes[pass_index].buffer_usages)
			{
				RenderGraphBufferLifetime& lifetime = out_result.buffer_lifetimes[usage.buffer.index];
				lifetime.used = true;
				lifetime.first_pass = std::min(lifetime.first_pass, pass_index);
				lifetime.last_pass = lifetime.last_pass == UINT32_MAX ? pass_index : std::max(lifetime.last_pass, pass_index);

				RenderGraphPassBarrierPlan& barrier_plan = out_result.pass_barriers[pass_index];
				if (barrier_plan.buffer_states.size() < buffers.size())
				{
					barrier_plan.buffer_states.resize(buffers.size(), RHI::AshResourceState::Unknown);
				}

				const RHI::AshResourceState state = render_graph_access_to_rhi_state(usage.access);
				RHI::AshResourceState& planned_state = barrier_plan.buffer_states[usage.buffer.index];
				if (planned_state == RHI::AshResourceState::Unknown)
				{
					planned_state = state;
					if (state != RHI::AshResourceState::Unknown)
					{
						barrier_plan.buffer_transitions.push_back({ usage.buffer, state });
					}
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
		return compile_cached(textures, {}, passes, out_result);
	}

	bool RenderGraphCompiler::compile_cached(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		RenderGraphCompileResult& out_result)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphCompiler::compile_cached", AshEngine::Profile::Color::Submit);
		return compile_cached_in_bucket(textures, buffers, passes, hash_topology(textures, buffers, passes), out_result);
	}

	bool RenderGraphCompiler::compile_cached_in_bucket(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		size_t topology_hash,
		RenderGraphCompileResult& out_result)
	{
		RenderGraphCompileCacheState& cache = compile_cache_state();
		{
			std::scoped_lock<std::mutex> lock(cache.mutex);
			const auto found = cache.results.find(topology_hash);
			if (found != cache.results.end())
			{
				if (const RenderGraphCompileCacheState::Entry* entry = find_cached_compile_entry(found->second, textures, buffers, passes))
				{
					out_result = entry->result;
					++cache.stats.hits;
					return true;
				}
			}
		}

		RenderGraphCompileResult compiled{};
		if (!compile(textures, buffers, passes, compiled))
		{
			return false;
		}

		{
			std::scoped_lock<std::mutex> lock(cache.mutex);
			const auto found = cache.results.find(topology_hash);
			if (found != cache.results.end())
			{
				if (const RenderGraphCompileCacheState::Entry* entry = find_cached_compile_entry(found->second, textures, buffers, passes))
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
			entries.push_back({ make_render_graph_topology_key(textures, buffers, passes), compiled });
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
