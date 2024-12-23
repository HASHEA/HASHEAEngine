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

	}; // struct RenderPassCreation
	class RenderPass : public RHIResource
	{
	public:
		RenderPass() = default;
		virtual ~RenderPass() {}

	private:

	};

}