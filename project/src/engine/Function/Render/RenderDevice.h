#pragma once
#include "Base/hcore.h"
#include "Function/Render/Material.h"
#include "Function/Render/VertexDecl.h"
#include "Graphics/Pipeline.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace RHI
{
	struct AshBarrier;
	enum class AshResourceState : uint32_t;
	class GraphicsContext;
	class Swapchain;
	class CommandBuffer;
	class TextureView;
	struct ShaderParameterBlockLayout;
	struct ShaderResourceBindingLayout;
}

namespace AshEngine
{
	class GraphicsProgram;
	class ComputeProgram;
	class Renderer;

	enum class RenderTextureFormat : uint8_t
	{
		Unknown = 0,
		RGBA8_UNORM,
		RGBA8_SRGB,
		BGRA8_UNORM,
		BGRA8_SRGB,
		RGBA16_SFLOAT,
		RG16_SFLOAT,
		R32G32_UINT,
		RGBA32_SFLOAT,
		BC1_RGB_UNORM,
		BC1_RGB_SRGB_UNORM,
		BC1_RGBA_UNORM,
		BC1_RGBA_SRGB_UNORM,
		BC2_UNORM,
		BC2_SRGB_UNORM,
		BC3_UNORM,
		BC3_SRGB_UNORM,
		BC4_UNORM,
		BC4_SNORM,
		BC5_UNORM,
		BC5_SNORM,
		BC6H_UFLOAT,
		BC6H_SFLOAT,
		BC7_UNORM,
		BC7_SRGB_UNORM,
		D24_UNORM_S8_UINT,
		D32_SFLOAT
	};

	enum class RenderLoadAction : uint8_t
	{
		Load = 0,
		Clear,
		DontCare
	};

	enum class RenderCullMode : uint8_t
	{
		None = 0,
		Front,
		Back
	};

	/// Which winding is the mesh exterior in model space (maps through `RHI::RasterizerConvention.h` per backend).
	enum class RenderFrontFace : uint8_t
	{
		CounterClockwise = 0,
		Clockwise
	};

	enum class RenderPrimitiveTopology : uint8_t
	{
		TriangleList = 0,
		TriangleStrip,
		LineList
	};

	enum class RenderCompareOp : uint8_t
	{
		Always = 0,
		LessEqual,
		GreaterEqual
	};

	enum class RenderBlendMode : uint8_t
	{
		Opaque = 0,
		Additive
	};

	enum class RenderIndexFormat : uint8_t
	{
		UInt16 = 0,
		UInt32
	};

	enum class RenderSamplerState : uint8_t
	{
		Default = 0
	};

	struct RenderColorValue
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	struct RenderDepthStencilValue
	{
		float depth = 1.0f;
		uint32_t stencil = 0;
	};

	struct RenderViewport
	{
		int16_t x = 0;
		int16_t y = 0;
		uint16_t width = 0;
		uint16_t height = 0;
		float min_depth = 0.0f;
		float max_depth = 1.0f;
	};

	struct RenderScissor
	{
		int16_t x = 0;
		int16_t y = 0;
		uint16_t width = 0;
		uint16_t height = 0;
	};

	struct GraphicsProgramState
	{
		RenderCullMode cull_mode = RenderCullMode::None;
		RenderPrimitiveTopology primitive_topology = RenderPrimitiveTopology::TriangleList;
		bool depth_test = false;
		bool depth_write = false;
		RenderCompareOp depth_compare = RenderCompareOp::LessEqual;
		RenderBlendMode blend_mode = RenderBlendMode::Opaque;
		RenderFrontFace front_face = RenderFrontFace::CounterClockwise;
	};

	struct GraphicsProgramDesc
	{
		const char* shader_path = nullptr;
		const char* base_shader_path = nullptr;
		const char* user_shader_path = nullptr;
		const char* generated_bindings_path = nullptr;
		const char* vertex_entry = "VSMain";
		const char* fragment_entry = "PSMain";
		const char* shader_macro = nullptr;
		uint64_t source_hash = 0;
		GraphicsProgramState state{};
		const char* name = nullptr;
		std::shared_ptr<const VertexDecl> vertex_decl = nullptr;
		/// When `num_vertex_attributes > 0` or `num_vertex_streams > 0`, this layout is passed to the RHI graphics pipeline
		/// (Vulkan + D3D12). Use `Graphics/VertexInputLayout.h` for low-level layout assembly, and define Engine-side presets
		/// close to the owning vertex type (for example `Function/Render/VertexLayoutPresets.h`).
		RHI::VertexInputCreation vertex_input{};

