#pragma once
#include "RHICommon.h"
#include "RHIResource.h"
#include <memory>
namespace RHI
{
	class RenderPass;
	class DescriptorSetLayout;
	struct RasterizationCreation {

		AshCullModeFlagBits             cull_mode = AshCullModeFlagBits::ASH_CULL_MODE_NONE;
		AshFrontFace                    front = AshFrontFace::ASH_FRONT_FACE_COUNTER_CLOCKWISE;
		AshFillMode						fill = AshFillMode::Solid;
	}; // struct RasterizationCreation

	struct StencilOperationState {

		AshStencilOp                     fail = AshStencilOp::ASH_STENCIL_OP_KEEP;
		AshStencilOp                     pass = AshStencilOp::ASH_STENCIL_OP_KEEP;
		AshStencilOp                     depth_fail = AshStencilOp::ASH_STENCIL_OP_KEEP;
		AshCompareOp                     compare = AshCompareOp::ASH_COMPARE_OP_ALWAYS;
		uint32_t                         compare_mask = 0xff;
		uint32_t                         write_mask = 0xff;
		uint32_t                         reference = 0xff;

	}; // struct StencilOperationState

	struct DepthStencilCreation {

		StencilOperationState					front{};
		StencilOperationState					back{};
		AshCompareOp							depth_comparison = AshCompareOp::ASH_COMPARE_OP_ALWAYS;
		uint8_t									depth_enable : 1;
		uint8_t									depth_write_enable : 1;
		uint8_t									stencil_enable : 1;
		uint8_t									pad : 5;

		// Default constructor
		DepthStencilCreation() : depth_enable(0), depth_write_enable(0), stencil_enable(0) {
		}

		inline DepthStencilCreation& set_depth(bool write, AshCompareOp comparison_test)
		{
			depth_write_enable = write;
			depth_comparison = comparison_test;
			// Setting depth like this means we want to use the depth test.
			depth_enable = 1;
			return *this;
		}

	}; // struct DepthStencilCreation

	struct BlendState {

		AshBlendFactor                   source_color = AshBlendFactor::ASH_BLEND_FACTOR_ONE;
		AshBlendFactor                   destination_color = AshBlendFactor::ASH_BLEND_FACTOR_ONE;
		AshBlendOp                       color_operation = AshBlendOp::ASH_BLEND_OP_ADD;

		AshBlendFactor                   source_alpha = AshBlendFactor::ASH_BLEND_FACTOR_ONE;
		AshBlendFactor                   destination_alpha = AshBlendFactor::ASH_BLEND_FACTOR_ONE;
		AshBlendOp                       alpha_operation = AshBlendOp::ASH_BLEND_OP_ADD;

		AshColorWriteMask                color_write_mask = AshColorWriteMask::All;

		uint8_t                              blend_enabled : 1;
		uint8_t                              separate_blend : 1;
		uint8_t                              pad : 6;


		BlendState() : blend_enabled(0), separate_blend(0) {
		}

		inline BlendState& set_color(AshBlendFactor source, AshBlendFactor destination, AshBlendOp operation)
		{
			source_color = source;
			destination_color = destination;
			color_operation = operation;
			blend_enabled = 1;

			return *this;
		}
		inline BlendState& set_alpha(AshBlendFactor source, AshBlendFactor destination, AshBlendOp operation)
		{
			source_alpha = source;
			destination_alpha = destination;
			alpha_operation = operation;
			separate_blend = 1;

			return *this;
		}
		inline BlendState& set_color_write_mask(AshColorWriteMask value)
		{
			color_write_mask = value;
			return *this;
		}

	}; // struct BlendState

	struct BlendStateCreation {

		BlendState                      blend_states[k_max_image_outputs];
		uint32_t                        active_states = 0;

		inline BlendState& add_blend_state()
		{
			return blend_states[active_states++];
		}

	}; // BlendStateCreation
	struct VertexStream {

		uint16_t							binding		= 0;
		uint16_t							stride		= 0;
		AshVertexInputRate					input_rate	= AshVertexInputRate::PerVertex;

	}; // struct VertexStream

	struct VertexAttribute {

		uint16_t                             location	= 0;
		uint16_t                             binding	= 0;
		uint32_t                             offset		= 0;
		AshVertexComponentFormat		     format		= AshVertexComponentFormat::FormatCount;

	}; // struct VertexAttribute

	struct VertexInputCreation {

		uint32_t								num_vertex_streams = 0;
		uint32_t								num_vertex_attributes = 0;
		VertexStream							vertex_streams[k_max_vertex_streams];
		VertexAttribute							vertex_attributes[k_max_vertex_attributes];

		inline VertexInputCreation& add_vertex_stream(const VertexStream& stream)
		{
			vertex_streams[num_vertex_streams++] = stream;
			return *this;
		}
		inline VertexInputCreation& add_vertex_attribute(const VertexAttribute& attribute)
		{
			vertex_attributes[num_vertex_attributes++] = attribute;
			return *this;
		}
	}; // struct VertexInputCreation

	struct ShaderStage {

		const char*								code		= nullptr;
		uint32_t								code_size	= 0;
		AshShaderStageFlagBits					type		= AshShaderStageFlagBits::ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

	}; // struct ShaderStage

	struct ShaderStateCreation {

		ShaderStage                     stages[k_max_shader_stages];
		const char* name = nullptr;
		uint32_t                             stages_count = 0;
		uint32_t                             spv_input = 0;
		// Building helpers
		inline ShaderStateCreation& add_stage(const char* code, size_t code_size, AshShaderStageFlagBits type)
		{
			for (uint32_t s = 0; s < stages_count; ++s) {
				ShaderStage& stage = stages[s];

				if (stage.type == type) {
					stage.code = code;
					stage.code_size = (uint32_t)code_size;
					return *this;
				}
			}
			stages[stages_count].code = code;
			stages[stages_count].code_size = (uint32_t)code_size;
			stages[stages_count].type = type;
			++stages_count;
			return *this;
		}
		inline ShaderStateCreation& set_spv_input(bool value)
		{
			spv_input = value;
			return *this;
		}
	}; // struct ShaderStateCreation

	struct ViewportState {
		uint32_t                             num_viewports = 0;
		uint32_t                             num_scissors = 0;
		Viewport* viewport = nullptr;
		Rect2DInt* scissors = nullptr;
	}; // struct ViewportState
	struct PipelineCreation
	{
		RasterizationCreation						rasterization;
		DepthStencilCreation						depth_stencil;
		BlendStateCreation							blend_state;
		VertexInputCreation							vertex_input;
		ShaderStateCreation							shaders;
		AshPrimitiveTopology						primitiveTopology = AshPrimitiveTopology::ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		uint32_t									flags;
		std::shared_ptr<RenderPass>					render_pass;
		std::shared_ptr<DescriptorSetLayout>		descriptor_set_layout[k_max_descriptor_set_layouts];
		const ViewportState*						viewport = nullptr;
		uint32_t                             num_active_layouts = 0;
		inline PipelineCreation& add_descriptor_set_layout(std::shared_ptr<DescriptorSetLayout> layout)
		{
			descriptor_set_layout[num_active_layouts++] = layout;
			return *this;
		}
		const char* name = nullptr;
	};
	class Pipeline : public RHIResource
	{
	public:
		Pipeline() = default;
		virtual ~Pipeline() {}

	private:

	};

}