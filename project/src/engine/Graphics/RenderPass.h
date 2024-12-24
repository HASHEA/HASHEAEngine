#pragma once
#include "RHIResource.h"
namespace RHI
{
	static const uint8_t                     k_max_image_outputs = 8;                // Maximum number of images/render_targets/fbo attachments usable.
	struct RenderPassCreation {

		uint16_t                        num_render_targets							= 0;
		AshFormat                       depth_stencil_format						= ASH_FORMAT_UNDEFINED;
		AshResourceState                depth_stencil_final_layout					= AshResourceState::ASH_RESOURCE_STATE_UNDEFINED;
		uint32_t                        shading_rate_image_index					= UINT32_MAX;
		AshLoadOption					depth_operation								= AshLoadOption::ASH_LOAD_DONT_CARE;
		AshLoadOption					stencil_operation							= AshLoadOption::ASH_LOAD_DONT_CARE;
		uint32_t                        multiview_mask								= 0;
		const char*                     name										= nullptr;
		AshFormat                       color_formats[k_max_image_outputs];
		AshResourceState                color_final_layouts[k_max_image_outputs];
		AshLoadOption					color_operations[k_max_image_outputs];
		inline RenderPassCreation& add_attachment(AshFormat format, AshResourceState layout, AshLoadOption load_op)
		{
			color_formats[num_render_targets] = format;
			color_operations[num_render_targets] = load_op;
			color_final_layouts[num_render_targets++] = layout;

			return *this;
		}
		inline RenderPassCreation& add_shading_rate_image()
		{
			shading_rate_image_index = num_render_targets++;
			return *this;
		}
		inline RenderPassCreation& set_depth_stencil_texture(AshFormat format, AshResourceState layout)
		{
			depth_stencil_format = format;
			depth_stencil_final_layout = layout;
			return *this;
		}
		inline RenderPassCreation& set_name(const char* _name)
		{
			name = _name;
			return *this;
		}
		inline RenderPassCreation& set_depth_stencil_operations(AshLoadOption depth, AshLoadOption stencil)
		{
			depth_operation = depth;
			stencil_operation = stencil;

			return *this;
		}
		inline RenderPassCreation& set_multiview_mask(uint32_t mask)
		{
			multiview_mask = mask;
			return *this;
		}
	}; // struct RenderPassCreation
	class RenderPass : public RHIResource
	{
	public:
		RenderPass() = default;
		virtual ~RenderPass() {}

	private:

	};

}