		GraphicsProgramDesc() = default;

		GraphicsProgramDesc(
			const char* in_shader_path,
			const char* in_vertex_entry,
			const char* in_fragment_entry,
			const char* in_shader_macro,
			const GraphicsProgramState& in_state,
			const char* in_name,
			const RHI::VertexInputCreation& in_vertex_input = {})
			: shader_path(in_shader_path)
			, base_shader_path(in_shader_path)
			, vertex_entry(in_vertex_entry)
			, fragment_entry(in_fragment_entry)
			, shader_macro(in_shader_macro)
			, state(in_state)
			, name(in_name)
			, vertex_input(in_vertex_input)
		{
		}

		GraphicsProgramDesc(
			const char* in_shader_path,
			const char* in_vertex_entry,
			const char* in_fragment_entry,
			const char* in_shader_macro,
			const GraphicsProgramState& in_state,
			const char* in_name,
			std::shared_ptr<const VertexDecl> in_vertex_decl,
			const RHI::VertexInputCreation& in_vertex_input = {})
			: shader_path(in_shader_path)
			, base_shader_path(in_shader_path)
			, vertex_entry(in_vertex_entry)
			, fragment_entry(in_fragment_entry)
			, shader_macro(in_shader_macro)
			, state(in_state)
			, name(in_name)
			, vertex_decl(std::move(in_vertex_decl))
			, vertex_input(in_vertex_input)
		{
		}
	};

	struct RenderTargetDesc
	{
		uint16_t width = 1;
		uint16_t height = 1;
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		bool shader_resource = true;
		bool unordered_access = false;
		const char* name = nullptr;
		bool use_optimized_clear_value = false;
		RenderColorValue optimized_clear_color{};
		RenderDepthStencilValue optimized_clear_depth_stencil{};
	};

	struct TextureUploadDesc
	{
		uint16_t width = 1;
		uint16_t height = 1;
		RenderTextureFormat format = RenderTextureFormat::RGBA8_UNORM;
		const void* initial_data = nullptr;
		uint32_t row_pitch = 0;
		uint8_t mip_level_count = 1;
		bool srgb = false;
		const char* name = nullptr;
	};

	struct TextureSubresourceUploadDesc
	{
		uint32_t mip_level = 0;
		uint32_t array_layer = 0;
		const void* data = nullptr;
		uint32_t row_pitch = 0;
		uint32_t slice_pitch = 0;
	};

	struct TextureCubeUploadDesc
	{
		uint16_t width = 1;
		uint16_t height = 1;
		RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
		uint8_t mip_level_count = 1;
		const TextureSubresourceUploadDesc* subresources = nullptr;
		uint32_t subresource_count = 0;
		const char* name = nullptr;
	};

	ASH_API bool validate_texture_cube_upload_desc(const TextureCubeUploadDesc& desc, std::string* out_error = nullptr);

	struct UniformBufferDesc
	{
		uint32_t size = 0;
		bool cpu_write = true;
		const void* initial_data = nullptr;
		const char* name = nullptr;
	};

	struct VertexBufferDesc
	{
		uint32_t size = 0;
		uint32_t stride = 0;
		bool cpu_write = false;
		const void* initial_data = nullptr;
		const char* name = nullptr;
	};

	struct IndexBufferDesc
	{
		uint32_t size = 0;
		RenderIndexFormat format = RenderIndexFormat::UInt32;
		bool cpu_write = false;
		const void* initial_data = nullptr;
		const char* name = nullptr;
	};

	struct StorageBufferDesc
	{
		uint32_t size = 0;
		uint32_t stride = 0;
		bool cpu_write = false;
		const void* initial_data = nullptr;
		const char* name = nullptr;
	};

	class ASH_API RenderTarget
	{
	public:
		class Impl;

	public:
		RenderTarget();
		~RenderTarget();

	public:
		uint32_t get_width() const;
		uint32_t get_height() const;
		RenderTextureFormat get_format() const;
		bool is_depth_stencil() const;

