#include "RenderDevice.h"
#include "Graphics/Buffer.h"
#include "Graphics/CommandBuffer.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RenderPass.h"
#include "Graphics/RenderProgram.h"
#include "Graphics/Shader.h"
#include "Graphics/Swapchain.h"
#include "Graphics/Texture.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		struct RenderPassSignature
		{
			uint8_t color_count = 0;
			RHI::AshFormat color_formats[RHI::k_max_image_outputs]{};
			RHI::AshFormat depth_format = RHI::ASH_FORMAT_UNDEFINED;
		};

		struct BufferResource
		{
			std::shared_ptr<RHI::Buffer> buffer = nullptr;
			uint32_t size = 0;
			uint32_t stride = 0;
			RenderIndexFormat index_format = RenderIndexFormat::UInt32;
		};

		struct ImmutableConstantValue
		{
			enum class Type : uint8_t
			{
				Int = 0,
				UInt,
				Float
			};

			Type type = Type::UInt;
			union
			{
				int32_t value_i;
				uint32_t value_u;
				float value_f;
			};

			ImmutableConstantValue()
				: value_u(0)
			{
			}
		};

		static bool is_root_parameter_block_name(const std::string& name)
		{
			return name == "AshRootConstants" || name == "RootConstants";
		}

		static bool shader_parameter_members_match(const RHI::ShaderParameterBlockLayout& lhs, const RHI::ShaderParameterBlockLayout& rhs)
		{
			if (lhs.members.size() != rhs.members.size())
			{
				return false;
			}

			for (size_t index = 0; index < lhs.members.size(); ++index)
			{
				const RHI::ShaderParameterMember& lhsMember = lhs.members[index];
				const RHI::ShaderParameterMember& rhsMember = rhs.members[index];
				if (lhsMember.name != rhsMember.name ||
					lhsMember.offset != rhsMember.offset ||
					lhsMember.size != rhsMember.size ||
					lhsMember.array_size != rhsMember.array_size ||
					lhsMember.value_type != rhsMember.value_type)
				{
					return false;
				}
			}

			return true;
		}

		static const RHI::ShaderParameterBlockLayout* find_root_parameter_block(const std::shared_ptr<RHI::Shader>& shader)
		{
			if (!shader)
			{
				return nullptr;
			}

			for (const RHI::ShaderParameterBlockLayout& layout : shader->get_parameter_block_layouts())
			{
				if (is_root_parameter_block_name(layout.name))
				{
					return &layout;
				}
			}

			return nullptr;
		}

		static bool is_depth_format(RenderTextureFormat format)
		{
			return format == RenderTextureFormat::D24_UNORM_S8_UINT || format == RenderTextureFormat::D32_SFLOAT;
		}

		static RHI::AshFormat to_rhi_format(RenderTextureFormat format)
		{
			switch (format)
			{
			case RenderTextureFormat::RGBA8_UNORM:
				return RHI::ASH_FORMAT_R8G8B8A8_UNORM;
			case RenderTextureFormat::BGRA8_SRGB:
				return RHI::ASH_FORMAT_B8G8R8A8_SRGB;
			case RenderTextureFormat::RGBA16_SFLOAT:
				return RHI::ASH_FORMAT_R16G16B16A16_SFLOAT;
			case RenderTextureFormat::RGBA32_SFLOAT:
				return RHI::ASH_FORMAT_R32G32B32A32_SFLOAT;
			case RenderTextureFormat::D24_UNORM_S8_UINT:
				return RHI::ASH_FORMAT_D24_UNORM_S8_UINT;
			case RenderTextureFormat::D32_SFLOAT:
				return RHI::ASH_FORMAT_D32_SFLOAT;
			default:
				return RHI::ASH_FORMAT_UNDEFINED;
			}
		}

		static RenderTextureFormat from_rhi_format(RHI::AshFormat format)
		{
			switch (format)
			{
			case RHI::ASH_FORMAT_R8G8B8A8_UNORM:
				return RenderTextureFormat::RGBA8_UNORM;
			case RHI::ASH_FORMAT_B8G8R8A8_SRGB:
				return RenderTextureFormat::BGRA8_SRGB;
			case RHI::ASH_FORMAT_R16G16B16A16_SFLOAT:
				return RenderTextureFormat::RGBA16_SFLOAT;
			case RHI::ASH_FORMAT_R32G32B32A32_SFLOAT:
				return RenderTextureFormat::RGBA32_SFLOAT;
			case RHI::ASH_FORMAT_D24_UNORM_S8_UINT:
				return RenderTextureFormat::D24_UNORM_S8_UINT;
			case RHI::ASH_FORMAT_D32_SFLOAT:
				return RenderTextureFormat::D32_SFLOAT;
			default:
				return RenderTextureFormat::Unknown;
			}
		}

		static RHI::AshLoadOption to_rhi_load_action(RenderLoadAction action)
		{
			switch (action)
			{
			case RenderLoadAction::Load:
				return RHI::AshLoadOption::ASH_LOAD_LOAD;
			case RenderLoadAction::Clear:
				return RHI::AshLoadOption::ASH_LOAD_CLEAR;
			default:
				return RHI::AshLoadOption::ASH_LOAD_DONT_CARE;
			}
		}

		static RHI::AshCullModeFlagBits to_rhi_cull_mode(RenderCullMode mode)
		{
			switch (mode)
			{
			case RenderCullMode::Front:
				return RHI::ASH_CULL_MODE_FRONT_BIT;
			case RenderCullMode::Back:
				return RHI::ASH_CULL_MODE_BACK_BIT;
			default:
				return RHI::ASH_CULL_MODE_NONE;
			}
		}

		static RHI::AshPrimitiveTopology to_rhi_topology(RenderPrimitiveTopology topology)
		{
			switch (topology)
			{
			case RenderPrimitiveTopology::TriangleStrip:
				return RHI::ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			default:
				return RHI::ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			}
		}

		static RenderPrimitiveTopology from_rhi_topology(RHI::AshPrimitiveTopology topology)
		{
			switch (topology)
			{
			case RHI::ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
				return RenderPrimitiveTopology::TriangleStrip;
			default:
				return RenderPrimitiveTopology::TriangleList;
			}
		}

		static RHI::AshIndexType to_rhi_index_type(RenderIndexFormat format)
		{
			return format == RenderIndexFormat::UInt16 ? RHI::ASH_INDEX_TYPE_UINT16 : RHI::ASH_INDEX_TYPE_UINT32;
		}

		static RHI::AshSamplerState to_rhi_sampler_state(RenderSamplerState state)
		{
			switch (state)
			{
			case RenderSamplerState::Default:
			default:
				return RHI::ASH_SAMPLER_STATE_DEFAULT;
			}
		}

		static RHI::Viewport to_rhi_viewport(const RenderViewport& viewport)
		{
			RHI::Viewport rhi_viewport{};
			rhi_viewport.rect = { viewport.x, viewport.y, viewport.width, viewport.height };
			rhi_viewport.min_depth = viewport.min_depth;
			rhi_viewport.max_depth = viewport.max_depth;
			return rhi_viewport;
		}

		static RHI::Rect2DInt to_rhi_scissor(const RenderScissor& scissor)
		{
			return { scissor.x, scissor.y, scissor.width, scissor.height };
		}

		static uint64_t hash_render_pass_signature(const RenderPassSignature& signature)
		{
			uint64_t hash_value = signature.color_count;
			for (uint32_t i = 0; i < signature.color_count; ++i)
			{
				hash_value = (hash_value * 16777619ull) ^ static_cast<uint64_t>(signature.color_formats[i]);
			}
			hash_value = (hash_value * 16777619ull) ^ static_cast<uint64_t>(signature.depth_format);
			return hash_value;
		}

		static uint64_t hash_render_target_desc(const RenderTargetDesc& desc)
		{
			uint64_t hash_value = desc.width;
			hash_value = (hash_value * 16777619ull) ^ desc.height;
			hash_value = (hash_value * 16777619ull) ^ static_cast<uint64_t>(desc.format);
			hash_value = (hash_value * 16777619ull) ^ static_cast<uint64_t>(desc.shader_resource ? 1 : 0);
			hash_value = (hash_value * 16777619ull) ^ static_cast<uint64_t>(desc.unordered_access ? 1 : 0);
			return hash_value;
		}

		static std::shared_ptr<RHI::CommandBuffer> make_command_buffer_ref(RHI::CommandBuffer* command_buffer)
		{
			return std::shared_ptr<RHI::CommandBuffer>(command_buffer, [](RHI::CommandBuffer*) {});
		}

		static constexpr RenderTextureFormat k_public_back_buffer_format = RenderTextureFormat::BGRA8_SRGB;
		static constexpr const char* k_public_back_buffer_name = "EngineBackBufferOffscreen";
	}

	class RenderTarget::Impl
	{
	public:
		enum class Kind : uint8_t
		{
			BackBuffer = 0,
			Texture
		};

		Kind kind = Kind::BackBuffer;
		RHI::Swapchain* swapchain = nullptr;
		std::shared_ptr<RHI::Texture> texture = nullptr;
		bool shader_resource = false;
		bool unordered_access = false;
		bool depth_stencil = false;
		RenderTextureFormat format = RenderTextureFormat::Unknown;

		std::shared_ptr<RHI::Texture> get_texture() const
		{
			return kind == Kind::BackBuffer && swapchain ? swapchain->get_swapchain_buffer() : texture;
		}

		RHI::AshResourceState get_final_resource_state() const
		{
			if (kind == Kind::BackBuffer)
			{
				return RHI::AshResourceState::Present;
			}
			if (unordered_access)
			{
				return RHI::AshResourceState::UAVGraphics;
			}
			if (shader_resource)
			{
				return RHI::AshResourceState::SRVGraphics;
			}
			return depth_stencil ? RHI::AshResourceState::DSVWrite : RHI::AshResourceState::RTV;
		}
	};

	class UniformBuffer::Impl
	{
	public:
		std::shared_ptr<BufferResource> resource = nullptr;
	};

	class VertexBuffer::Impl
	{
	public:
		std::shared_ptr<BufferResource> resource = nullptr;
	};

	class IndexBuffer::Impl
	{
	public:
		std::shared_ptr<BufferResource> resource = nullptr;
	};

	class StorageBuffer::Impl
	{
	public:
		std::shared_ptr<BufferResource> resource = nullptr;
	};

	struct ProgramBindingState
	{
		std::unordered_map<std::string, std::shared_ptr<UniformBuffer::Impl>> uniform_buffers;
		std::unordered_map<std::string, std::shared_ptr<StorageBuffer::Impl>> storage_buffer_srvs;
		std::unordered_map<std::string, std::vector<std::shared_ptr<StorageBuffer::Impl>>> storage_buffer_srv_arrays;
		std::unordered_map<std::string, std::shared_ptr<StorageBuffer::Impl>> storage_buffer_uavs;
		std::unordered_map<std::string, std::vector<std::shared_ptr<StorageBuffer::Impl>>> storage_buffer_uav_arrays;
		std::unordered_map<std::string, std::shared_ptr<RenderTarget::Impl>> texture_srvs;
		std::unordered_map<std::string, std::vector<std::shared_ptr<RenderTarget::Impl>>> texture_srv_arrays;
		std::unordered_map<std::string, std::shared_ptr<RenderTarget::Impl>> texture_uavs;
		std::unordered_map<std::string, std::vector<std::shared_ptr<RenderTarget::Impl>>> texture_uav_arrays;
		std::unordered_map<std::string, RenderSamplerState> samplers;
		std::unordered_map<std::string, std::vector<RenderSamplerState>> sampler_arrays;
		std::unordered_map<std::string, ImmutableConstantValue> immutable_constants;
		std::vector<uint8_t> const_data_block;
		const RHI::ShaderParameterBlockLayout* const_parameter_block = nullptr;
	};

	class GraphicsProgram::Impl
	{
	public:
		GraphicsProgramState state{};
		std::string shader_path;
		std::string vertex_entry = "VSMain";
		std::string fragment_entry = "PSMain";
		std::string shader_macro;
		std::string name;
		std::shared_ptr<RHI::Shader> vertex_shader = nullptr;
		std::shared_ptr<RHI::Shader> fragment_shader = nullptr;
		std::unordered_map<uint64_t, std::unique_ptr<RHI::IGraphicsRenderProgram>> programs;
		ProgramBindingState bindings;
	};

	class ComputeProgram::Impl
	{
	public:
		std::string shader_path;
		std::string compute_entry = "CSMain";
		std::string shader_macro;
		std::string name;
		std::shared_ptr<RHI::Shader> compute_shader = nullptr;
		std::unique_ptr<RHI::IComputeRenderProgram> program = nullptr;
		ProgramBindingState bindings;
	};

	class RenderDevice::Impl
	{
	public:
		RHI::GraphicsContext* graphics_context = nullptr;
		RHI::Swapchain* swapchain = nullptr;
		RHI::CommandBuffer* current_command_buffer = nullptr;
		std::shared_ptr<RHI::Framebuffer> current_framebuffer = nullptr;
		std::shared_ptr<RHI::RenderPass> current_render_pass = nullptr;
		RenderPassSignature current_pass_signature{};
		std::shared_ptr<RenderTarget::Impl> back_buffer_target = nullptr;
		std::shared_ptr<RenderTarget::Impl> swapchain_target = nullptr;
		std::unordered_map<uint64_t, std::vector<std::shared_ptr<RenderTarget>>> transient_render_target_pool;
		bool viewport_override_active = false;
		RenderViewport viewport_override{};
		bool scissor_override_active = false;
		RenderScissor scissor_override{};
		bool back_buffer_written_this_frame = false;
	};

	static std::shared_ptr<RenderTarget::Impl> create_render_target_impl(
		RHI::GraphicsContext* graphics_context,
		const RenderTargetDesc& desc)
	{
		if (!graphics_context || desc.width == 0 || desc.height == 0 || desc.format == RenderTextureFormat::Unknown)
		{
			return nullptr;
		}

		RHI::TextureCreation texture_creation{};
		texture_creation.width = desc.width;
		texture_creation.height = desc.height;
		texture_creation.depth = 1;
		texture_creation.array_layer_count = 1;
		texture_creation.mip_level_count = 1;
		texture_creation.format = to_rhi_format(desc.format);
		texture_creation.type = RHI::Ash_Texture2D;
		texture_creation.initial_state = RHI::AshResourceState::Unknown;
		texture_creation.memoryType = RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		texture_creation.name = desc.name;

		const bool depth_stencil = is_depth_format(desc.format);
		texture_creation.uUsageFlags = depth_stencil ? RHI::ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : RHI::ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
		if (desc.shader_resource)
		{
			texture_creation.uUsageFlags |= RHI::ASH_TEXTURE_USAGE_SAMPLED_BIT;
		}
		if (desc.unordered_access)
		{
			texture_creation.uUsageFlags |= RHI::ASH_TEXTURE_USAGE_STORAGE_BIT;
		}

		std::shared_ptr<RHI::Texture> texture = graphics_context->create_texture(texture_creation);
		if (!texture)
		{
			return nullptr;
		}

		auto impl = std::make_shared<RenderTarget::Impl>();
		impl->kind = RenderTarget::Impl::Kind::Texture;
		impl->texture = texture;
		impl->shader_resource = desc.shader_resource;
		impl->unordered_access = desc.unordered_access;
		impl->depth_stencil = depth_stencil;
		impl->format = desc.format;
		return impl;
	}

	static void apply_program_state(GraphicsProgram::Impl& impl, RHI::IGraphicsRenderProgram& program)
	{
		program.apply_render_state([&impl](RHI::RenderState* render_state) {
			render_state->rasterization.cull_mode = to_rhi_cull_mode(impl.state.cull_mode);
			render_state->primitive_topology = to_rhi_topology(impl.state.primitive_topology);
			render_state->clear_viewport_state();
			if (impl.state.depth_test)
			{
				render_state->depth_stencil.depth_enable = 1;
				render_state->depth_stencil.depth_write_enable = impl.state.depth_write ? 1 : 0;
				render_state->depth_stencil.depth_comparison = RHI::ASH_COMPARE_OP_LESS_OR_EQUAL;
			}
			else
			{
				render_state->depth_stencil.depth_enable = 0;
				render_state->depth_stencil.depth_write_enable = 0;
			}
		});
	}

	static bool set_program_const_data(ProgramBindingState& bindings, uint32_t size, const void* data)
	{
		if (size > 0 && !data)
		{
			return false;
		}

		bindings.const_data_block.resize(size);
		if (size > 0)
		{
			std::memcpy(bindings.const_data_block.data(), data, size);
		}
		return true;
	}

	static const RHI::ShaderParameterMember* find_parameter_member(const ProgramBindingState& bindings, const char* name)
	{
		if (!bindings.const_parameter_block || !name)
		{
			return nullptr;
		}

		for (const RHI::ShaderParameterMember& member : bindings.const_parameter_block->members)
		{
			if (member.name == name)
			{
				return &member;
			}
		}

		return nullptr;
	}

	template <typename TValue>
	static bool write_parameter_member(ProgramBindingState& bindings, const char* name, const TValue& value)
	{
		const RHI::ShaderParameterMember* member = find_parameter_member(bindings, name);
		if (!member)
		{
			return false;
		}
		if (!bindings.const_parameter_block)
		{
			return false;
		}
		if (member->offset + sizeof(TValue) > bindings.const_parameter_block->byte_size)
		{
			return false;
		}

		if (bindings.const_data_block.size() < bindings.const_parameter_block->byte_size)
		{
			bindings.const_data_block.resize(bindings.const_parameter_block->byte_size, 0u);
		}

		std::memcpy(bindings.const_data_block.data() + member->offset, &value, sizeof(TValue));
		return true;
	}

	static bool collect_buffer_views(
		const std::vector<std::shared_ptr<StorageBuffer::Impl>>& buffer_impls,
		bool unordered_access,
		std::vector<std::shared_ptr<RHI::BufferView>>& out_views)
	{
		out_views.clear();
		out_views.reserve(buffer_impls.size());
		for (const auto& buffer_impl : buffer_impls)
		{
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				return false;
			}
			std::shared_ptr<RHI::BufferView> view = unordered_access ?
				buffer_impl->resource->buffer->get_default_uav() :
				buffer_impl->resource->buffer->get_default_srv();
			if (!view)
			{
				return false;
			}
			out_views.push_back(view);
		}
		return true;
	}

	static bool collect_texture_views(
		const std::vector<std::shared_ptr<RenderTarget::Impl>>& texture_impls,
		bool unordered_access,
		std::vector<std::shared_ptr<RHI::TextureView>>& out_views)
	{
		out_views.clear();
		out_views.reserve(texture_impls.size());
		for (const auto& texture_impl : texture_impls)
		{
			if (!texture_impl)
			{
				return false;
			}
			std::shared_ptr<RHI::Texture> texture = texture_impl->get_texture();
			if (!texture)
			{
				return false;
			}
			std::shared_ptr<RHI::TextureView> view = unordered_access ? texture->get_default_uav() : texture->get_default_srv();
			if (!view)
			{
				return false;
			}
			out_views.push_back(view);
		}
		return true;
	}

	static bool collect_samplers(
		RHI::GraphicsContext* graphics_context,
		const std::vector<RenderSamplerState>& sampler_states,
		std::vector<std::shared_ptr<RHI::Sampler>>& out_samplers)
	{
		out_samplers.clear();
		out_samplers.reserve(sampler_states.size());
		for (RenderSamplerState sampler_state : sampler_states)
		{
			std::shared_ptr<RHI::Sampler> sampler = graphics_context->get_sampler(to_rhi_sampler_state(sampler_state));
			if (!sampler)
			{
				return false;
			}
			out_samplers.push_back(sampler);
		}
		return true;
	}

	static bool apply_program_bindings(ProgramBindingState& bindings, RHI::GraphicsContext* graphics_context, RHI::IRenderProgramBinder& binder)
	{
		bool success = true;
		for (const auto& [name, value] : bindings.immutable_constants)
		{
			switch (value.type)
			{
			case ImmutableConstantValue::Type::Int:
				binder.set_immutable_const_value_int(name.c_str(), value.value_i);
				break;
			case ImmutableConstantValue::Type::UInt:
				binder.set_immutable_const_value_uint(name.c_str(), value.value_u);
				break;
			case ImmutableConstantValue::Type::Float:
				binder.set_immutable_const_value_float(name.c_str(), value.value_f);
				break;
			}
		}

		for (const auto& [name, buffer_impl] : bindings.uniform_buffers)
		{
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				success = false;
				break;
			}
			binder.add_bind_cbv(name.c_str(), buffer_impl->resource->buffer->get_default_cbv());
		}

		for (const auto& [name, buffer_impl] : bindings.storage_buffer_srvs)
		{
			if (!success)
			{
				break;
			}
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				success = false;
				break;
			}
			std::shared_ptr<RHI::BufferView> view = buffer_impl->resource->buffer->get_default_srv();
			if (!view)
			{
				success = false;
				break;
			}
			binder.add_bind_srv(name.c_str(), view);
		}

		for (const auto& [name, buffer_impls] : bindings.storage_buffer_srv_arrays)
		{
			if (!success)
			{
				break;
			}
			std::vector<std::shared_ptr<RHI::BufferView>> views;
			if (!collect_buffer_views(buffer_impls, false, views))
			{
				success = false;
				break;
			}
			binder.add_bind_srv_array(name.c_str(), views);
		}

		for (const auto& [name, buffer_impl] : bindings.storage_buffer_uavs)
		{
			if (!success)
			{
				break;
			}
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				success = false;
				break;
			}
			std::shared_ptr<RHI::BufferView> view = buffer_impl->resource->buffer->get_default_uav();
			if (!view)
			{
				success = false;
				break;
			}
			binder.add_bind_uav(name.c_str(), view);
		}

		for (const auto& [name, buffer_impls] : bindings.storage_buffer_uav_arrays)
		{
			if (!success)
			{
				break;
			}
			std::vector<std::shared_ptr<RHI::BufferView>> views;
			if (!collect_buffer_views(buffer_impls, true, views))
			{
				success = false;
				break;
			}
			binder.add_bind_uav_array(name.c_str(), views);
		}

		for (const auto& [name, texture_impl] : bindings.texture_srvs)
		{
			if (!success)
			{
				break;
			}
			if (!texture_impl)
			{
				success = false;
				break;
			}
			std::shared_ptr<RHI::Texture> texture = texture_impl->get_texture();
			if (!texture || !texture->get_default_srv())
			{
				success = false;
				break;
			}
			binder.add_bind_srv(name.c_str(), texture->get_default_srv());
		}

		for (const auto& [name, texture_impls] : bindings.texture_srv_arrays)
		{
			if (!success)
			{
				break;
			}
			std::vector<std::shared_ptr<RHI::TextureView>> views;
			if (!collect_texture_views(texture_impls, false, views))
			{
				success = false;
				break;
			}
			binder.add_bind_srv_array(name.c_str(), views);
		}

		for (const auto& [name, texture_impl] : bindings.texture_uavs)
		{
			if (!success)
			{
				break;
			}
			if (!texture_impl)
			{
				success = false;
				break;
			}
			std::shared_ptr<RHI::Texture> texture = texture_impl->get_texture();
			if (!texture || !texture->get_default_uav())
			{
				success = false;
				break;
			}
			binder.add_bind_uav(name.c_str(), texture->get_default_uav());
		}

		for (const auto& [name, texture_impls] : bindings.texture_uav_arrays)
		{
			if (!success)
			{
				break;
			}
			std::vector<std::shared_ptr<RHI::TextureView>> views;
			if (!collect_texture_views(texture_impls, true, views))
			{
				success = false;
				break;
			}
			binder.add_bind_uav_array(name.c_str(), views);
		}

		for (const auto& [name, sampler_state] : bindings.samplers)
		{
			if (!success)
			{
				break;
			}
			std::shared_ptr<RHI::Sampler> sampler = graphics_context->get_sampler(to_rhi_sampler_state(sampler_state));
			if (!sampler)
			{
				success = false;
				break;
			}
			binder.add_bind_sampler(name.c_str(), sampler);
		}

		for (const auto& [name, sampler_states] : bindings.sampler_arrays)
		{
			if (!success)
			{
				break;
			}
			std::vector<std::shared_ptr<RHI::Sampler>> samplers;
			if (!collect_samplers(graphics_context, sampler_states, samplers))
			{
				success = false;
				break;
			}
			binder.add_bind_sampler_array(name.c_str(), samplers);
		}

		return success;
	}

	static bool resolve_graphics_parameter_block_layout(
		const std::shared_ptr<RHI::Shader>& vertex_shader,
		const std::shared_ptr<RHI::Shader>& fragment_shader,
		const RHI::ShaderParameterBlockLayout*& out_layout)
	{
		const RHI::ShaderParameterBlockLayout* vertex_layout = find_root_parameter_block(vertex_shader);
		const RHI::ShaderParameterBlockLayout* fragment_layout = find_root_parameter_block(fragment_shader);

		if (vertex_layout && fragment_layout)
		{
			if (vertex_layout->name != fragment_layout->name ||
				vertex_layout->bind_point != fragment_layout->bind_point ||
				vertex_layout->bind_space != fragment_layout->bind_space ||
				vertex_layout->byte_size != fragment_layout->byte_size ||
				!shader_parameter_members_match(*vertex_layout, *fragment_layout))
			{
				return false;
			}
			out_layout = vertex_layout;
			return true;
		}

		out_layout = vertex_layout ? vertex_layout : fragment_layout;
		return true;
	}

	template <typename ProgramType>
	static bool commit_program_bindings(ProgramBindingState& bindings, RHI::GraphicsContext* graphics_context, ProgramType& program)
	{
		RHI::IRenderProgramBinder& binder = program.begin_bind();
		if (!apply_program_bindings(bindings, graphics_context, binder))
		{
			return false;
		}
		if (!program.end_bind())
		{
			return false;
		}
		const uint32_t const_data_size = static_cast<uint32_t>(bindings.const_data_block.size());
		const void* const_data = const_data_size > 0 ? bindings.const_data_block.data() : nullptr;
		return program.set_const_data_block(const_data_size, const_data);
	}

	static void append_buffer_barrier(std::vector<RHI::AshBarrier>& barriers, const std::shared_ptr<BufferResource>& resource, RHI::AshResourceState state)
	{
		if (resource && resource->buffer)
		{
			barriers.emplace_back(resource->buffer, state);
		}
	}

	static void append_texture_barrier(std::vector<RHI::AshBarrier>& barriers, const std::shared_ptr<RenderTarget::Impl>& render_target, RHI::AshResourceState state)
	{
		if (!render_target)
		{
			return;
		}

		std::shared_ptr<RHI::Texture> texture = render_target->get_texture();
		if (texture)
		{
			barriers.emplace_back(texture, state);
		}
	}

	static bool collect_program_resource_barriers(const ProgramBindingState& bindings, bool graphics_pipeline, std::vector<RHI::AshBarrier>& out_barriers)
	{
		const RHI::AshResourceState srv_state = graphics_pipeline ? RHI::AshResourceState::SRVGraphics : RHI::AshResourceState::SRVCompute;
		const RHI::AshResourceState uav_state = graphics_pipeline ? RHI::AshResourceState::UAVGraphics : RHI::AshResourceState::UAVCompute;

		for (const auto& [name, buffer_impl] : bindings.uniform_buffers)
		{
			(void)name;
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				return false;
			}
			append_buffer_barrier(out_barriers, buffer_impl->resource, RHI::AshResourceState::ConstBuffer);
		}

		for (const auto& [name, buffer_impl] : bindings.storage_buffer_srvs)
		{
			(void)name;
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				return false;
			}
			append_buffer_barrier(out_barriers, buffer_impl->resource, srv_state);
		}

		for (const auto& [name, buffer_impls] : bindings.storage_buffer_srv_arrays)
		{
			(void)name;
			for (const auto& buffer_impl : buffer_impls)
			{
				if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
				{
					return false;
				}
				append_buffer_barrier(out_barriers, buffer_impl->resource, srv_state);
			}
		}

		for (const auto& [name, buffer_impl] : bindings.storage_buffer_uavs)
		{
			(void)name;
			if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
			{
				return false;
			}
			append_buffer_barrier(out_barriers, buffer_impl->resource, uav_state);
		}

		for (const auto& [name, buffer_impls] : bindings.storage_buffer_uav_arrays)
		{
			(void)name;
			for (const auto& buffer_impl : buffer_impls)
			{
				if (!buffer_impl || !buffer_impl->resource || !buffer_impl->resource->buffer)
				{
					return false;
				}
				append_buffer_barrier(out_barriers, buffer_impl->resource, uav_state);
			}
		}

		for (const auto& [name, texture_impl] : bindings.texture_srvs)
		{
			(void)name;
			if (!texture_impl || !texture_impl->get_texture())
			{
				return false;
			}
			append_texture_barrier(out_barriers, texture_impl, srv_state);
		}

		for (const auto& [name, texture_impls] : bindings.texture_srv_arrays)
		{
			(void)name;
			for (const auto& texture_impl : texture_impls)
			{
				if (!texture_impl || !texture_impl->get_texture())
				{
					return false;
				}
				append_texture_barrier(out_barriers, texture_impl, srv_state);
			}
		}

		for (const auto& [name, texture_impl] : bindings.texture_uavs)
		{
			(void)name;
			if (!texture_impl || !texture_impl->get_texture())
			{
				return false;
			}
			append_texture_barrier(out_barriers, texture_impl, uav_state);
		}

		for (const auto& [name, texture_impls] : bindings.texture_uav_arrays)
		{
			(void)name;
			for (const auto& texture_impl : texture_impls)
			{
				if (!texture_impl || !texture_impl->get_texture())
				{
					return false;
				}
				append_texture_barrier(out_barriers, texture_impl, uav_state);
			}
		}

		return true;
	}

		static bool submit_resource_barriers(RHI::CommandBuffer* command_buffer, const std::vector<RHI::AshBarrier>& barriers)
		{
			if (!command_buffer || barriers.empty())
			{
				return command_buffer != nullptr;
			}
			return command_buffer->cmd_transition_resource_state(barriers.data(), static_cast<uint32_t>(barriers.size()));
		}

		static bool collect_pass_begin_barriers(
			const std::vector<std::shared_ptr<RenderTarget::Impl>>& color_attachments,
			const std::shared_ptr<RenderTarget::Impl>& depth_attachment,
			std::vector<RHI::AshBarrier>& out_barriers)
		{
			for (const auto& color_attachment : color_attachments)
			{
				if (!color_attachment)
				{
					return false;
				}
				append_texture_barrier(out_barriers, color_attachment, RHI::AshResourceState::RTV);
			}

			if (depth_attachment)
			{
				append_texture_barrier(out_barriers, depth_attachment, RHI::AshResourceState::DSVWrite);
			}

			return true;
		}

		static bool collect_pass_end_barriers(
			const std::shared_ptr<RHI::Framebuffer>& framebuffer,
			const std::shared_ptr<RHI::RenderPass>& render_pass,
			std::vector<RHI::AshBarrier>& out_barriers)
		{
			if (!framebuffer || !render_pass)
			{
				return false;
			}

			auto& color_attachments = framebuffer->get_render_targets();
			const uint32_t color_attachment_count = render_pass->get_color_attachment_count();
			for (uint32_t i = 0; i < color_attachment_count; ++i)
			{
				if (i >= color_attachments.size() || !color_attachments[i])
				{
					return false;
				}
				out_barriers.emplace_back(color_attachments[i], render_pass->get_color_attachment_final_state(i));
			}

			if (std::shared_ptr<RHI::Texture> depth_attachment = framebuffer->get_depth_stencil())
			{
				out_barriers.emplace_back(depth_attachment, render_pass->get_depth_stencil_attachment_final_state());
			}

			return true;
		}

		static std::unique_ptr<RHI::IGraphicsRenderProgram> create_rhi_graphics_program(GraphicsProgram::Impl& impl, RHI::GraphicsContext* graphics_context, const std::shared_ptr<RHI::RenderPass>& render_pass)
		{
		RHI::GraphicProgramCreateDesc rhi_desc{};
		rhi_desc.pipeline.name = impl.name.empty() ? nullptr : impl.name.c_str();
		rhi_desc.pipeline.render_pass = render_pass;
		rhi_desc.pipeline.shaders.add_stage(impl.vertex_shader, RHI::ASH_SHADER_STAGE_VERTEX_BIT, impl.vertex_entry.c_str());
		rhi_desc.pipeline.shaders.add_stage(impl.fragment_shader, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, impl.fragment_entry.c_str());
		rhi_desc.pipeline.primitiveTopology = to_rhi_topology(impl.state.primitive_topology);
		rhi_desc.pipeline.rasterization.cull_mode = to_rhi_cull_mode(impl.state.cull_mode);
		const uint32_t attachment_count = render_pass ? render_pass->get_color_attachment_count() : 1u;
		rhi_desc.pipeline.blend_state.active_states = attachment_count;
		for (uint32_t i = 0; i < attachment_count; ++i)
		{
			rhi_desc.pipeline.blend_state.blend_states[i].set_color_write_mask(RHI::AshColorWriteMask::All);
		}

		std::unique_ptr<RHI::IGraphicsRenderProgram> program = graphics_context->create_graphics_render_program(rhi_desc);
		if (!program)
		{
			return nullptr;
		}

		apply_program_state(impl, *program);
		if (!commit_program_bindings(impl.bindings, graphics_context, *program))
		{
			return nullptr;
		}
		return program;
	}

	static std::unique_ptr<RHI::IComputeRenderProgram> create_rhi_compute_program(ComputeProgram::Impl& impl, RHI::GraphicsContext* graphics_context)
	{
		RHI::ComputeProgramCreateDesc rhi_desc{};
		rhi_desc.pipeline.name = impl.name.empty() ? nullptr : impl.name.c_str();
		rhi_desc.pipeline.shaders.add_stage(impl.compute_shader, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, impl.compute_entry.c_str());

		std::unique_ptr<RHI::IComputeRenderProgram> program = graphics_context->create_compute_render_program(rhi_desc);
		if (!program)
		{
			return nullptr;
		}
		if (!commit_program_bindings(impl.bindings, graphics_context, *program))
		{
			return nullptr;
		}
		return program;
	}

	RenderTarget::RenderTarget() = default;
	RenderTarget::RenderTarget(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	RenderTarget::~RenderTarget() = default;

	uint32_t RenderTarget::get_width() const
	{
		if (!m_impl)
		{
			return 0;
		}
		const std::shared_ptr<RHI::Texture> texture = m_impl->get_texture();
		return texture ? texture->get_width() : 0;
	}

	uint32_t RenderTarget::get_height() const
	{
		if (!m_impl)
		{
			return 0;
		}
		const std::shared_ptr<RHI::Texture> texture = m_impl->get_texture();
		return texture ? texture->get_height() : 0;
	}

	RenderTextureFormat RenderTarget::get_format() const
	{
		return m_impl ? m_impl->format : RenderTextureFormat::Unknown;
	}

	bool RenderTarget::is_depth_stencil() const
	{
		return m_impl ? m_impl->depth_stencil : false;
	}

	UniformBuffer::UniformBuffer() = default;
	UniformBuffer::UniformBuffer(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	UniformBuffer::~UniformBuffer() = default;

	uint32_t UniformBuffer::get_size() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->size : 0;
	}

	bool UniformBuffer::update(uint32_t offset, uint32_t size, const void* data)
	{
		return m_impl && m_impl->resource && m_impl->resource->buffer && data ?
			m_impl->resource->buffer->update(offset, size, const_cast<void*>(data)) : false;
	}

	VertexBuffer::VertexBuffer() = default;
	VertexBuffer::VertexBuffer(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	VertexBuffer::~VertexBuffer() = default;

	uint32_t VertexBuffer::get_size() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->size : 0;
	}

	uint32_t VertexBuffer::get_stride() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->stride : 0;
	}

	uint32_t VertexBuffer::get_vertex_count() const
	{
		return (m_impl && m_impl->resource && m_impl->resource->stride > 0) ? m_impl->resource->size / m_impl->resource->stride : 0;
	}

	bool VertexBuffer::update(uint32_t offset, uint32_t size, const void* data)
	{
		return m_impl && m_impl->resource && m_impl->resource->buffer && data ?
			m_impl->resource->buffer->update(offset, size, const_cast<void*>(data)) : false;
	}

	IndexBuffer::IndexBuffer() = default;
	IndexBuffer::IndexBuffer(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	IndexBuffer::~IndexBuffer() = default;

	uint32_t IndexBuffer::get_size() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->size : 0;
	}

	uint32_t IndexBuffer::get_index_count() const
	{
		if (!m_impl || !m_impl->resource)
		{
			return 0;
		}
		const uint32_t index_stride = m_impl->resource->index_format == RenderIndexFormat::UInt16 ? 2u : 4u;
		return index_stride > 0 ? m_impl->resource->size / index_stride : 0;
	}

	RenderIndexFormat IndexBuffer::get_format() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->index_format : RenderIndexFormat::UInt32;
	}

	bool IndexBuffer::update(uint32_t offset, uint32_t size, const void* data)
	{
		return m_impl && m_impl->resource && m_impl->resource->buffer && data ?
			m_impl->resource->buffer->update(offset, size, const_cast<void*>(data)) : false;
	}

	StorageBuffer::StorageBuffer() = default;
	StorageBuffer::StorageBuffer(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	StorageBuffer::~StorageBuffer() = default;

	uint32_t StorageBuffer::get_size() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->size : 0;
	}

	uint32_t StorageBuffer::get_stride() const
	{
		return (m_impl && m_impl->resource) ? m_impl->resource->stride : 0;
	}

	uint32_t StorageBuffer::get_element_count() const
	{
		return (m_impl && m_impl->resource && m_impl->resource->stride > 0) ? m_impl->resource->size / m_impl->resource->stride : 0;
	}

	bool StorageBuffer::update(uint32_t offset, uint32_t size, const void* data)
	{
		return m_impl && m_impl->resource && m_impl->resource->buffer && data ?
			m_impl->resource->buffer->update(offset, size, const_cast<void*>(data)) : false;
	}

	GraphicsProgram::GraphicsProgram() = default;
	GraphicsProgram::GraphicsProgram(std::unique_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	GraphicsProgram::~GraphicsProgram() = default;

	bool GraphicsProgram::apply_render_state(const std::function<void(GraphicsProgramState&)>& fn)
	{
		if (!m_impl)
		{
			return false;
		}

		GraphicsProgramState updated_state = m_impl->state;
		if (fn)
		{
			fn(updated_state);
		}
		m_impl->state = updated_state;

		for (auto& [_, program] : m_impl->programs)
		{
			if (program)
			{
				apply_program_state(*m_impl, *program);
			}
		}
		return true;
	}

	bool GraphicsProgram::set_const_data_block(uint32_t size, const void* data)
	{
		if (!m_impl)
		{
			return false;
		}
		if (!set_program_const_data(m_impl->bindings, size, data))
		{
			return false;
		}

		for (auto& [_, program] : m_impl->programs)
		{
			if (program && !program->set_const_data_block(size, data))
			{
				return false;
			}
		}
		return true;
	}

	bool GraphicsProgram::set_static_int(const char* name, int32_t value)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (write_parameter_member(m_impl->bindings, name, value))
		{
			m_impl->bindings.immutable_constants.erase(name);
			return true;
		}
		ImmutableConstantValue& constant = m_impl->bindings.immutable_constants[name];
		constant.type = ImmutableConstantValue::Type::Int;
		constant.value_i = value;
		return true;
	}

	bool GraphicsProgram::set_static_uint(const char* name, uint32_t value)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (write_parameter_member(m_impl->bindings, name, value))
		{
			m_impl->bindings.immutable_constants.erase(name);
			return true;
		}
		ImmutableConstantValue& constant = m_impl->bindings.immutable_constants[name];
		constant.type = ImmutableConstantValue::Type::UInt;
		constant.value_u = value;
		return true;
	}

	bool GraphicsProgram::set_static_float(const char* name, float value)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (write_parameter_member(m_impl->bindings, name, value))
		{
			m_impl->bindings.immutable_constants.erase(name);
			return true;
		}
		ImmutableConstantValue& constant = m_impl->bindings.immutable_constants[name];
		constant.type = ImmutableConstantValue::Type::Float;
		constant.value_f = value;
		return true;
	}

	bool GraphicsProgram::set_uniform_buffer(const char* name, const std::shared_ptr<UniformBuffer>& buffer)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (!buffer)
		{
			m_impl->bindings.uniform_buffers.erase(name);
			return true;
		}
		m_impl->bindings.uniform_buffers[name] = buffer->m_impl;
		return true;
	}

	bool GraphicsProgram::set_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_srv_arrays.erase(name);
		if (!buffer)
		{
			m_impl->bindings.storage_buffer_srvs.erase(name);
			return true;
		}
		m_impl->bindings.storage_buffer_srvs[name] = buffer->m_impl;
		return true;
	}

	bool GraphicsProgram::set_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_srvs.erase(name);
		if (buffers.empty())
		{
			m_impl->bindings.storage_buffer_srv_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<StorageBuffer::Impl>> buffer_impls;
		buffer_impls.reserve(buffers.size());
		for (const auto& buffer : buffers)
		{
			if (!buffer)
			{
				return false;
			}
			buffer_impls.push_back(buffer->m_impl);
		}
		m_impl->bindings.storage_buffer_srv_arrays[name] = std::move(buffer_impls);
		return true;
	}

	bool GraphicsProgram::set_rw_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_uav_arrays.erase(name);
		if (!buffer)
		{
			m_impl->bindings.storage_buffer_uavs.erase(name);
			return true;
		}
		m_impl->bindings.storage_buffer_uavs[name] = buffer->m_impl;
		return true;
	}

	bool GraphicsProgram::set_rw_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_uavs.erase(name);
		if (buffers.empty())
		{
			m_impl->bindings.storage_buffer_uav_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<StorageBuffer::Impl>> buffer_impls;
		buffer_impls.reserve(buffers.size());
		for (const auto& buffer : buffers)
		{
			if (!buffer)
			{
				return false;
			}
			buffer_impls.push_back(buffer->m_impl);
		}
		m_impl->bindings.storage_buffer_uav_arrays[name] = std::move(buffer_impls);
		return true;
	}

	bool GraphicsProgram::set_texture(const char* name, const std::shared_ptr<RenderTarget>& texture)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_srv_arrays.erase(name);
		if (!texture)
		{
			m_impl->bindings.texture_srvs.erase(name);
			return true;
		}
		m_impl->bindings.texture_srvs[name] = texture->m_impl;
		return true;
	}

	bool GraphicsProgram::set_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_srvs.erase(name);
		if (textures.empty())
		{
			m_impl->bindings.texture_srv_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<RenderTarget::Impl>> texture_impls;
		texture_impls.reserve(textures.size());
		for (const auto& texture : textures)
		{
			if (!texture)
			{
				return false;
			}
			texture_impls.push_back(texture->m_impl);
		}
		m_impl->bindings.texture_srv_arrays[name] = std::move(texture_impls);
		return true;
	}

	bool GraphicsProgram::set_rw_texture(const char* name, const std::shared_ptr<RenderTarget>& texture)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_uav_arrays.erase(name);
		if (!texture)
		{
			m_impl->bindings.texture_uavs.erase(name);
			return true;
		}
		m_impl->bindings.texture_uavs[name] = texture->m_impl;
		return true;
	}

	bool GraphicsProgram::set_rw_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_uavs.erase(name);
		if (textures.empty())
		{
			m_impl->bindings.texture_uav_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<RenderTarget::Impl>> texture_impls;
		texture_impls.reserve(textures.size());
		for (const auto& texture : textures)
		{
			if (!texture)
			{
				return false;
			}
			texture_impls.push_back(texture->m_impl);
		}
		m_impl->bindings.texture_uav_arrays[name] = std::move(texture_impls);
		return true;
	}

	bool GraphicsProgram::set_sampler(const char* name, RenderSamplerState sampler_state)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.sampler_arrays.erase(name);
		m_impl->bindings.samplers[name] = sampler_state;
		return true;
	}

	bool GraphicsProgram::set_sampler_array(const char* name, const std::vector<RenderSamplerState>& sampler_states)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.samplers.erase(name);
		if (sampler_states.empty())
		{
			m_impl->bindings.sampler_arrays.erase(name);
			return true;
		}
		m_impl->bindings.sampler_arrays[name] = sampler_states;
		return true;
	}

	ComputeProgram::ComputeProgram() = default;
	ComputeProgram::ComputeProgram(std::unique_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}
	ComputeProgram::~ComputeProgram() = default;

	bool ComputeProgram::set_const_data_block(uint32_t size, const void* data)
	{
		if (!m_impl)
		{
			return false;
		}
		if (!set_program_const_data(m_impl->bindings, size, data))
		{
			return false;
		}
		return !m_impl->program || m_impl->program->set_const_data_block(size, data);
	}

	bool ComputeProgram::set_static_int(const char* name, int32_t value)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (write_parameter_member(m_impl->bindings, name, value))
		{
			m_impl->bindings.immutable_constants.erase(name);
			return true;
		}
		ImmutableConstantValue& constant = m_impl->bindings.immutable_constants[name];
		constant.type = ImmutableConstantValue::Type::Int;
		constant.value_i = value;
		return true;
	}

	bool ComputeProgram::set_static_uint(const char* name, uint32_t value)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (write_parameter_member(m_impl->bindings, name, value))
		{
			m_impl->bindings.immutable_constants.erase(name);
			return true;
		}
		ImmutableConstantValue& constant = m_impl->bindings.immutable_constants[name];
		constant.type = ImmutableConstantValue::Type::UInt;
		constant.value_u = value;
		return true;
	}

	bool ComputeProgram::set_static_float(const char* name, float value)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (write_parameter_member(m_impl->bindings, name, value))
		{
			m_impl->bindings.immutable_constants.erase(name);
			return true;
		}
		ImmutableConstantValue& constant = m_impl->bindings.immutable_constants[name];
		constant.type = ImmutableConstantValue::Type::Float;
		constant.value_f = value;
		return true;
	}

	bool ComputeProgram::set_uniform_buffer(const char* name, const std::shared_ptr<UniformBuffer>& buffer)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		if (!buffer)
		{
			m_impl->bindings.uniform_buffers.erase(name);
			return true;
		}
		m_impl->bindings.uniform_buffers[name] = buffer->m_impl;
		return true;
	}

	bool ComputeProgram::set_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_srv_arrays.erase(name);
		if (!buffer)
		{
			m_impl->bindings.storage_buffer_srvs.erase(name);
			return true;
		}
		m_impl->bindings.storage_buffer_srvs[name] = buffer->m_impl;
		return true;
	}

	bool ComputeProgram::set_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_srvs.erase(name);
		if (buffers.empty())
		{
			m_impl->bindings.storage_buffer_srv_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<StorageBuffer::Impl>> buffer_impls;
		buffer_impls.reserve(buffers.size());
		for (const auto& buffer : buffers)
		{
			if (!buffer)
			{
				return false;
			}
			buffer_impls.push_back(buffer->m_impl);
		}
		m_impl->bindings.storage_buffer_srv_arrays[name] = std::move(buffer_impls);
		return true;
	}

	bool ComputeProgram::set_rw_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_uav_arrays.erase(name);
		if (!buffer)
		{
			m_impl->bindings.storage_buffer_uavs.erase(name);
			return true;
		}
		m_impl->bindings.storage_buffer_uavs[name] = buffer->m_impl;
		return true;
	}

	bool ComputeProgram::set_rw_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.storage_buffer_uavs.erase(name);
		if (buffers.empty())
		{
			m_impl->bindings.storage_buffer_uav_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<StorageBuffer::Impl>> buffer_impls;
		buffer_impls.reserve(buffers.size());
		for (const auto& buffer : buffers)
		{
			if (!buffer)
			{
				return false;
			}
			buffer_impls.push_back(buffer->m_impl);
		}
		m_impl->bindings.storage_buffer_uav_arrays[name] = std::move(buffer_impls);
		return true;
	}

	bool ComputeProgram::set_texture(const char* name, const std::shared_ptr<RenderTarget>& texture)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_srv_arrays.erase(name);
		if (!texture)
		{
			m_impl->bindings.texture_srvs.erase(name);
			return true;
		}
		m_impl->bindings.texture_srvs[name] = texture->m_impl;
		return true;
	}

	bool ComputeProgram::set_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_srvs.erase(name);
		if (textures.empty())
		{
			m_impl->bindings.texture_srv_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<RenderTarget::Impl>> texture_impls;
		texture_impls.reserve(textures.size());
		for (const auto& texture : textures)
		{
			if (!texture)
			{
				return false;
			}
			texture_impls.push_back(texture->m_impl);
		}
		m_impl->bindings.texture_srv_arrays[name] = std::move(texture_impls);
		return true;
	}

	bool ComputeProgram::set_rw_texture(const char* name, const std::shared_ptr<RenderTarget>& texture)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_uav_arrays.erase(name);
		if (!texture)
		{
			m_impl->bindings.texture_uavs.erase(name);
			return true;
		}
		m_impl->bindings.texture_uavs[name] = texture->m_impl;
		return true;
	}

	bool ComputeProgram::set_rw_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.texture_uavs.erase(name);
		if (textures.empty())
		{
			m_impl->bindings.texture_uav_arrays.erase(name);
			return true;
		}
		std::vector<std::shared_ptr<RenderTarget::Impl>> texture_impls;
		texture_impls.reserve(textures.size());
		for (const auto& texture : textures)
		{
			if (!texture)
			{
				return false;
			}
			texture_impls.push_back(texture->m_impl);
		}
		m_impl->bindings.texture_uav_arrays[name] = std::move(texture_impls);
		return true;
	}

	bool ComputeProgram::set_sampler(const char* name, RenderSamplerState sampler_state)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.sampler_arrays.erase(name);
		m_impl->bindings.samplers[name] = sampler_state;
		return true;
	}

	bool ComputeProgram::set_sampler_array(const char* name, const std::vector<RenderSamplerState>& sampler_states)
	{
		if (!m_impl || !name)
		{
			return false;
		}
		m_impl->bindings.samplers.erase(name);
		if (sampler_states.empty())
		{
			m_impl->bindings.sampler_arrays.erase(name);
			return true;
		}
		m_impl->bindings.sampler_arrays[name] = sampler_states;
		return true;
	}

	RenderDevice::RenderDevice(RHI::GraphicsContext* graphics_context, RHI::Swapchain* swapchain)
		: m_impl(std::make_unique<Impl>())
	{
		m_impl->graphics_context = graphics_context;
		m_impl->swapchain = swapchain;
		m_impl->back_buffer_target = std::make_shared<RenderTarget::Impl>();
		m_impl->back_buffer_target->kind = RenderTarget::Impl::Kind::Texture;
		m_impl->back_buffer_target->shader_resource = true;
		m_impl->back_buffer_target->unordered_access = false;
		m_impl->back_buffer_target->depth_stencil = false;
		m_impl->back_buffer_target->format = k_public_back_buffer_format;

		m_impl->swapchain_target = std::make_shared<RenderTarget::Impl>();
		sync_swapchain_target();
		ensure_back_buffer_target();
	}

	RenderDevice::~RenderDevice()
	{
		if (!m_impl)
		{
			return;
		}

		m_impl->current_command_buffer = nullptr;
		m_impl->current_framebuffer.reset();
		m_impl->current_render_pass.reset();
		m_impl->back_buffer_target.reset();
		m_impl->swapchain_target.reset();
		m_impl->transient_render_target_pool.clear();
		m_impl->graphics_context = nullptr;
		m_impl->swapchain = nullptr;
	}

	bool RenderDevice::begin_frame()
	{
		if (!m_impl->graphics_context || !m_impl->swapchain)
		{
			return false;
		}

		m_impl->graphics_context->begin_frame();
		m_impl->swapchain->begin_frame();
		sync_swapchain_target();
		if (!ensure_back_buffer_target())
		{
			return false;
		}
		m_impl->current_command_buffer = m_impl->graphics_context->get_command_buffer(0);
		if (!m_impl->current_command_buffer)
		{
			return false;
		}

		m_impl->current_command_buffer->begin_record();
		m_impl->current_framebuffer.reset();
		m_impl->current_render_pass.reset();
		m_impl->viewport_override_active = false;
		m_impl->scissor_override_active = false;
		m_impl->back_buffer_written_this_frame = false;
		return true;
	}

	bool RenderDevice::end_frame()
	{
		if (!m_impl->current_command_buffer)
		{
			return false;
		}

		end_pass();
		const bool present_pass_result = render_present_to_swapchain();
		m_impl->current_command_buffer->end_record();
		m_impl->graphics_context->submit({ m_impl->current_command_buffer, 1 });
		m_impl->swapchain->end_frame();
		m_impl->graphics_context->end_frame();
		m_impl->current_command_buffer = nullptr;
		return present_pass_result;
	}

	void RenderDevice::present()
	{
		if (m_impl->swapchain)
		{
			m_impl->swapchain->present();
		}
	}

	std::shared_ptr<RenderTarget> RenderDevice::get_back_buffer()
	{
		if (!ensure_back_buffer_target())
		{
			return nullptr;
		}
		return std::shared_ptr<RenderTarget>(new RenderTarget(m_impl->back_buffer_target));
	}

	std::shared_ptr<RenderTarget> RenderDevice::create_render_target(const RenderTargetDesc& desc)
	{
		const std::shared_ptr<RenderTarget::Impl> impl = create_render_target_impl(m_impl->graphics_context, desc);
		return impl ? std::shared_ptr<RenderTarget>(new RenderTarget(impl)) : nullptr;
	}

	std::shared_ptr<RenderTarget> RenderDevice::acquire_transient_render_target(const RenderTargetDesc& desc)
	{
		const uint64_t hash_value = hash_render_target_desc(desc);
		auto& bucket = m_impl->transient_render_target_pool[hash_value];
		if (!bucket.empty())
		{
			std::shared_ptr<RenderTarget> render_target = bucket.back();
			bucket.pop_back();
			return render_target;
		}
		return create_render_target(desc);
	}

	void RenderDevice::release_transient_render_target(const std::shared_ptr<RenderTarget>& render_target)
	{
		if (!render_target)
		{
			return;
		}

		RenderTargetDesc desc{};
		desc.width = static_cast<uint16_t>(render_target->get_width());
		desc.height = static_cast<uint16_t>(render_target->get_height());
		desc.format = render_target->get_format();
		desc.shader_resource = render_target->m_impl ? render_target->m_impl->shader_resource : false;
		desc.unordered_access = render_target->m_impl ? render_target->m_impl->unordered_access : false;
		m_impl->transient_render_target_pool[hash_render_target_desc(desc)].push_back(render_target);
	}

	void RenderDevice::clear_transient_render_targets()
	{
		m_impl->transient_render_target_pool.clear();
	}

	std::shared_ptr<UniformBuffer> RenderDevice::create_uniform_buffer(const UniformBufferDesc& desc)
	{
		if (!m_impl->graphics_context || desc.size == 0)
		{
			return nullptr;
		}

		RHI::BufferCreation buffer_creation{};
		buffer_creation.size = desc.size;
		buffer_creation.struct_byte_stride = 0;
		buffer_creation.usage_flags = RHI::ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		buffer_creation.access_type = desc.cpu_write ? RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE : RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		buffer_creation.force_static = true;
		buffer_creation.initial_data = const_cast<void*>(desc.initial_data);
		buffer_creation.name = desc.name;

		std::shared_ptr<RHI::Buffer> buffer = m_impl->graphics_context->create_buffer(buffer_creation);
		if (!buffer)
		{
			return nullptr;
		}

		auto resource = std::make_shared<BufferResource>();
		resource->buffer = buffer;
		resource->size = desc.size;

		auto impl = std::make_shared<UniformBuffer::Impl>();
		impl->resource = resource;
		return std::shared_ptr<UniformBuffer>(new UniformBuffer(impl));
	}

	std::shared_ptr<VertexBuffer> RenderDevice::create_vertex_buffer(const VertexBufferDesc& desc)
	{
		if (!m_impl->graphics_context || desc.size == 0 || desc.stride == 0)
		{
			return nullptr;
		}

		RHI::BufferCreation buffer_creation{};
		buffer_creation.size = desc.size;
		buffer_creation.struct_byte_stride = desc.stride;
		buffer_creation.usage_flags = RHI::ASH_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		buffer_creation.access_type = desc.cpu_write ? RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE : RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		buffer_creation.force_static = true;
		buffer_creation.initial_data = const_cast<void*>(desc.initial_data);
		buffer_creation.name = desc.name;

		std::shared_ptr<RHI::Buffer> buffer = m_impl->graphics_context->create_buffer(buffer_creation);
		if (!buffer)
		{
			return nullptr;
		}

		auto resource = std::make_shared<BufferResource>();
		resource->buffer = buffer;
		resource->size = desc.size;
		resource->stride = desc.stride;

		auto impl = std::make_shared<VertexBuffer::Impl>();
		impl->resource = resource;
		return std::shared_ptr<VertexBuffer>(new VertexBuffer(impl));
	}

	std::shared_ptr<IndexBuffer> RenderDevice::create_index_buffer(const IndexBufferDesc& desc)
	{
		if (!m_impl->graphics_context || desc.size == 0)
		{
			return nullptr;
		}

		RHI::BufferCreation buffer_creation{};
		buffer_creation.size = desc.size;
		buffer_creation.struct_byte_stride = desc.format == RenderIndexFormat::UInt16 ? 2 : 4;
		buffer_creation.usage_flags = RHI::ASH_BUFFER_USAGE_INDEX_BUFFER_BIT;
		buffer_creation.access_type = desc.cpu_write ? RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE : RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		buffer_creation.force_static = true;
		buffer_creation.initial_data = const_cast<void*>(desc.initial_data);
		buffer_creation.name = desc.name;

		std::shared_ptr<RHI::Buffer> buffer = m_impl->graphics_context->create_buffer(buffer_creation);
		if (!buffer)
		{
			return nullptr;
		}

		auto resource = std::make_shared<BufferResource>();
		resource->buffer = buffer;
		resource->size = desc.size;
		resource->stride = buffer_creation.struct_byte_stride;
		resource->index_format = desc.format;

		auto impl = std::make_shared<IndexBuffer::Impl>();
		impl->resource = resource;
		return std::shared_ptr<IndexBuffer>(new IndexBuffer(impl));
	}

	std::shared_ptr<StorageBuffer> RenderDevice::create_storage_buffer(const StorageBufferDesc& desc)
	{
		if (!m_impl->graphics_context || desc.size == 0)
		{
			return nullptr;
		}

		RHI::BufferCreation buffer_creation{};
		buffer_creation.size = desc.size;
		buffer_creation.struct_byte_stride = desc.stride;
		buffer_creation.usage_flags = RHI::ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		buffer_creation.access_type = desc.cpu_write ? RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE : RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		buffer_creation.force_static = true;
		buffer_creation.initial_data = const_cast<void*>(desc.initial_data);
		buffer_creation.name = desc.name;

		std::shared_ptr<RHI::Buffer> buffer = m_impl->graphics_context->create_buffer(buffer_creation);
		if (!buffer)
		{
			return nullptr;
		}

		auto resource = std::make_shared<BufferResource>();
		resource->buffer = buffer;
		resource->size = desc.size;
		resource->stride = desc.stride;

		auto impl = std::make_shared<StorageBuffer::Impl>();
		impl->resource = resource;
		return std::shared_ptr<StorageBuffer>(new StorageBuffer(impl));
	}

	std::unique_ptr<GraphicsProgram> RenderDevice::create_graphics_program(const GraphicsProgramDesc& desc)
	{
		if (!m_impl->graphics_context || !desc.shader_path)
		{
			return nullptr;
		}

		RHI::ShaderCreation vertex_shader_creation{};
		vertex_shader_creation.pBaseShaderPath = desc.shader_path;
		vertex_shader_creation.pShaderMacro = desc.shader_macro;
		vertex_shader_creation.pEntryPoint = desc.vertex_entry;
		vertex_shader_creation.type = RHI::ASH_SHADER_STAGE_VERTEX_BIT;

		RHI::ShaderCreation fragment_shader_creation{};
		fragment_shader_creation.pBaseShaderPath = desc.shader_path;
		fragment_shader_creation.pShaderMacro = desc.shader_macro;
		fragment_shader_creation.pEntryPoint = desc.fragment_entry;
		fragment_shader_creation.type = RHI::ASH_SHADER_STAGE_FRAGMENT_BIT;

		std::shared_ptr<RHI::Shader> vertex_shader = m_impl->graphics_context->create_shader(vertex_shader_creation);
		std::shared_ptr<RHI::Shader> fragment_shader = m_impl->graphics_context->create_shader(fragment_shader_creation);
		if (!vertex_shader || !fragment_shader)
		{
			return nullptr;
		}

		auto impl = std::make_unique<GraphicsProgram::Impl>();
		impl->state = desc.state;
		impl->shader_path = desc.shader_path;
		impl->vertex_entry = desc.vertex_entry ? desc.vertex_entry : "VSMain";
		impl->fragment_entry = desc.fragment_entry ? desc.fragment_entry : "PSMain";
		impl->shader_macro = desc.shader_macro ? desc.shader_macro : "";
		impl->name = desc.name ? desc.name : "EngineGraphicsProgram";
		impl->vertex_shader = vertex_shader;
		impl->fragment_shader = fragment_shader;
		if (!resolve_graphics_parameter_block_layout(impl->vertex_shader, impl->fragment_shader, impl->bindings.const_parameter_block))
		{
			HLogError("Graphics program '{}' found incompatible root parameter block layouts across shader stages.", impl->name);
			return nullptr;
		}
		if (impl->bindings.const_parameter_block && impl->bindings.const_parameter_block->byte_size > 0)
		{
			impl->bindings.const_data_block.resize(impl->bindings.const_parameter_block->byte_size, 0u);
		}

		return std::unique_ptr<GraphicsProgram>(new GraphicsProgram(std::move(impl)));
	}

	std::unique_ptr<ComputeProgram> RenderDevice::create_compute_program(const ComputeProgramDesc& desc)
	{
		if (!m_impl->graphics_context || !desc.shader_path)
		{
			return nullptr;
		}

		RHI::ShaderCreation compute_shader_creation{};
		compute_shader_creation.pBaseShaderPath = desc.shader_path;
		compute_shader_creation.pShaderMacro = desc.shader_macro;
		compute_shader_creation.pEntryPoint = desc.compute_entry ? desc.compute_entry : "CSMain";
		compute_shader_creation.type = RHI::ASH_SHADER_STAGE_COMPUTE_BIT;

		std::shared_ptr<RHI::Shader> compute_shader = m_impl->graphics_context->create_shader(compute_shader_creation);
		if (!compute_shader)
		{
			return nullptr;
		}

		auto impl = std::make_unique<ComputeProgram::Impl>();
		impl->shader_path = desc.shader_path;
		impl->compute_entry = desc.compute_entry ? desc.compute_entry : "CSMain";
		impl->shader_macro = desc.shader_macro ? desc.shader_macro : "";
		impl->name = desc.name ? desc.name : "EngineComputeProgram";
		impl->compute_shader = compute_shader;
		impl->bindings.const_parameter_block = find_root_parameter_block(impl->compute_shader);
		if (impl->bindings.const_parameter_block && impl->bindings.const_parameter_block->byte_size > 0)
		{
			impl->bindings.const_data_block.resize(impl->bindings.const_parameter_block->byte_size, 0u);
		}
		return std::unique_ptr<ComputeProgram>(new ComputeProgram(std::move(impl)));
	}

	bool RenderDevice::ensure_back_buffer_target()
	{
		if (!m_impl || !m_impl->graphics_context || !m_impl->swapchain || !m_impl->back_buffer_target)
		{
			return false;
		}

		const uint32_t swapchain_width = m_impl->swapchain->get_width();
		const uint32_t swapchain_height = m_impl->swapchain->get_height();
		if (swapchain_width == 0 || swapchain_height == 0)
		{
			return false;
		}

		std::shared_ptr<RHI::Texture> texture = m_impl->back_buffer_target->texture;
		if (texture &&
			texture->get_width() == swapchain_width &&
			texture->get_height() == swapchain_height &&
			m_impl->back_buffer_target->format == k_public_back_buffer_format &&
			m_impl->back_buffer_target->shader_resource &&
			!m_impl->back_buffer_target->unordered_access &&
			!m_impl->back_buffer_target->depth_stencil)
		{
			return true;
		}

		RenderTargetDesc desc{};
		desc.width = static_cast<uint16_t>(swapchain_width);
		desc.height = static_cast<uint16_t>(swapchain_height);
		desc.format = k_public_back_buffer_format;
		desc.shader_resource = true;
		desc.unordered_access = false;
		desc.name = k_public_back_buffer_name;

		const std::shared_ptr<RenderTarget::Impl> new_target = create_render_target_impl(m_impl->graphics_context, desc);
		if (!new_target || !new_target->texture)
		{
			HLogError("RenderDevice: failed to create engine offscreen back buffer {}x{}.", swapchain_width, swapchain_height);
			return false;
		}

		m_impl->back_buffer_target->kind = RenderTarget::Impl::Kind::Texture;
		m_impl->back_buffer_target->swapchain = nullptr;
		m_impl->back_buffer_target->texture = new_target->texture;
		m_impl->back_buffer_target->shader_resource = new_target->shader_resource;
		m_impl->back_buffer_target->unordered_access = new_target->unordered_access;
		m_impl->back_buffer_target->depth_stencil = new_target->depth_stencil;
		m_impl->back_buffer_target->format = new_target->format;
		return true;
	}

	void RenderDevice::sync_swapchain_target()
	{
		if (!m_impl || !m_impl->swapchain || !m_impl->swapchain_target)
		{
			return;
		}

		m_impl->swapchain_target->kind = RenderTarget::Impl::Kind::BackBuffer;
		m_impl->swapchain_target->swapchain = m_impl->swapchain;
		m_impl->swapchain_target->texture.reset();
		m_impl->swapchain_target->shader_resource = false;
		m_impl->swapchain_target->unordered_access = false;
		m_impl->swapchain_target->depth_stencil = false;
		m_impl->swapchain_target->format = from_rhi_format(m_impl->swapchain->get_format());
	}

	bool RenderDevice::render_present_to_swapchain()
	{
		if (!m_impl || !m_impl->current_command_buffer || !m_impl->swapchain_target || !ensure_back_buffer_target())
		{
			return false;
		}

		if (!m_impl->back_buffer_written_this_frame)
		{
			const std::shared_ptr<RenderTarget> back_buffer_target(new RenderTarget(m_impl->back_buffer_target));
			PassDesc clear_pass_desc{};
			clear_pass_desc.name = "EngineBackBufferClearPass";
			clear_pass_desc.color_attachments.push_back({
				back_buffer_target,
				RenderLoadAction::Clear,
				{ 0.0f, 0.0f, 0.0f, 1.0f }
			});

			if (!begin_pass(clear_pass_desc))
			{
				HLogError("RenderDevice: failed to begin internal offscreen clear pass.");
				return false;
			}
			end_pass();
		}

		const std::shared_ptr<RHI::Texture> source_texture = m_impl->back_buffer_target->get_texture();
		const std::shared_ptr<RHI::Texture> destination_texture = m_impl->swapchain_target->get_texture();
		if (!source_texture || !destination_texture)
		{
			HLogError("RenderDevice: present copy requires both offscreen and swapchain textures.");
			return false;
		}

		if (!m_impl->current_command_buffer->cmd_copy_texture(source_texture, destination_texture))
		{
			HLogError("RenderDevice: failed to copy internal back buffer to swapchain.");
			return false;
		}

		if (!m_impl->current_command_buffer->cmd_transition_resource_state({ destination_texture, RHI::AshResourceState::CopyDst, RHI::AshResourceState::Present }))
		{
			HLogError("RenderDevice: failed to transition swapchain image to present.");
			return false;
		}
		return true;
	}

	bool RenderDevice::begin_pass(const PassDesc& desc)
	{
		if (!m_impl->current_command_buffer)
		{
			return false;
		}
		if (desc.color_attachments.empty() && !desc.depth_attachment.render_target)
		{
			return false;
		}
		RenderPassSignature signature{};
		RHI::RenderPassCreation render_pass_creation{};
		render_pass_creation.set_name(desc.name ? desc.name : "EngineRenderPass");

		RHI::FramebufferCreation framebuffer_creation{};
		framebuffer_creation.name = desc.name ? desc.name : "EngineFramebuffer";
		framebuffer_creation.layers = 1;
		framebuffer_creation.colorAttachments.init(nullptr, static_cast<uint32_t>(desc.color_attachments.size()), 0);

		uint32_t pass_width = 0;
		uint32_t pass_height = 0;
		auto assign_pass_extent = [&pass_width, &pass_height](const std::shared_ptr<RHI::Texture>& texture) -> bool
		{
			if (!texture)
			{
				return false;
			}
			if (pass_width == 0 && pass_height == 0)
			{
				pass_width = texture->get_width();
				pass_height = texture->get_height();
				return true;
			}
			return pass_width == texture->get_width() && pass_height == texture->get_height();
		};

		for (const PassColorAttachment& attachment : desc.color_attachments)
		{
			if (!attachment.render_target || !attachment.render_target->m_impl || attachment.render_target->m_impl->depth_stencil)
			{
				framebuffer_creation.colorAttachments.shutdown();
				return false;
			}
			if (attachment.render_target->m_impl == m_impl->back_buffer_target)
			{
				m_impl->back_buffer_written_this_frame = true;
			}

			std::shared_ptr<RHI::Texture> texture = attachment.render_target->m_impl->get_texture();
			if (!assign_pass_extent(texture))
			{
				framebuffer_creation.colorAttachments.shutdown();
				return false;
			}

			render_pass_creation.add_attachment(texture->get_format(), attachment.render_target->m_impl->get_final_resource_state(), to_rhi_load_action(attachment.load_action));
			framebuffer_creation.colorAttachments.push_back(texture);
			signature.color_formats[signature.color_count++] = texture->get_format();
		}

		if (desc.depth_attachment.render_target)
		{
			if (!desc.depth_attachment.render_target->m_impl || !desc.depth_attachment.render_target->m_impl->depth_stencil)
			{
				framebuffer_creation.colorAttachments.shutdown();
				return false;
			}
			std::shared_ptr<RHI::Texture> depth_texture = desc.depth_attachment.render_target->m_impl->get_texture();
			if (!assign_pass_extent(depth_texture))
			{
				framebuffer_creation.colorAttachments.shutdown();
				return false;
			}
			render_pass_creation.set_depth_stencil_texture(depth_texture->get_format(), desc.depth_attachment.render_target->m_impl->get_final_resource_state());
			render_pass_creation.set_depth_stencil_operations(
				to_rhi_load_action(desc.depth_attachment.load_action),
				to_rhi_load_action(desc.depth_attachment.load_action));
			framebuffer_creation.depthStencilAttachment = depth_texture;
			signature.depth_format = depth_texture->get_format();
		}

		framebuffer_creation.width = static_cast<uint16_t>(pass_width);
		framebuffer_creation.height = static_cast<uint16_t>(pass_height);

		m_impl->current_render_pass = m_impl->graphics_context->create_render_pass(render_pass_creation);
		if (!m_impl->current_render_pass)
		{
			framebuffer_creation.colorAttachments.shutdown();
			HLogError("RenderDevice: create_render_pass failed for '{}'.", desc.name ? desc.name : "EngineRenderPass");
			return false;
		}

		framebuffer_creation.renderPass = m_impl->current_render_pass;
		m_impl->current_framebuffer = m_impl->graphics_context->create_framebuffer(framebuffer_creation);
		framebuffer_creation.colorAttachments.shutdown();
		if (!m_impl->current_framebuffer)
		{
			HLogError("RenderDevice: create_framebuffer failed for '{}'.", desc.name ? desc.name : "EngineRenderPass");
			return false;
		}

		for (uint32_t i = 0; i < desc.color_attachments.size(); ++i)
		{
			const PassColorAttachment& attachment = desc.color_attachments[i];
			m_impl->current_framebuffer->clear_render_target(i, { attachment.clear_color.r, attachment.clear_color.g, attachment.clear_color.b, attachment.clear_color.a });
		}

		if (desc.depth_attachment.render_target)
		{
			m_impl->current_framebuffer->clear_depth_stencil({ desc.depth_attachment.clear_value.depth, desc.depth_attachment.clear_value.stencil });
		}

		std::vector<std::shared_ptr<RenderTarget::Impl>> begin_pass_color_attachments;
		begin_pass_color_attachments.reserve(desc.color_attachments.size());
		for (const PassColorAttachment& attachment : desc.color_attachments)
		{
			begin_pass_color_attachments.push_back(attachment.render_target ? attachment.render_target->m_impl : nullptr);
		}
		const std::shared_ptr<RenderTarget::Impl> begin_pass_depth_attachment =
			desc.depth_attachment.render_target ? desc.depth_attachment.render_target->m_impl : nullptr;

		std::vector<RHI::AshBarrier> begin_pass_barriers;
		if (!collect_pass_begin_barriers(begin_pass_color_attachments, begin_pass_depth_attachment, begin_pass_barriers) ||
			!submit_resource_barriers(m_impl->current_command_buffer, begin_pass_barriers))
		{
			HLogError("RenderDevice: begin_pass barriers failed for '{}'.", desc.name ? desc.name : "EngineRenderPass");
			m_impl->current_framebuffer.reset();
			m_impl->current_render_pass.reset();
			return false;
		}

		m_impl->current_pass_signature = signature;
		m_impl->viewport_override_active = false;
		m_impl->scissor_override_active = false;
		m_impl->current_command_buffer->cmd_begin_render_pass(m_impl->current_framebuffer);
		return true;
	}

	bool RenderDevice::bind_graphics_program(GraphicsProgram* program)
	{
		if (!program || !program->m_impl || !m_impl->current_command_buffer || !m_impl->current_framebuffer || !m_impl->current_render_pass)
		{
			return false;
		}

		const uint64_t signature_hash = hash_render_pass_signature(m_impl->current_pass_signature);
		auto program_it = program->m_impl->programs.find(signature_hash);
		if (program_it == program->m_impl->programs.end())
		{
			std::unique_ptr<RHI::IGraphicsRenderProgram> rhi_program = create_rhi_graphics_program(*program->m_impl, m_impl->graphics_context, m_impl->current_render_pass);
			if (!rhi_program)
			{
				HLogError("RenderDevice: create graphics pipeline failed '{}'.", program->m_impl->name);
				return false;
			}
			program_it = program->m_impl->programs.emplace(signature_hash, std::move(rhi_program)).first;
		}

		RHI::IGraphicsRenderProgram* rhi_program = program_it->second.get();
		if (!rhi_program)
		{
			return false;
		}

		apply_program_state(*program->m_impl, *rhi_program);
		if (!commit_program_bindings(program->m_impl->bindings, m_impl->graphics_context, *rhi_program))
		{
			HLogError("RenderDevice: commit graphics bindings failed '{}'.", program->m_impl->name);
			return false;
		}
		if (!rhi_program->apply(make_command_buffer_ref(m_impl->current_command_buffer)))
		{
			HLogError("RenderDevice: apply graphics render program failed '{}'.", program->m_impl->name);
			return false;
		}

		RenderViewport viewport{};
		if (m_impl->viewport_override_active)
		{
			viewport = m_impl->viewport_override;
		}
		else
		{
			viewport.x = 0;
			viewport.y = 0;
			viewport.width = static_cast<uint16_t>(m_impl->current_framebuffer->get_width());
			viewport.height = static_cast<uint16_t>(m_impl->current_framebuffer->get_height());
			viewport.min_depth = 0.0f;
			viewport.max_depth = 1.0f;
		}
		m_impl->current_command_buffer->cmd_set_viewport(to_rhi_viewport(viewport));

		RenderScissor scissor{};
		if (m_impl->scissor_override_active)
		{
			scissor = m_impl->scissor_override;
		}
		else
		{
			scissor.x = 0;
			scissor.y = 0;
			scissor.width = static_cast<uint16_t>(m_impl->current_framebuffer->get_width());
			scissor.height = static_cast<uint16_t>(m_impl->current_framebuffer->get_height());
		}
		m_impl->current_command_buffer->cmd_set_scissor(to_rhi_scissor(scissor));
		return true;
	}

	bool RenderDevice::bind_compute_program(ComputeProgram* program)
	{
		if (!program || !program->m_impl || !m_impl->current_command_buffer || m_impl->current_framebuffer)
		{
			return false;
		}

		if (!program->m_impl->program)
		{
			program->m_impl->program = create_rhi_compute_program(*program->m_impl, m_impl->graphics_context);
			if (!program->m_impl->program)
			{
				return false;
			}
		}

		if (!commit_program_bindings(program->m_impl->bindings, m_impl->graphics_context, *program->m_impl->program))
		{
			return false;
		}
		return program->m_impl->program->apply(make_command_buffer_ref(m_impl->current_command_buffer));
	}

	bool RenderDevice::transition_graphics_program_resources(GraphicsProgram* program)
	{
		if (!program || !program->m_impl || !m_impl->current_command_buffer || m_impl->current_framebuffer)
		{
			return false;
		}

		std::vector<RHI::AshBarrier> barriers;
		if (!collect_program_resource_barriers(program->m_impl->bindings, true, barriers))
		{
			return false;
		}

		return submit_resource_barriers(m_impl->current_command_buffer, barriers);
	}

	bool RenderDevice::transition_compute_program_resources(ComputeProgram* program)
	{
		if (!program || !program->m_impl || !m_impl->current_command_buffer || m_impl->current_framebuffer)
		{
			return false;
		}

		std::vector<RHI::AshBarrier> barriers;
		if (!collect_program_resource_barriers(program->m_impl->bindings, false, barriers))
		{
			return false;
		}

		return submit_resource_barriers(m_impl->current_command_buffer, barriers);
	}

	bool RenderDevice::transition_vertex_buffer(const std::shared_ptr<VertexBuffer>& buffer)
	{
		if (!m_impl->current_command_buffer || !buffer || !buffer->m_impl || !buffer->m_impl->resource || !buffer->m_impl->resource->buffer || m_impl->current_framebuffer)
		{
			return false;
		}

		return m_impl->current_command_buffer->cmd_transition_resource_state({ buffer->m_impl->resource->buffer, RHI::AshResourceState::VertexBuffer });
	}

	bool RenderDevice::transition_index_buffer(const std::shared_ptr<IndexBuffer>& buffer)
	{
		if (!m_impl->current_command_buffer || !buffer || !buffer->m_impl || !buffer->m_impl->resource || !buffer->m_impl->resource->buffer || m_impl->current_framebuffer)
		{
			return false;
		}

		return m_impl->current_command_buffer->cmd_transition_resource_state({ buffer->m_impl->resource->buffer, RHI::AshResourceState::IndexBuffer });
	}

	bool RenderDevice::bind_vertex_buffer(uint32_t slot, const std::shared_ptr<VertexBuffer>& buffer, uint64_t offset)
	{
		if (!m_impl->current_command_buffer || !buffer || !buffer->m_impl || !buffer->m_impl->resource || !buffer->m_impl->resource->buffer)
		{
			return false;
		}

		std::shared_ptr<RHI::Buffer> rhi_buffer = buffer->m_impl->resource->buffer;
		const uint64_t offsets[] = { offset };
		m_impl->current_command_buffer->cmd_bind_vertex_buffers(slot, 1, &rhi_buffer, offsets);
		return true;
	}

	bool RenderDevice::bind_index_buffer(const std::shared_ptr<IndexBuffer>& buffer, uint64_t offset)
	{
		if (!m_impl->current_command_buffer || !buffer || !buffer->m_impl || !buffer->m_impl->resource || !buffer->m_impl->resource->buffer)
		{
			return false;
		}

		m_impl->current_command_buffer->cmd_bind_index_buffer(buffer->m_impl->resource->buffer, offset, to_rhi_index_type(buffer->m_impl->resource->index_format));
		return true;
	}

	void RenderDevice::set_viewport(const RenderViewport& viewport)
	{
		m_impl->viewport_override = viewport;
		m_impl->viewport_override_active = true;
		if (m_impl->current_command_buffer)
		{
			m_impl->current_command_buffer->cmd_set_viewport(to_rhi_viewport(viewport));
		}
	}

	void RenderDevice::set_scissor(const RenderScissor& scissor)
	{
		m_impl->scissor_override = scissor;
		m_impl->scissor_override_active = true;
		if (m_impl->current_command_buffer)
		{
			m_impl->current_command_buffer->cmd_set_scissor(to_rhi_scissor(scissor));
		}
	}

	void RenderDevice::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
	{
		if (m_impl->current_command_buffer)
		{
			m_impl->current_command_buffer->cmd_draw(vertex_count, instance_count, first_vertex, first_instance);
		}
	}

	void RenderDevice::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
	{
		if (m_impl->current_command_buffer)
		{
			m_impl->current_command_buffer->cmd_draw_indexed(index_count, instance_count, first_index, vertex_offset, first_instance);
		}
	}

	void RenderDevice::dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
	{
		if (m_impl->current_command_buffer && !m_impl->current_framebuffer)
		{
			m_impl->current_command_buffer->cmd_dispatch(group_count_x, group_count_y, group_count_z);
		}
	}

	void RenderDevice::end_pass()
	{
		if (m_impl->current_command_buffer && m_impl->current_framebuffer)
		{
			m_impl->current_command_buffer->cmd_end_render_pass();

			std::vector<RHI::AshBarrier> end_pass_barriers;
			submit_resource_barriers(
				m_impl->current_command_buffer,
				collect_pass_end_barriers(m_impl->current_framebuffer, m_impl->current_render_pass, end_pass_barriers) ? end_pass_barriers : std::vector<RHI::AshBarrier>{});
		}
		m_impl->current_framebuffer.reset();
		m_impl->current_render_pass.reset();
		m_impl->viewport_override_active = false;
		m_impl->scissor_override_active = false;
	}

	RHI::CommandBuffer* RenderDevice::get_current_command_buffer() const
	{
		return m_impl ? m_impl->current_command_buffer : nullptr;
	}

	std::shared_ptr<RHI::TextureView> RenderDevice::get_shader_resource_view(const std::shared_ptr<RenderTarget>& render_target) const
	{
		if (!render_target || !render_target->m_impl)
		{
			return nullptr;
		}

		std::shared_ptr<RHI::Texture> texture = render_target->m_impl->get_texture();
		return texture ? texture->get_default_srv() : nullptr;
	}

	bool RenderDevice::transition_render_target_for_sampling(const std::shared_ptr<RenderTarget>& render_target)
	{
		if (!m_impl->current_command_buffer || m_impl->current_framebuffer || !render_target || !render_target->m_impl)
		{
			return false;
		}

		std::shared_ptr<RHI::Texture> texture = render_target->m_impl->get_texture();
		if (!texture)
		{
			return false;
		}

		return m_impl->current_command_buffer->cmd_transition_resource_state({ texture, RHI::AshResourceState::SRVGraphics });
	}

	bool RenderDevice::has_back_buffer_content() const
	{
		return m_impl && m_impl->back_buffer_written_this_frame;
	}
}
