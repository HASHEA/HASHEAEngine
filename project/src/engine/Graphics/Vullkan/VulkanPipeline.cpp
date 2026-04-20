#include "VulkanPipeline.h"
#include "Graphics/RasterizerConvention.h"
#include "VulkanContext.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPass.h"
#include "VulkanShader.h"
#include "Graphics/VertexInputLayout.h"
#include "Base/hlog.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace RHI
{
	namespace
	{
		struct TemporaryShaderStage
		{
			VkShaderModule module = VK_NULL_HANDLE;
			VkPipelineShaderStageCreateInfo stage_info{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			ParseResult reflection{};
			bool has_reflection = false;
		};

		struct StageSpecializationData
		{
			std::vector<VkSpecializationMapEntry> map_entries;
			std::vector<uint8_t> data;
			VkSpecializationInfo info{};
			bool valid = false;
		};

		static bool has_graphics_stage(AshShaderStageFlagBits stage)
		{
			return stage == ASH_SHADER_STAGE_VERTEX_BIT ||
				stage == ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
				stage == ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT ||
				stage == ASH_SHADER_STAGE_GEOMETRY_BIT ||
				stage == ASH_SHADER_STAGE_FRAGMENT_BIT ||
				stage == ASH_SHADER_STAGE_TASK_BIT_EXT ||
				stage == ASH_SHADER_STAGE_MESH_BIT_EXT;
		}

		static bool has_compute_stage(AshShaderStageFlagBits stage)
		{
			return stage == ASH_SHADER_STAGE_COMPUTE_BIT;
		}

		static VkStencilOpState to_vk_stencil_state(const StencilOperationState& state)
		{
			VkStencilOpState vk_state{};
			vk_state.failOp = ash_stencil_op_to_vk(state.fail);
			vk_state.passOp = ash_stencil_op_to_vk(state.pass);
			vk_state.depthFailOp = ash_stencil_op_to_vk(state.depth_fail);
			vk_state.compareOp = ash_compare_option_to_vk(state.compare);
			vk_state.compareMask = state.compare_mask;
			vk_state.writeMask = state.write_mask;
			vk_state.reference = state.reference;
			return vk_state;
		}

		static bool collect_shader_stages(const PipelineCreation& ci,
			std::vector<VkPipelineShaderStageCreateInfo>& stage_infos,
			std::vector<ParseResult>& stage_reflections,
			std::vector<TemporaryShaderStage>& temporary_stages,
			bool& has_compute,
			bool& has_graphics)
		{
			stage_infos.clear();
			stage_reflections.clear();
			temporary_stages.clear();
			has_compute = false;
			has_graphics = false;

			for (uint32_t i = 0; i < ci.shaders.stages_count; ++i)
			{
				const ShaderStage& stage = ci.shaders.stages[i];
				has_compute = has_compute || has_compute_stage(stage.type);
				has_graphics = has_graphics || has_graphics_stage(stage.type);

				if (stage.shader)
				{
					auto vulkan_shader = std::dynamic_pointer_cast<VulkanShader>(stage.shader);
					if (!vulkan_shader)
					{
						HLogError("Pipeline '{}' received a non-Vulkan shader object.", ci.name ? ci.name : "<unnamed>");
						return false;
					}

					stage_infos.push_back(vulkan_shader->get_stage_info());
					stage_reflections.push_back(vulkan_shader->get_reflection_data());
					continue;
				}

				if (!ci.shaders.spv_input || !stage.code || stage.code_size == 0)
				{
					HLogError("Pipeline '{}' stage {} does not provide a valid Vulkan shader input.", ci.name ? ci.name : "<unnamed>", static_cast<uint32_t>(stage.type));
					return false;
				}

				if ((stage.code_size % sizeof(uint32_t)) != 0)
				{
					HLogError("Pipeline '{}' stage {} SPIR-V byte size is not 4-byte aligned.", ci.name ? ci.name : "<unnamed>", static_cast<uint32_t>(stage.type));
					return false;
				}

				TemporaryShaderStage temp_stage{};
				VkShaderModuleCreateInfo create_info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
				create_info.codeSize = stage.code_size;
				create_info.pCode = reinterpret_cast<const uint32_t*>(stage.code);
				VK_CHECK_RESULT(vkCreateShaderModule(VulkanContext::get_vulkan_device(), &create_info, VulkanContext::get_vulkan_allocation_callbacks(), &temp_stage.module));

				temp_stage.stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				temp_stage.stage_info.module = temp_stage.module;
				temp_stage.stage_info.stage = ash_shader_stage_to_vk(stage.type);
				temp_stage.stage_info.pName = stage.entry_point ? stage.entry_point : "main";

				if (parse_binary_spv(reinterpret_cast<const uint32_t*>(stage.code), stage.code_size / sizeof(uint32_t), stage.type, &temp_stage.reflection))
				{
					temp_stage.has_reflection = true;
				}
				else
				{
					HLogWarning("Pipeline '{}' stage {} reflection failed, continuing without reflected metadata.", ci.name ? ci.name : "<unnamed>", static_cast<uint32_t>(stage.type));
				}

				stage_infos.push_back(temp_stage.stage_info);
				stage_reflections.push_back(temp_stage.reflection);
				temporary_stages.push_back(temp_stage);
			}

			return !stage_infos.empty();
		}

		static bool build_stage_specialization_data(
			const ParseResult& stage_reflection,
			const std::unordered_map<std::string, ConstantValue>* specialization_overrides,
			StageSpecializationData& specialization_data)
		{
			specialization_data = StageSpecializationData{};
			if (stage_reflection.specialization_constants_count == 0)
			{
				return true;
			}

			for (uint32_t i = 0; i < stage_reflection.specialization_constants_count; ++i)
			{
				const SpecializationConstant& spec = stage_reflection.specialization_constants[i];
				const SpecializationName& spec_name = stage_reflection.specialization_names[i];

				ConstantValue value = spec.default_value;
				if (specialization_overrides && spec_name.name[0] != '\0')
				{
					auto it = specialization_overrides->find(spec_name.name);
					if (it != specialization_overrides->end())
					{
						value = it->second;
					}
				}

				const size_t offset = specialization_data.data.size();
				const uint32_t byte_stride = std::max<uint32_t>(spec.byte_stride, 4u);
				specialization_data.data.resize(offset + byte_stride, 0);

				switch (value.type)
				{
				case ConstantValue::Type::Type_i32:
					memcpy(specialization_data.data.data() + offset, &value.value.value_i, sizeof(int32_t));
					break;
				case ConstantValue::Type::Type_f32:
					memcpy(specialization_data.data.data() + offset, &value.value.value_f, sizeof(float));
					break;
				case ConstantValue::Type::Type_u32:
				default:
					memcpy(specialization_data.data.data() + offset, &value.value.value_u, sizeof(uint32_t));
					break;
				}

				VkSpecializationMapEntry entry{};
				entry.constantID = spec.binding;
				entry.offset = static_cast<uint32_t>(offset);
				entry.size = byte_stride;
				specialization_data.map_entries.push_back(entry);
			}

			specialization_data.info.mapEntryCount = static_cast<uint32_t>(specialization_data.map_entries.size());
			specialization_data.info.pMapEntries = specialization_data.map_entries.data();
			specialization_data.info.dataSize = specialization_data.data.size();
			specialization_data.info.pData = specialization_data.data.data();
			specialization_data.valid = specialization_data.info.mapEntryCount > 0;
			return true;
		}

		static std::shared_ptr<DescriptorSetLayout> create_empty_descriptor_set_layout(uint32_t set_index)
		{
			DescriptorSetLayoutCreation creation{};
			creation.set_set_index(set_index);
			return VulkanDescriptorSetLayout::create(creation);
		}

		static bool build_pipeline_layouts(const PipelineCreation& ci, const ParseResult& merged_reflection,
			std::vector<std::shared_ptr<DescriptorSetLayout>>& descriptor_set_layouts)
		{
			descriptor_set_layouts.clear();
			uint32_t max_set_index = 0;
			bool has_any_layout = false;

			for (uint32_t i = 0; i < ci.num_active_layouts; ++i)
			{
				if (ci.descriptor_set_layout[i])
				{
					has_any_layout = true;
					max_set_index = std::max(max_set_index, i);
				}
			}
			for (uint32_t i = 0; i < merged_reflection.set_count; ++i)
			{
				has_any_layout = true;
				max_set_index = std::max(max_set_index, merged_reflection.sets[i].set_index);
			}

			if (!has_any_layout)
			{
				return true;
			}

			descriptor_set_layouts.resize(max_set_index + 1);

			for (uint32_t i = 0; i < descriptor_set_layouts.size(); ++i)
			{
				if (i < ci.num_active_layouts && ci.descriptor_set_layout[i])
				{
					descriptor_set_layouts[i] = ci.descriptor_set_layout[i];
					continue;
				}

				const DescriptorSetLayoutCreation* reflected_layout = nullptr;
				for (uint32_t reflected_index = 0; reflected_index < merged_reflection.set_count; ++reflected_index)
				{
					if (merged_reflection.sets[reflected_index].set_index == i)
					{
						reflected_layout = &merged_reflection.sets[reflected_index];
						break;
					}
				}

				descriptor_set_layouts[i] = reflected_layout ?
					std::static_pointer_cast<DescriptorSetLayout>(VulkanDescriptorSetLayout::create(*reflected_layout)) :
					create_empty_descriptor_set_layout(i);

				if (!descriptor_set_layouts[i])
				{
					HLogError("Failed to build descriptor set layout {} for pipeline '{}'.", i, ci.name ? ci.name : "<unnamed>");
					return false;
				}
			}

			return true;
		}

		static bool create_pipeline_layout(const PipelineCreation& ci, const ParseResult& merged_reflection,
			std::vector<std::shared_ptr<DescriptorSetLayout>>& descriptor_set_layouts, VkPipelineLayout& pipeline_layout)
		{
			if (!build_pipeline_layouts(ci, merged_reflection, descriptor_set_layouts))
			{
				return false;
			}

			std::vector<VkDescriptorSetLayout> vk_set_layouts;
			vk_set_layouts.reserve(descriptor_set_layouts.size());
			for (const auto& layout : descriptor_set_layouts)
			{
				vk_set_layouts.push_back(layout ? reinterpret_cast<VkDescriptorSetLayout>(layout->get_native_handle()) : VK_NULL_HANDLE);
			}

			VkPushConstantRange push_constant_range{};
			VkPushConstantRange* push_constant_range_ptr = nullptr;
			if (merged_reflection.push_constants_count > 0 && merged_reflection.push_constants_stride > 0)
			{
				push_constant_range.offset = 0;
				push_constant_range.size = merged_reflection.push_constants_stride;
				push_constant_range.stageFlags = ash_shader_stage_flags_to_vk(merged_reflection.pipeline_info.active_stages);
				push_constant_range_ptr = &push_constant_range;
			}

			VkPipelineLayoutCreateInfo layout_create_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			layout_create_info.setLayoutCount = static_cast<uint32_t>(vk_set_layouts.size());
			layout_create_info.pSetLayouts = vk_set_layouts.empty() ? nullptr : vk_set_layouts.data();
			layout_create_info.pushConstantRangeCount = push_constant_range_ptr ? 1u : 0u;
			layout_create_info.pPushConstantRanges = push_constant_range_ptr;
			VK_CHECK_RESULT(vkCreatePipelineLayout(VulkanContext::get_vulkan_device(), &layout_create_info, VulkanContext::get_vulkan_allocation_callbacks(), &pipeline_layout));
			return true;
		}

	}

	VulkanPipeline::VulkanPipeline(const PipelineCreation& ci, const std::unordered_map<std::string, ConstantValue>* specialization_overrides)
	{
		name = ci.name;
		vk_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

		std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos;
		std::vector<ParseResult> stage_reflections;
		std::vector<TemporaryShaderStage> temporary_stages;
		bool has_compute = false;
		bool has_graphics = false;

		if (!collect_shader_stages(ci, shader_stage_infos, stage_reflections, temporary_stages, has_compute, has_graphics))
		{
			return;
		}

		std::vector<StageSpecializationData> specialization_datas(shader_stage_infos.size());
		for (size_t i = 0; i < shader_stage_infos.size(); ++i)
		{
			build_stage_specialization_data(stage_reflections[i], specialization_overrides, specialization_datas[i]);
			if (specialization_datas[i].valid)
			{
				shader_stage_infos[i].pSpecializationInfo = &specialization_datas[i].info;
			}
		}

		ParseResult merged_reflection{};
		if (!stage_reflections.empty() && !merge_parse_results(stage_reflections.data(), static_cast<uint32_t>(stage_reflections.size()), &merged_reflection))
		{
			HLogWarning("Pipeline '{}' failed to merge shader reflection data. Falling back to explicit pipeline metadata only.", name ? name : "<unnamed>");
			merged_reflection = ParseResult{};
		}

		if (!create_pipeline_layout(ci, merged_reflection, m_descriptorSetLayouts, vk_pipeline_layout))
		{
			for (auto& temporary_stage : temporary_stages)
			{
				if (temporary_stage.module != VK_NULL_HANDLE)
				{
					vkDestroyShaderModule(VulkanContext::get_vulkan_device(), temporary_stage.module, VulkanContext::get_vulkan_allocation_callbacks());
					temporary_stage.module = VK_NULL_HANDLE;
				}
			}
			return;
		}

		const bool create_compute_pipeline = has_compute && !has_graphics;
		vk_bind_point = create_compute_pipeline ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

		if (create_compute_pipeline)
		{
			const auto compute_it = std::find_if(shader_stage_infos.begin(), shader_stage_infos.end(), [](const VkPipelineShaderStageCreateInfo& info) {
				return info.stage == VK_SHADER_STAGE_COMPUTE_BIT;
				});
			if (compute_it == shader_stage_infos.end())
			{
				HLogError("Pipeline '{}' requested compute creation but no compute shader stage was provided.", name ? name : "<unnamed>");
			}
			else
			{
				VkComputePipelineCreateInfo create_info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
				create_info.flags = static_cast<VkPipelineCreateFlags>(ci.flags);
				create_info.layout = vk_pipeline_layout;
				create_info.stage = *compute_it;
				VK_CHECK_RESULT(vkCreateComputePipelines(VulkanContext::get_vulkan_device(), VulkanContext::get_pipeline_cache(), 1, &create_info, VulkanContext::get_vulkan_allocation_callbacks(), &vk_pipeline));
			}
		}
		else
		{
			const bool use_explicit_vertex_input =
				ci.vertex_input.num_vertex_attributes > 0 || ci.vertex_input.num_vertex_streams > 0;
			const VertexInputCreation& vertex_input = use_explicit_vertex_input ?
				ci.vertex_input : merged_reflection.pipeline_info.vertex_input;
			const bool vertex_input_valid = use_explicit_vertex_input ?
				validate_vertex_input_layout(vertex_input, name) :
				validate_vertex_input_layout_basic(vertex_input, name);
			if (vertex_input_valid)
			{
				std::vector<VkVertexInputBindingDescription> binding_descriptions;
				binding_descriptions.reserve(vertex_input.num_vertex_streams);
				for (uint32_t i = 0; i < vertex_input.num_vertex_streams; ++i)
				{
					const VertexStream& stream = vertex_input.vertex_streams[i];
					VkVertexInputBindingDescription description{};
					description.binding = stream.binding;
					description.stride = stream.stride;
					description.inputRate = ash_vertex_input_rate_to_vk(stream.input_rate);
					binding_descriptions.push_back(description);
				}

				std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
				attribute_descriptions.reserve(vertex_input.num_vertex_attributes);
				for (uint32_t i = 0; i < vertex_input.num_vertex_attributes; ++i)
				{
					const VertexAttribute& attribute = vertex_input.vertex_attributes[i];
					VkVertexInputAttributeDescription description{};
					description.location = attribute.location;
					description.binding = attribute.binding;
					description.offset = attribute.offset;
					description.format = ash_vertex_component_format_to_vk(attribute.format);
					attribute_descriptions.push_back(description);
				}

				VkPipelineVertexInputStateCreateInfo vertex_input_state{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
				vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(binding_descriptions.size());
				vertex_input_state.pVertexBindingDescriptions = binding_descriptions.empty() ? nullptr : binding_descriptions.data();
				vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
				vertex_input_state.pVertexAttributeDescriptions = attribute_descriptions.empty() ? nullptr : attribute_descriptions.data();

				VkPipelineInputAssemblyStateCreateInfo input_assembly_state{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
				input_assembly_state.topology = ash_primitive_topology_to_vk(ci.primitiveTopology);
				input_assembly_state.primitiveRestartEnable = input_assembly_state.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
					input_assembly_state.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
					input_assembly_state.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY ||
					input_assembly_state.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;

				std::vector<VkDynamicState> dynamic_states;
				std::vector<VkViewport> viewports;
				std::vector<VkRect2D> scissors;
				VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
				if (ci.viewport && ci.viewport->viewport && ci.viewport->scissors && ci.viewport->num_viewports > 0 && ci.viewport->num_scissors > 0)
				{
					viewports.reserve(ci.viewport->num_viewports);
					for (uint32_t i = 0; i < ci.viewport->num_viewports; ++i)
					{
						const Viewport& viewport = ci.viewport->viewport[i];
						VkViewport vk_viewport{};
						vk_viewport.x = static_cast<float>(viewport.rect.x);
						vk_viewport.y = static_cast<float>(viewport.rect.y);
						vk_viewport.width = static_cast<float>(viewport.rect.width);
						vk_viewport.height = static_cast<float>(viewport.rect.height);
						vk_viewport.minDepth = viewport.min_depth;
						vk_viewport.maxDepth = viewport.max_depth;
						viewports.push_back(vk_viewport);
					}
					scissors.reserve(ci.viewport->num_scissors);
					for (uint32_t i = 0; i < ci.viewport->num_scissors; ++i)
					{
						const Rect2DInt& scissor = ci.viewport->scissors[i];
						scissors.push_back({ { scissor.x, scissor.y }, { scissor.width, scissor.height } });
					}
					viewport_state.viewportCount = static_cast<uint32_t>(viewports.size());
					viewport_state.pViewports = viewports.data();
					viewport_state.scissorCount = static_cast<uint32_t>(scissors.size());
					viewport_state.pScissors = scissors.data();
				}
				else
				{
					dynamic_states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
					dynamic_states.push_back(VK_DYNAMIC_STATE_SCISSOR);
					viewport_state.viewportCount = 1;
					viewport_state.scissorCount = 1;
				}

				VkPipelineRasterizationStateCreateInfo rasterization_state{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
				rasterization_state.depthClampEnable = VK_FALSE;
				rasterization_state.rasterizerDiscardEnable = VK_FALSE;
				rasterization_state.polygonMode = ash_fill_mode_to_vk(ci.rasterization.fill);
				rasterization_state.cullMode = ash_cull_mode_to_vk(ci.rasterization.cull_mode);
				rasterization_state.frontFace = mesh_winding_to_vulkan_front_face(ci.rasterization.front);
				rasterization_state.depthBiasEnable = VK_FALSE;
				rasterization_state.lineWidth = 1.0f;

				VkPipelineMultisampleStateCreateInfo multisample_state{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
				multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
				multisample_state.sampleShadingEnable = VK_FALSE;

				VkPipelineDepthStencilStateCreateInfo depth_stencil_state{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
				depth_stencil_state.depthTestEnable = ci.depth_stencil.depth_enable ? VK_TRUE : VK_FALSE;
				depth_stencil_state.depthWriteEnable = ci.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE;
				depth_stencil_state.depthCompareOp = ash_compare_option_to_vk(ci.depth_stencil.depth_comparison);
				depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
				depth_stencil_state.stencilTestEnable = ci.depth_stencil.stencil_enable ? VK_TRUE : VK_FALSE;
				depth_stencil_state.front = to_vk_stencil_state(ci.depth_stencil.front);
				depth_stencil_state.back = to_vk_stencil_state(ci.depth_stencil.back);

				uint32_t color_attachment_count = 0;
				if (ci.render_pass)
				{
					color_attachment_count = ci.render_pass->get_color_attachment_count();
				}
				color_attachment_count = std::max(color_attachment_count, ci.blend_state.active_states);

				std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
				blend_attachments.resize(color_attachment_count);
				for (uint32_t i = 0; i < color_attachment_count; ++i)
				{
					const BlendState& blend_state = i < ci.blend_state.active_states ? ci.blend_state.blend_states[i] : BlendState{};
					VkPipelineColorBlendAttachmentState& attachment = blend_attachments[i];
					attachment.blendEnable = blend_state.blend_enabled ? VK_TRUE : VK_FALSE;
					attachment.srcColorBlendFactor = ash_blend_factor_to_vk(blend_state.source_color);
					attachment.dstColorBlendFactor = ash_blend_factor_to_vk(blend_state.destination_color);
					attachment.colorBlendOp = ash_blend_op_to_vk(blend_state.color_operation);
					attachment.srcAlphaBlendFactor = ash_blend_factor_to_vk(blend_state.separate_blend ? blend_state.source_alpha : blend_state.source_color);
					attachment.dstAlphaBlendFactor = ash_blend_factor_to_vk(blend_state.separate_blend ? blend_state.destination_alpha : blend_state.destination_color);
					attachment.alphaBlendOp = ash_blend_op_to_vk(blend_state.separate_blend ? blend_state.alpha_operation : blend_state.color_operation);
					attachment.colorWriteMask = ash_color_write_mask_to_vk(blend_state.color_write_mask);
				}

				VkPipelineColorBlendStateCreateInfo color_blend_state{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
				color_blend_state.logicOpEnable = VK_FALSE;
				color_blend_state.attachmentCount = static_cast<uint32_t>(blend_attachments.size());
				color_blend_state.pAttachments = blend_attachments.empty() ? nullptr : blend_attachments.data();

				VkPipelineDynamicStateCreateInfo dynamic_state_info{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
				dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
				dynamic_state_info.pDynamicStates = dynamic_states.empty() ? nullptr : dynamic_states.data();

				VkPipelineTessellationStateCreateInfo tessellation_state{ VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
				VkPipelineTessellationStateCreateInfo* tessellation_state_ptr = nullptr;
				if (ci.primitiveTopology == ASH_PRIMITIVE_TOPOLOGY_PATCH_LIST)
				{
					tessellation_state.patchControlPoints = 3;
					tessellation_state_ptr = &tessellation_state;
				}

				VkGraphicsPipelineCreateInfo create_info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
				create_info.flags = static_cast<VkPipelineCreateFlags>(ci.flags);
				create_info.stageCount = static_cast<uint32_t>(shader_stage_infos.size());
				create_info.pStages = shader_stage_infos.data();
				create_info.pVertexInputState = &vertex_input_state;
				create_info.pInputAssemblyState = &input_assembly_state;
				create_info.pTessellationState = tessellation_state_ptr;
				create_info.pViewportState = &viewport_state;
				create_info.pRasterizationState = &rasterization_state;
				create_info.pMultisampleState = &multisample_state;
				create_info.pDepthStencilState = &depth_stencil_state;
				create_info.pColorBlendState = &color_blend_state;
				create_info.pDynamicState = dynamic_states.empty() ? nullptr : &dynamic_state_info;
				create_info.layout = vk_pipeline_layout;

				VkPipelineRenderingCreateInfoKHR pipeline_rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
				std::vector<VkFormat> color_formats;
				if (VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering))
				{
					if (!ci.render_pass)
					{
						HLogError("Pipeline '{}' requires a render pass description to derive dynamic rendering formats.", name ? name : "<unnamed>");
					}
					else
					{
						color_formats.reserve(ci.render_pass->get_color_attachment_count());
						for (uint32_t i = 0; i < ci.render_pass->get_color_attachment_count(); ++i)
						{
							color_formats.push_back(get_vk_texture_format_info(ci.render_pass->get_color_attachment_format(i)).vkFormat);
						}
						pipeline_rendering_info.colorAttachmentCount = static_cast<uint32_t>(color_formats.size());
						pipeline_rendering_info.pColorAttachmentFormats = color_formats.empty() ? nullptr : color_formats.data();
						const VkFormat depth_stencil_format = get_vk_texture_format_info(ci.render_pass->get_depth_stencil_format()).vkFormat;
						const bool has_depth_component = TextureFormat::has_depth(depth_stencil_format);
						const bool has_stencil_component = TextureFormat::has_stencil(depth_stencil_format);
						pipeline_rendering_info.depthAttachmentFormat = has_depth_component ? depth_stencil_format : VK_FORMAT_UNDEFINED;
						pipeline_rendering_info.stencilAttachmentFormat = has_stencil_component ? depth_stencil_format : VK_FORMAT_UNDEFINED;
						pipeline_rendering_info.viewMask = ci.render_pass->get_multiview_mask();
						create_info.pNext = &pipeline_rendering_info;
					}
				}
				else
				{
					if (!ci.render_pass)
					{
						HLogError("Pipeline '{}' requires a VkRenderPass when dynamic rendering is unavailable.", name ? name : "<unnamed>");
					}
					else
					{
						create_info.renderPass = reinterpret_cast<VkRenderPass>(ci.render_pass->get_native_handle());
						create_info.subpass = 0;
					}
				}

				if ((VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering) && ci.render_pass) ||
					(!VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering) && create_info.renderPass != VK_NULL_HANDLE))
				{
					VK_CHECK_RESULT(vkCreateGraphicsPipelines(VulkanContext::get_vulkan_device(), VulkanContext::get_pipeline_cache(), 1, &create_info, VulkanContext::get_vulkan_allocation_callbacks(), &vk_pipeline));
				}
			}
		}
		for (auto& temporary_stage : temporary_stages)
		{
			if (temporary_stage.module != VK_NULL_HANDLE)
			{
				vkDestroyShaderModule(VulkanContext::get_vulkan_device(), temporary_stage.module, VulkanContext::get_vulkan_allocation_callbacks());
				temporary_stage.module = VK_NULL_HANDLE;
			}
		}

		if (vk_pipeline != VK_NULL_HANDLE && name)
		{
			VulkanContext::set_resource_name(VK_OBJECT_TYPE_PIPELINE, (uint64_t)vk_pipeline, name);
			char layout_name[128]{};
			const size_t name_length = std::min(strlen(name), sizeof(layout_name) - 8);
			memcpy(layout_name, name, name_length);
			memcpy(layout_name + name_length, "_Layout", 8);
			VulkanContext::set_resource_name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)vk_pipeline_layout, layout_name);
		}
	}

	VulkanPipeline::~VulkanPipeline()
	{
		m_descriptorSetLayouts.clear();
		if (vk_pipeline != VK_NULL_HANDLE)
		{
			const uint32_t current_frame = VulkanContext::get_current_frame();
			if (VulkanContext::get_vulkan_device() != VK_NULL_HANDLE && current_frame != UINT32_MAX)
			{
				const VkPipeline pipeline = vk_pipeline;
				VulkanContext::get_current_frame_deletion_queue().emplace([pipeline]() {
					vkDestroyPipeline(VulkanContext::get_vulkan_device(), pipeline, VulkanContext::get_vulkan_allocation_callbacks());
				});
			}
			else if (VulkanContext::get_vulkan_device() != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(VulkanContext::get_vulkan_device(), vk_pipeline, VulkanContext::get_vulkan_allocation_callbacks());
			}
			vk_pipeline = VK_NULL_HANDLE;
		}
		if (vk_pipeline_layout != VK_NULL_HANDLE)
		{
			const uint32_t current_frame = VulkanContext::get_current_frame();
			if (VulkanContext::get_vulkan_device() != VK_NULL_HANDLE && current_frame != UINT32_MAX)
			{
				const VkPipelineLayout pipeline_layout = vk_pipeline_layout;
				VulkanContext::get_current_frame_deletion_queue().emplace([pipeline_layout]() {
					vkDestroyPipelineLayout(VulkanContext::get_vulkan_device(), pipeline_layout, VulkanContext::get_vulkan_allocation_callbacks());
				});
			}
			else if (VulkanContext::get_vulkan_device() != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(VulkanContext::get_vulkan_device(), vk_pipeline_layout, VulkanContext::get_vulkan_allocation_callbacks());
			}
			vk_pipeline_layout = VK_NULL_HANDLE;
		}
	}

	bool VulkanPipeline::is_valid() const
	{
		return vk_pipeline != VK_NULL_HANDLE;
	}

	VkPipeline VulkanPipeline::get_native_handle() const
	{
		return vk_pipeline;
	}

	VkPipelineLayout VulkanPipeline::get_pipeline_layout() const
	{
		return vk_pipeline_layout;
	}

	VkPipelineBindPoint VulkanPipeline::get_bind_point() const
	{
		return vk_bind_point;
	}
}