	private:
		std::shared_ptr<Impl> m_impl;

	private:
		explicit RenderTarget(std::shared_ptr<Impl> impl);
		friend class GraphicsProgram;
		friend class ComputeProgram;
		friend class RenderDevice;
	};

	class ASH_API UniformBuffer
	{
	public:
		class Impl;

	public:
		UniformBuffer();
		~UniformBuffer();

	public:
		uint32_t get_size() const;
		bool update(uint32_t offset, uint32_t size, const void* data);

	private:
		std::shared_ptr<Impl> m_impl;

	private:
		explicit UniformBuffer(std::shared_ptr<Impl> impl);
		friend class GraphicsProgram;
		friend class ComputeProgram;
		friend class RenderDevice;
	};

	class ASH_API VertexBuffer
	{
	public:
		class Impl;

	public:
		VertexBuffer();
		~VertexBuffer();

	public:
		uint32_t get_size() const;
		uint32_t get_stride() const;
		uint32_t get_vertex_count() const;
		bool update(uint32_t offset, uint32_t size, const void* data);

	private:
		std::shared_ptr<Impl> m_impl;

	private:
		explicit VertexBuffer(std::shared_ptr<Impl> impl);
		friend class RenderDevice;
	};

	class ASH_API IndexBuffer
	{
	public:
		class Impl;

	public:
		IndexBuffer();
		~IndexBuffer();

	public:
		uint32_t get_size() const;
		uint32_t get_index_count() const;
		RenderIndexFormat get_format() const;
		bool update(uint32_t offset, uint32_t size, const void* data);

	private:
		std::shared_ptr<Impl> m_impl;

	private:
		explicit IndexBuffer(std::shared_ptr<Impl> impl);
		friend class RenderDevice;
	};

	class ASH_API StorageBuffer
	{
	public:
		class Impl;

	public:
		StorageBuffer();
		~StorageBuffer();

	public:
		uint32_t get_size() const;
		uint32_t get_stride() const;
		uint32_t get_element_count() const;
		bool update(uint32_t offset, uint32_t size, const void* data);

	private:
		std::shared_ptr<Impl> m_impl;

	private:
		explicit StorageBuffer(std::shared_ptr<Impl> impl);
		friend class GraphicsProgram;
		friend class ComputeProgram;
		friend class RenderDevice;
	};

	class ASH_API RenderSampler
	{
	public:
		class Impl;

	public:
		RenderSampler();
		~RenderSampler();

	public:
		const RenderSamplerDesc& get_desc() const;

	private:
		std::shared_ptr<Impl> m_impl;

	private:
		explicit RenderSampler(std::shared_ptr<Impl> impl);
		friend class GraphicsProgram;
		friend class ComputeProgram;
		friend class RenderDevice;
	};

	class ASH_API GraphicsProgram
	{
	public:
		class Impl;

	public:
		GraphicsProgram();
		~GraphicsProgram();

	public:
		bool apply_render_state(const std::function<void(GraphicsProgramState&)>& fn);
		bool get_reflected_sampler_names(std::vector<std::string>& out_names) const;
		bool get_resource_binding_layouts(std::vector<RHI::ShaderResourceBindingLayout>& out_layouts) const;
		bool get_parameter_block_layout(const char* name, RHI::ShaderParameterBlockLayout& out_layout) const;
		bool set_const_data_block(uint32_t size, const void* data);
		bool set_static_int(const char* name, int32_t value);
		bool set_static_uint(const char* name, uint32_t value);
		bool set_static_float(const char* name, float value);
		bool set_uniform_buffer(const char* name, const std::shared_ptr<UniformBuffer>& buffer);
		bool set_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer);
		bool set_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers);
		bool set_rw_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer);
		bool set_rw_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers);
		bool set_texture(const char* name, const std::shared_ptr<RenderTarget>& texture);
		bool set_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures);
		bool set_rw_texture(const char* name, const std::shared_ptr<RenderTarget>& texture);
		bool set_rw_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures);
		bool set_sampler(const char* name, const std::shared_ptr<RenderSampler>& sampler);
		bool set_sampler_array(const char* name, const std::vector<std::shared_ptr<RenderSampler>>& samplers);
		bool set_sampler(const char* name, RenderSamplerState sampler_state = RenderSamplerState::Default);
		bool set_sampler_array(const char* name, const std::vector<RenderSamplerState>& sampler_states);

	private:
		std::unique_ptr<Impl> m_impl;

	private:
		explicit GraphicsProgram(std::unique_ptr<Impl> impl);
		friend class RenderDevice;
	};

	struct ComputeProgramDesc
	{
		const char* shader_path = nullptr;
		const char* compute_entry = "CSMain";
		const char* shader_macro = nullptr;
		const char* name = nullptr;
		uint64_t source_hash = 0;
	};

	class ASH_API ComputeProgram
	{
	public:
		class Impl;

	public:
		ComputeProgram();
		~ComputeProgram();

	public:
		bool get_reflected_sampler_names(std::vector<std::string>& out_names) const;
		bool set_const_data_block(uint32_t size, const void* data);
		bool set_static_int(const char* name, int32_t value);
		bool set_static_uint(const char* name, uint32_t value);
		bool set_static_float(const char* name, float value);
		bool set_uniform_buffer(const char* name, const std::shared_ptr<UniformBuffer>& buffer);
		bool set_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer);
		bool set_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers);
		bool set_rw_storage_buffer(const char* name, const std::shared_ptr<StorageBuffer>& buffer);
		bool set_rw_storage_buffer_array(const char* name, const std::vector<std::shared_ptr<StorageBuffer>>& buffers);
		bool set_texture(const char* name, const std::shared_ptr<RenderTarget>& texture);
		bool set_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures);
		bool set_rw_texture(const char* name, const std::shared_ptr<RenderTarget>& texture);
		bool set_rw_texture_array(const char* name, const std::vector<std::shared_ptr<RenderTarget>>& textures);
		bool set_sampler(const char* name, const std::shared_ptr<RenderSampler>& sampler);
		bool set_sampler_array(const char* name, const std::vector<std::shared_ptr<RenderSampler>>& samplers);
		bool set_sampler(const char* name, RenderSamplerState sampler_state = RenderSamplerState::Default);
		bool set_sampler_array(const char* name, const std::vector<RenderSamplerState>& sampler_states);

	private:
		std::unique_ptr<Impl> m_impl;

	private:
		explicit ComputeProgram(std::unique_ptr<Impl> impl);
		friend class RenderDevice;
	};

	struct PassColorAttachment
	{
		std::shared_ptr<RenderTarget> render_target = nullptr;
		RenderLoadAction load_action = RenderLoadAction::Clear;
		RenderColorValue clear_color{};
		RHI::AshResourceState final_state = RHI::AshResourceState::Unknown;
	};

	struct PassDepthAttachment
	{
		std::shared_ptr<RenderTarget> render_target = nullptr;
		RenderLoadAction load_action = RenderLoadAction::Clear;
		RenderDepthStencilValue clear_value{};
		bool read_only = false;
		RHI::AshResourceState final_state = RHI::AshResourceState::Unknown;
	};

	struct PassDesc
	{
		std::vector<PassColorAttachment> color_attachments;
		PassDepthAttachment depth_attachment{};
		const char* name = nullptr;
		bool allow_reorder_draws = false;
	};

	ASH_API RenderColorValue get_engine_back_buffer_clear_color();
	ASH_API RHI::AshResourceState get_depth_attachment_resource_state(bool read_only, bool shader_resource_capable);
	ASH_API void fill_pipeline_state_from_graphics_program_state(
		const GraphicsProgramState& state,
		RHI::PipelineCreation& pipeline,
		uint32_t color_attachment_count = 1u,
		bool reverse_z = false);

	class ASH_API RenderDevice
	{
	public:
		~RenderDevice();

	public:
		bool begin_frame();
		bool end_frame();
		void present();

		std::shared_ptr<RenderTarget> get_back_buffer();
		std::shared_ptr<RenderTarget> create_render_target(const RenderTargetDesc& desc);
		std::shared_ptr<RenderTarget> create_texture_2d(const TextureUploadDesc& desc);
		std::shared_ptr<RenderTarget> create_texture_cube(const TextureCubeUploadDesc& desc);
		std::shared_ptr<RenderTarget> acquire_transient_render_target(const RenderTargetDesc& desc);
		void release_transient_render_target(const std::shared_ptr<RenderTarget>& render_target);
		void clear_transient_render_targets();

		std::shared_ptr<UniformBuffer> create_uniform_buffer(const UniformBufferDesc& desc);
		std::shared_ptr<VertexBuffer> create_vertex_buffer(const VertexBufferDesc& desc);
		std::shared_ptr<IndexBuffer> create_index_buffer(const IndexBufferDesc& desc);
		std::shared_ptr<StorageBuffer> create_storage_buffer(const StorageBufferDesc& desc);
		std::shared_ptr<RenderSampler> create_sampler(const RenderSamplerDesc& desc, const char* debug_name = nullptr);

		std::unique_ptr<GraphicsProgram> create_graphics_program(const GraphicsProgramDesc& desc);
		std::unique_ptr<ComputeProgram> create_compute_program(const ComputeProgramDesc& desc);
		bool reflect_graphics_program(
			const GraphicsProgramDesc& desc,
			std::vector<RHI::ShaderResourceBindingLayout>& out_binding_layouts,
			RHI::ShaderParameterBlockLayout* out_parameter_block_layout = nullptr,
			const char* parameter_block_name = nullptr);

		bool begin_pass(const PassDesc& desc);
		bool bind_graphics_program(GraphicsProgram* program, bool reverse_z = false);
		bool bind_compute_program(ComputeProgram* program);
		bool bind_vertex_buffer(uint32_t slot, const std::shared_ptr<VertexBuffer>& buffer, uint64_t offset = 0);
		bool bind_index_buffer(const std::shared_ptr<IndexBuffer>& buffer, uint64_t offset = 0);
		void set_viewport(const RenderViewport& viewport);
		void set_scissor(const RenderScissor& scissor);
		void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
		void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0, int32_t vertex_offset = 0, uint32_t first_instance = 0);
		void dispatch(uint32_t group_count_x, uint32_t group_count_y = 1, uint32_t group_count_z = 1);
		bool end_pass();

		RHI::CommandBuffer* get_current_command_buffer() const;
		std::shared_ptr<RHI::TextureView> get_shader_resource_view(const std::shared_ptr<RenderTarget>& render_target) const;
		bool transition_render_target_for_sampling(const std::shared_ptr<RenderTarget>& render_target);
		bool has_back_buffer_content() const;

		// editor begin 修改原因：GPU scene picking 需要 Function 层 texture texel readback
		struct RenderTextureTexelReadDesc
		{
			int32_t x = 0;
			int32_t y = 0;
		};

		bool queue_render_target_texel_read(
			const std::shared_ptr<RenderTarget>& render_target,
			const RenderTextureTexelReadDesc& desc);

		bool flush_queued_render_target_texel_reads(void* out_data, size_t out_size);
		// editor end

	private:
		bool ensure_back_buffer_target();
		void sync_swapchain_target();
		bool render_present_to_swapchain();
		bool collect_graphics_program_resource_barriers(GraphicsProgram* program, std::vector<RHI::AshBarrier>& out_barriers);
		bool collect_vertex_buffer_barrier(const std::shared_ptr<VertexBuffer>& buffer, std::vector<RHI::AshBarrier>& out_barriers);
		bool collect_index_buffer_barrier(const std::shared_ptr<IndexBuffer>& buffer, std::vector<RHI::AshBarrier>& out_barriers);
		bool collect_depth_attachment_barrier(const PassDepthAttachment& attachment, std::vector<RHI::AshBarrier>& out_barriers);
		bool submit_resource_barriers(const std::vector<RHI::AshBarrier>& barriers);
		bool submit_graph_resource_barriers(const std::vector<RHI::AshBarrier>& barriers);
		bool transition_graphics_program_resources(GraphicsProgram* program);
		bool transition_compute_program_resources(ComputeProgram* program);
		bool transition_vertex_buffer(const std::shared_ptr<VertexBuffer>& buffer);
		bool transition_index_buffer(const std::shared_ptr<IndexBuffer>& buffer);

	private:
		RenderDevice(RHI::GraphicsContext* graphics_context, RHI::Swapchain* swapchain);

	private:
		class Impl;
		std::unique_ptr<Impl> m_impl;
		friend class Application;
		friend class Renderer;
	};
}
