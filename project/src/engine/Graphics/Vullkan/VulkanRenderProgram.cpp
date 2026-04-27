#include "VulkanRenderProgram.h"
#include "Base/hlog.h"
#include "VulkanCommandBuffer.h"
#include "VulkanContext.h"
#include "VulkanDescriptorSet.h"
#include "VulkanHelper.hpp"
#include "VulkanPipeline.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include <algorithm>
#include <cstring>

namespace RHI
{
	namespace
	{
		class VulkanRenderProgramBinder final : public IRenderProgramBinder
		{
		public:
			explicit VulkanRenderProgramBinder(VulkanRenderProgramBase& owner)
				: m_owner(owner)
			{
			}

			IRenderProgramBinder& begin_bind() override
			{
				m_owner.begin_resource_binding();
				return *this;
			}

			IRenderProgramBinder& add_bind_uav(const char* name, std::shared_ptr<BufferView> uav) override
			{
				m_owner.record_binding_result(m_owner.bind_uav(name, std::vector<std::shared_ptr<BufferView>>{ uav }));
				return *this;
			}

			IRenderProgramBinder& add_bind_uav(const char* name, std::shared_ptr<TextureView> uav) override
			{
				m_owner.record_binding_result(m_owner.bind_uav(name, std::vector<std::shared_ptr<TextureView>>{ uav }));
				return *this;
			}

			IRenderProgramBinder& add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& uavs) override
			{
				m_owner.record_binding_result(m_owner.bind_uav(name, uavs));
				return *this;
			}

			IRenderProgramBinder& add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& uavs) override
			{
				m_owner.record_binding_result(m_owner.bind_uav(name, uavs));
				return *this;
			}

			IRenderProgramBinder& add_bind_srv(const char* name, std::shared_ptr<BufferView> srv) override
			{
				m_owner.record_binding_result(m_owner.bind_srv(name, std::vector<std::shared_ptr<BufferView>>{ srv }));
				return *this;
			}

			IRenderProgramBinder& add_bind_srv(const char* name, std::shared_ptr<TextureView> srv) override
			{
				m_owner.record_binding_result(m_owner.bind_srv(name, std::vector<std::shared_ptr<TextureView>>{ srv }));
				return *this;
			}

			IRenderProgramBinder& add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& srvs) override
			{
				m_owner.record_binding_result(m_owner.bind_srv(name, srvs));
				return *this;
			}

			IRenderProgramBinder& add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& srvs) override
			{
				m_owner.record_binding_result(m_owner.bind_srv(name, srvs));
				return *this;
			}

			IRenderProgramBinder& add_bind_cbv(const char* name, std::shared_ptr<BufferView> cbv) override
			{
				m_owner.record_binding_result(m_owner.bind_cbv(name, cbv));
				return *this;
			}

			IRenderProgramBinder& add_bind_sampler(const char* name, std::shared_ptr<Sampler> sampler) override
			{
				m_owner.record_binding_result(m_owner.bind_sampler(name, std::vector<std::shared_ptr<Sampler>>{ sampler }));
				return *this;
			}

			IRenderProgramBinder& add_bind_sampler_array(const char* name, const std::vector<std::shared_ptr<Sampler>>& samplers) override
			{
				m_owner.record_binding_result(m_owner.bind_sampler(name, samplers));
				return *this;
			}

			IRenderProgramBinder& add_bind_acceleration_structure(const char* name, std::shared_ptr<AccelerationStructureView> acceleration_structure) override
			{
				m_owner.record_binding_result(m_owner.bind_acceleration_structure(name, acceleration_structure));
				return *this;
			}

			IRenderProgramBinder& set_const_data_block(uint32_t size, const void* data) override
			{
				m_owner.record_binding_result(m_owner.set_const_data_block_internal(size, data));
				return *this;
			}

			IRenderProgramBinder& set_immutable_const_value_int(const char* name, int32_t value) override
			{
				m_owner.record_binding_result(m_owner.set_immutable_const_int(name, value));
				return *this;
			}

			IRenderProgramBinder& set_immutable_const_value_uint(const char* name, uint32_t value) override
			{
				m_owner.record_binding_result(m_owner.set_immutable_const_uint(name, value));
				return *this;
			}

			IRenderProgramBinder& set_immutable_const_value_float(const char* name, float value) override
			{
				m_owner.record_binding_result(m_owner.set_immutable_const_float(name, value));
				return *this;
			}

			bool is_binding() const override
			{
				return m_owner.is_resource_binding();
			}

		private:
			VulkanRenderProgramBase& m_owner;
		};

		static bool descriptor_type_uses_image(AshDescriptorType descriptor_type)
		{
			return descriptor_type == ASH_DESCRIPTOR_TYPE_SAMPLER ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		}

		static bool descriptor_type_uses_buffer(AshDescriptorType descriptor_type)
		{
			return descriptor_type == ASH_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
				descriptor_type == ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		}

		static uint32_t get_max_reflected_set_index(const ParseResult& reflection)
		{
			uint32_t max_set_index = 0;
			for (uint32_t i = 0; i < reflection.set_count; ++i)
			{
				max_set_index = std::max(max_set_index, reflection.sets[i].set_index);
			}
			return max_set_index;
		}

		static uint32_t get_safe_current_frame_index()
		{
			uint32_t current_frame = VulkanContext::get_current_frame();
			if (current_frame == UINT32_MAX)
			{
				current_frame = 0;
			}
			return current_frame;
		}

		static uint64_t get_safe_absolute_frame_index()
		{
			uint64_t absolute_frame = VulkanContext::get_absolute_frame_count();
			if (absolute_frame == UINT64_MAX)
			{
				absolute_frame = 0;
			}
			return absolute_frame;
		}

		static VulkanProgramDescriptorSetFrameBucket& get_current_frame_bucket(VulkanProgramDescriptorSetState& descriptor_set_state)
		{
			if (descriptor_set_state.frame_buckets.size() != k_max_frames)
			{
				descriptor_set_state.frame_buckets.resize(k_max_frames);
			}

			const uint32_t frame_index = get_safe_current_frame_index() % static_cast<uint32_t>(descriptor_set_state.frame_buckets.size());
			VulkanProgramDescriptorSetFrameBucket& bucket = descriptor_set_state.frame_buckets[frame_index];
			const uint64_t absolute_frame = get_safe_absolute_frame_index();
			if (bucket.absolute_frame != absolute_frame)
			{
				bucket.descriptor_sets.clear();
				bucket.absolute_frame = absolute_frame;
			}
			return bucket;
		}

		static std::shared_ptr<VulkanDescriptorSet>& get_active_descriptor_set(VulkanProgramDescriptorSetState& descriptor_set_state)
		{
			H_ASSERT(descriptor_set_state.current_descriptor_set);
			return descriptor_set_state.current_descriptor_set;
		}
	}

	VulkanRenderProgramBase::VulkanRenderProgramBase() = default;
	VulkanRenderProgramBase::~VulkanRenderProgramBase() = default;
	VulkanGraphicsRenderProgram::VulkanGraphicsRenderProgram() = default;
	VulkanGraphicsRenderProgram::~VulkanGraphicsRenderProgram() = default;
	VulkanComputeRenderProgram::VulkanComputeRenderProgram() = default;
	VulkanComputeRenderProgram::~VulkanComputeRenderProgram() = default;

	void VulkanRenderProgramBase::record_binding_result(bool success)
	{
		m_binding_failed = !success || m_binding_failed;
	}

	bool VulkanRenderProgramBase::begin_resource_binding()
	{
		if (m_is_binding)
		{
			return true;
		}

		m_cached_sampler_views.clear();
		m_bound_resource_names.clear();
		m_binding_failed = false;
		for (auto& descriptor_set_state : m_descriptor_sets)
		{
			descriptor_set_state.current_descriptor_set.reset();
			if (!descriptor_set_state.has_bindings || !descriptor_set_state.layout)
			{
				continue;
			}

			auto& frame_bucket = get_current_frame_bucket(descriptor_set_state);
			auto descriptor_set = Ash_New_Shared<VulkanDescriptorSet>(
				reinterpret_cast<VkDescriptorSetLayout>(descriptor_set_state.layout->get_native_handle()),
				descriptor_set_state.layout->get_pool_container());
			descriptor_set_state.current_descriptor_set = descriptor_set;
			frame_bucket.descriptor_sets.push_back(descriptor_set);
			descriptor_set->begin_bind();
			descriptor_set->prepare_write_capacity(
				descriptor_set_state.image_descriptor_count,
				descriptor_set_state.buffer_descriptor_count,
				descriptor_set_state.write_count);
		}
		m_is_binding = true;
		return true;
	}

	bool VulkanRenderProgramBase::bind_uav(const char* name, const std::vector<std::shared_ptr<BufferView>>& uavs)
	{
		VulkanProgramBindingInfo binding_info{};
		bool should_bind = false;
		if (!validate_binding_name(name, binding_info, should_bind))
		{
			return false;
		}
		if (!should_bind)
		{
			return true;
		}
		if (!validate_binding_count(name, binding_info, static_cast<uint32_t>(uavs.size())))
		{
			return false;
		}

		auto& descriptor_set_state = m_descriptor_sets[binding_info.set_index];
		auto& descriptor_set = get_active_descriptor_set(descriptor_set_state);
		H_ASSERT(descriptor_set);
		descriptor_set->add_bind_uav_array(binding_info.binding, uavs, binding_info.descriptor_type);
		m_bound_resource_names.insert(name);
		return true;
	}

	bool VulkanRenderProgramBase::bind_uav(const char* name, const std::vector<std::shared_ptr<TextureView>>& uavs)
	{
		VulkanProgramBindingInfo binding_info{};
		bool should_bind = false;
		if (!validate_binding_name(name, binding_info, should_bind))
		{
			return false;
		}
		if (!should_bind)
		{
			return true;
		}
		if (!validate_binding_count(name, binding_info, static_cast<uint32_t>(uavs.size())))
		{
			return false;
		}

		auto& descriptor_set_state = m_descriptor_sets[binding_info.set_index];
		auto& descriptor_set = get_active_descriptor_set(descriptor_set_state);
		H_ASSERT(descriptor_set);
		descriptor_set->add_bind_uav_array(binding_info.binding, uavs, binding_info.descriptor_type);
		m_bound_resource_names.insert(name);
		return true;
	}

	bool VulkanRenderProgramBase::bind_srv(const char* name, const std::vector<std::shared_ptr<BufferView>>& srvs)
	{
		VulkanProgramBindingInfo binding_info{};
		bool should_bind = false;
		if (!validate_binding_name(name, binding_info, should_bind))
		{
			return false;
		}
		if (!should_bind)
		{
			return true;
		}
		if (!validate_binding_count(name, binding_info, static_cast<uint32_t>(srvs.size())))
		{
			return false;
		}

		auto& descriptor_set_state = m_descriptor_sets[binding_info.set_index];
		auto& descriptor_set = get_active_descriptor_set(descriptor_set_state);
		H_ASSERT(descriptor_set);
		descriptor_set->add_bind_srv_array(binding_info.binding, srvs, binding_info.descriptor_type);
		m_bound_resource_names.insert(name);
		return true;
	}

	bool VulkanRenderProgramBase::bind_srv(const char* name, const std::vector<std::shared_ptr<TextureView>>& srvs)
	{
		VulkanProgramBindingInfo binding_info{};
		bool should_bind = false;
		if (!validate_binding_name(name, binding_info, should_bind))
		{
			return false;
		}
		if (!should_bind)
		{
			return true;
		}
		if (!validate_binding_count(name, binding_info, static_cast<uint32_t>(srvs.size())))
		{
			return false;
		}

		auto& descriptor_set_state = m_descriptor_sets[binding_info.set_index];
		auto& descriptor_set = get_active_descriptor_set(descriptor_set_state);
		H_ASSERT(descriptor_set);
		descriptor_set->add_bind_srv_array(binding_info.binding, srvs, binding_info.descriptor_type);
		m_bound_resource_names.insert(name);
		return true;
	}

	bool VulkanRenderProgramBase::bind_cbv(const char* name, std::shared_ptr<BufferView> cbv)
	{
		VulkanProgramBindingInfo binding_info{};
		bool should_bind = false;
		if (!validate_binding_name(name, binding_info, should_bind))
		{
			return false;
		}
		if (!should_bind)
		{
			return true;
		}

		auto& descriptor_set_state = m_descriptor_sets[binding_info.set_index];
		auto& descriptor_set = get_active_descriptor_set(descriptor_set_state);
		H_ASSERT(descriptor_set);
		descriptor_set->add_bind_cbv(binding_info.binding, cbv);
		m_bound_resource_names.insert(name);
		return true;
	}

	bool VulkanRenderProgramBase::bind_sampler(const char* name, const std::vector<std::shared_ptr<Sampler>>& samplers)
	{
		VulkanProgramBindingInfo binding_info{};
		bool should_bind = false;
		if (!validate_binding_name(name, binding_info, should_bind))
		{
			return false;
		}
		if (!should_bind)
		{
			return true;
		}
		if (!validate_binding_count(name, binding_info, static_cast<uint32_t>(samplers.size())))
		{
			return false;
		}

		std::vector<std::shared_ptr<SamplerView>> sampler_views;
		sampler_views.reserve(samplers.size());
		for (const auto& sampler : samplers)
		{
			if (!sampler)
			{
				HLogError("Render program '{}' received a null sampler for binding '{}'.", m_debug_name.c_str(), name ? name : "<null>");
				return false;
			}

			auto sampler_view = Ash_New_Shared<VulkanSamplerView>(sampler->get_name(), sampler);
			m_cached_sampler_views.push_back(sampler_view);
			sampler_views.push_back(sampler_view);
		}

		auto& descriptor_set_state = m_descriptor_sets[binding_info.set_index];
		auto& descriptor_set = get_active_descriptor_set(descriptor_set_state);
		H_ASSERT(descriptor_set);
		descriptor_set->add_bind_sampler_array(binding_info.binding, sampler_views);
		m_bound_resource_names.insert(name);
		return true;
	}

	bool VulkanRenderProgramBase::bind_acceleration_structure(const char* name, std::shared_ptr<AccelerationStructureView> acceleration_structure)
	{
		HLogError("Render program '{}' does not yet implement acceleration structure binding for '{}'.", m_debug_name.c_str(), name ? name : "<null>");
		(void)acceleration_structure;
		return false;
	}

	bool VulkanRenderProgramBase::set_immutable_const_int(const char* name, int32_t value)
	{
		ConstantValue::Value constant_value{};
		constant_value.value_i = value;
		return store_immutable_constant(name, ConstantValue::Type::Type_i32, constant_value);
	}

	bool VulkanRenderProgramBase::set_immutable_const_uint(const char* name, uint32_t value)
	{
		ConstantValue::Value constant_value{};
		constant_value.value_u = value;
		return store_immutable_constant(name, ConstantValue::Type::Type_u32, constant_value);
	}

	bool VulkanRenderProgramBase::set_immutable_const_float(const char* name, float value)
	{
		ConstantValue::Value constant_value{};
		constant_value.value_f = value;
		return store_immutable_constant(name, ConstantValue::Type::Type_f32, constant_value);
	}

	bool VulkanRenderProgramBase::is_resource_binding() const
	{
		return m_is_binding;
	}

	bool VulkanRenderProgramBase::initialize_program(const PipelineCreation& creation, const char* debug_name)
	{
		m_debug_name = debug_name ? debug_name : "VulkanRenderProgram";
		m_pipeline_creation = creation;
		if (!build_reflection_from_pipeline(creation))
		{
			return false;
		}
		if (!rebuild_descriptor_sets(creation))
		{
			return false;
		}
		if (m_reflection.push_constants_stride > 0)
		{
			m_push_constant_data.assign(m_reflection.push_constants_stride, 0);
		}
		else
		{
			m_push_constant_data.clear();
		}
		return true;
	}

	bool VulkanRenderProgramBase::destroy_program()
	{
		m_is_binding = false;
		m_cached_sampler_views.clear();
		m_push_constant_data.clear();
		m_immutable_constants.clear();
		m_specialization_constant_types.clear();
		m_binding_infos.clear();
		m_descriptor_sets.clear();
		m_reflection = ParseResult{};
		m_pipeline.reset();
		m_binder.reset();
		m_pipeline_creation = PipelineCreation{};
		m_binding_failed = false;
		m_specialization_dirty = false;
		return true;
	}

	bool VulkanRenderProgramBase::refresh_pipeline(PipelineCreation& creation)
	{
		m_pipeline_creation = creation;
		m_pipeline_creation.name = creation.name ? creation.name : m_debug_name.c_str();
		m_pipeline = std::make_unique<VulkanPipeline>(m_pipeline_creation, &m_immutable_constants);
		if (!m_pipeline || !m_pipeline->is_valid())
		{
			HLogError("Failed to create Vulkan pipeline for render program '{}'.", m_debug_name.c_str());
			m_pipeline.reset();
			return false;
		}
		m_specialization_dirty = false;
		return true;
	}

	bool VulkanRenderProgramBase::end_bind_internal()
	{
		if (!m_is_binding)
		{
			return true;
		}

		if (m_specialization_dirty && !rebuild_pipeline_for_specialization())
		{
			return false;
		}

		if (m_binding_failed)
		{
			HLogError("Render program '{}' failed while building descriptor bindings.", m_debug_name.c_str());
			return false;
		}

		for (auto& descriptor_set_state : m_descriptor_sets)
		{
			if (descriptor_set_state.current_descriptor_set)
			{
				descriptor_set_state.current_descriptor_set->end_bind();
			}
		}
		m_cached_sampler_views.clear();
		for (const auto& [name, binding_info] : m_binding_infos)
		{
			if (m_bound_resource_names.find(name) == m_bound_resource_names.end())
			{
				HLogError(
					"Render program '{}' requires reflected resource '{}' (set={}, binding={}) but C++ did not bind it.",
					m_debug_name.c_str(),
					name.c_str(),
					binding_info.set_index,
					binding_info.binding);
				m_binding_failed = true;
			}
		}
		if (m_binding_failed)
		{
			HLogError("Render program '{}' failed while building descriptor bindings.", m_debug_name.c_str());
			return false;
		}

		m_is_binding = false;
		m_bound_resource_names.clear();
		return m_pipeline && m_pipeline->is_valid();
	}

	bool VulkanRenderProgramBase::apply_pipeline(std::shared_ptr<CommandBuffer> cb, const RenderState* render_state)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(cb);
		ASH_LOG_PROCESS_ERROR(m_pipeline);
		ASH_LOG_PROCESS_ERROR(m_pipeline->is_valid());

		if (m_specialization_dirty)
		{
			ASH_LOG_PROCESS_ERROR(rebuild_pipeline_for_specialization());
			ASH_LOG_PROCESS_ERROR(m_pipeline);
			ASH_LOG_PROCESS_ERROR(m_pipeline->is_valid());
		}

		auto vulkan_cb = std::dynamic_pointer_cast<VulkanCommandBuffer>(cb);
		ASH_LOG_PROCESS_ERROR(vulkan_cb);
		VkCommandBuffer vk_command_buffer = vulkan_cb->get_vkCommandBuffer();
		ASH_LOG_PROCESS_ERROR(vk_command_buffer);

		vkCmdBindPipeline(vk_command_buffer, m_pipeline->get_bind_point(), m_pipeline->get_native_handle());

		if (render_state)
		{
			const ViewportState* viewport_state = render_state->get_viewport_state();
			if (viewport_state && viewport_state->num_viewports > 0 && viewport_state->viewport)
			{
				cb->cmd_set_viewport(viewport_state->viewport[0]);
			}
			if (viewport_state && viewport_state->num_scissors > 0 && viewport_state->scissors)
			{
				cb->cmd_set_scissor(viewport_state->scissors[0]);
			}
		}

		for (uint32_t set_index = 0; set_index < m_descriptor_sets.size(); ++set_index)
		{
			auto& descriptor_set_state = m_descriptor_sets[set_index];
			if (!descriptor_set_state.current_descriptor_set)
			{
				continue;
			}

			VkDescriptorSet vk_descriptor_set = descriptor_set_state.current_descriptor_set->get_native_handle();
			if (vk_descriptor_set != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					vk_command_buffer,
					m_pipeline->get_bind_point(),
					m_pipeline->get_pipeline_layout(),
					set_index,
					1,
					&vk_descriptor_set,
					0,
					nullptr);
			}
		}

		if (m_reflection.push_constants_stride > 0 && !m_push_constant_data.empty())
		{
			vkCmdPushConstants(
				vk_command_buffer,
				m_pipeline->get_pipeline_layout(),
				ash_shader_stage_flags_to_vk(m_reflection.pipeline_info.active_stages),
				0,
				m_reflection.push_constants_stride,
				m_push_constant_data.data());
		}

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	bool VulkanRenderProgramBase::set_const_data_block_internal(uint32_t size, const void* data)
	{
		if (m_reflection.push_constants_stride == 0)
		{
			return size == 0 || data == nullptr;
		}
		if (size > m_reflection.push_constants_stride || (size > 0 && !data))
		{
			HLogError("Render program '{}' push-constant upload is invalid. size={}, max={}.", m_debug_name.c_str(), size, m_reflection.push_constants_stride);
			return false;
		}
		if (m_push_constant_data.size() != m_reflection.push_constants_stride)
		{
			m_push_constant_data.assign(m_reflection.push_constants_stride, 0);
		}
		if (size > 0)
		{
			memcpy(m_push_constant_data.data(), data, size);
			if (size < m_push_constant_data.size())
			{
				memset(m_push_constant_data.data() + size, 0, m_push_constant_data.size() - size);
			}
		}
		return true;
	}

	bool VulkanRenderProgramBase::build_reflection_from_pipeline(const PipelineCreation& creation)
	{
		std::vector<ParseResult> stage_reflections;
		stage_reflections.reserve(creation.shaders.stages_count);

		for (uint32_t i = 0; i < creation.shaders.stages_count; ++i)
		{
			const ShaderStage& stage = creation.shaders.stages[i];
			if (stage.shader)
			{
				auto vulkan_shader = std::dynamic_pointer_cast<VulkanShader>(stage.shader);
				if (!vulkan_shader)
				{
					HLogError("Render program '{}' received a non-Vulkan shader at stage {}.", m_debug_name.c_str(), static_cast<uint32_t>(stage.type));
					return false;
				}
				stage_reflections.push_back(vulkan_shader->get_reflection_data());
				continue;
			}

			if (!creation.shaders.spv_input || !stage.code || stage.code_size == 0)
			{
				HLogError("Render program '{}' requires reflected Vulkan shader input for stage {}.", m_debug_name.c_str(), static_cast<uint32_t>(stage.type));
				return false;
			}

			ParseResult reflection{};
			if (!parse_binary_spv(reinterpret_cast<const uint32_t*>(stage.code), stage.code_size / sizeof(uint32_t), stage.type, &reflection))
			{
				HLogError("Render program '{}' failed to reflect stage {}.", m_debug_name.c_str(), static_cast<uint32_t>(stage.type));
				return false;
			}
			stage_reflections.push_back(reflection);
		}

		if (stage_reflections.empty())
		{
			m_reflection = ParseResult{};
			m_specialization_constant_types.clear();
			return true;
		}

		if (!merge_parse_results(stage_reflections.data(), static_cast<uint32_t>(stage_reflections.size()), &m_reflection))
		{
			HLogError("Render program '{}' failed to merge shader reflection data.", m_debug_name.c_str());
			return false;
		}

		m_specialization_constant_types.clear();
		for (const ParseResult& stage_reflection : stage_reflections)
		{
			for (uint32_t i = 0; i < stage_reflection.specialization_constants_count; ++i)
			{
				const char* name = stage_reflection.specialization_names[i].name;
				if (!name || name[0] == '\0')
				{
					continue;
				}
				m_specialization_constant_types[name] = stage_reflection.specialization_constants[i].default_value.type;
			}
		}
		return true;
	}

	bool VulkanRenderProgramBase::rebuild_descriptor_sets(const PipelineCreation& creation)
	{
		m_binding_infos.clear();
		m_descriptor_sets.clear();

		if (m_reflection.set_count == 0)
		{
			return true;
		}

		m_descriptor_sets.resize(get_max_reflected_set_index(m_reflection) + 1);

		for (uint32_t i = 0; i < m_reflection.set_count; ++i)
		{
			const DescriptorSetLayoutCreation& reflected_layout = m_reflection.sets[i];
			auto& descriptor_set_state = m_descriptor_sets[reflected_layout.set_index];
			descriptor_set_state.layout_creation = reflected_layout;
			descriptor_set_state.image_descriptor_count = 0;
			descriptor_set_state.buffer_descriptor_count = 0;
			descriptor_set_state.write_count = 0;
			descriptor_set_state.has_bindings = reflected_layout.num_bindings > 0;
			descriptor_set_state.current_descriptor_set.reset();
			descriptor_set_state.frame_buckets.clear();

			if (reflected_layout.set_index < creation.num_active_layouts && creation.descriptor_set_layout[reflected_layout.set_index])
			{
				descriptor_set_state.layout = std::dynamic_pointer_cast<VulkanDescriptorSetLayout>(creation.descriptor_set_layout[reflected_layout.set_index]);
			}
			if (!descriptor_set_state.layout)
			{
				descriptor_set_state.layout = VulkanDescriptorSetLayout::create(reflected_layout);
			}
			if (!descriptor_set_state.layout)
			{
				HLogError("Render program '{}' failed to create descriptor set layout {}.", m_debug_name.c_str(), reflected_layout.set_index);
				return false;
			}
			if (descriptor_set_state.has_bindings)
			{
				descriptor_set_state.frame_buckets.resize(k_max_frames);
			}

			for (uint32_t binding_index = 0; binding_index < reflected_layout.num_bindings; ++binding_index)
			{
				const auto& binding = reflected_layout.bindings[binding_index];
				VulkanProgramBindingInfo binding_info{};
				binding_info.set_index = reflected_layout.set_index;
				binding_info.binding = binding.index;
				binding_info.descriptor_count = std::max<uint32_t>(binding.count, 1u);
				binding_info.descriptor_type = binding.type;
				binding_info.stage_flags = binding.stage_flags;
				m_binding_infos[binding.name] = binding_info;

				descriptor_set_state.write_count++;
				if (descriptor_type_uses_image(binding.type))
				{
					descriptor_set_state.image_descriptor_count += binding_info.descriptor_count;
				}
				if (descriptor_type_uses_buffer(binding.type))
				{
					descriptor_set_state.buffer_descriptor_count += binding_info.descriptor_count;
				}
			}
		}

		return true;
	}

	bool VulkanRenderProgramBase::validate_binding_name(const char* name, VulkanProgramBindingInfo& binding_info, bool& should_bind)
	{
		should_bind = false;
		if (!m_is_binding)
		{
			HLogError("Render program '{}' resource binding was attempted before begin_bind().", m_debug_name.c_str());
			return false;
		}
		if (!name)
		{
			HLogError("Render program '{}' received a null binding name.", m_debug_name.c_str());
			return false;
		}
		auto it = m_binding_infos.find(name);
		if (it == m_binding_infos.end())
		{
			return true;
		}
		binding_info = it->second;
		should_bind = true;
		return true;
	}

	bool VulkanRenderProgramBase::validate_binding_count(const char* name, const VulkanProgramBindingInfo& binding_info, uint32_t count) const
	{
		if (count == 0)
		{
			HLogError("Render program '{}' binding '{}' received an empty resource array.", m_debug_name.c_str(), name ? name : "<null>");
			return false;
		}
		if (count > binding_info.descriptor_count)
		{
			HLogError(
				"Render program '{}' binding '{}' exceeded reflected array size. provided={}, reflected={}.",
				m_debug_name.c_str(),
				name ? name : "<null>",
				count,
				binding_info.descriptor_count);
			return false;
		}
		return true;
	}

	bool VulkanRenderProgramBase::store_immutable_constant(const char* name, ConstantValue::Type type, ConstantValue::Value value)
	{
		if (!name)
		{
			return false;
		}
		auto type_it = m_specialization_constant_types.find(name);
		if (type_it == m_specialization_constant_types.end())
		{
			HLogError("Render program '{}' does not contain a reflected specialization constant named '{}'.", m_debug_name.c_str(), name);
			return false;
		}
		if (type_it->second != type)
		{
			HLogError("Render program '{}' specialization constant '{}' type mismatch.", m_debug_name.c_str(), name);
			return false;
		}
		ConstantValue constant_value{};
		constant_value.type = type;
		constant_value.value = value;

		auto it = m_immutable_constants.find(name);
		if (it != m_immutable_constants.end() &&
			it->second.type == constant_value.type &&
			memcmp(&it->second.value, &constant_value.value, sizeof(ConstantValue::Value)) == 0)
		{
			return true;
		}

		m_immutable_constants[name] = constant_value;
		m_specialization_dirty = true;
		return true;
	}

	bool VulkanGraphicsRenderProgram::create(const GraphicProgramCreateDesc& desc)
	{
		destroy();
		m_desc = desc;
		m_render_state.rasterization = desc.pipeline.rasterization;
		m_render_state.depth_stencil = desc.pipeline.depth_stencil;
		m_render_state.blend_state = desc.pipeline.blend_state;
		m_render_state.primitive_topology = desc.pipeline.primitiveTopology;
		if (desc.pipeline.viewport)
		{
			m_render_state.set_viewport_state(*desc.pipeline.viewport);
		}
		else
		{
			m_render_state.clear_viewport_state();
		}
		if (!initialize_program(desc.pipeline, desc.pipeline.name ? desc.pipeline.name : "VulkanGraphicsRenderProgram"))
		{
			return false;
		}
		return refresh_pipeline();
	}

	bool VulkanGraphicsRenderProgram::destroy()
	{
		return destroy_program();
	}

	bool VulkanGraphicsRenderProgram::apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall)
	{
		if (fnRenderStateDefineCall)
		{
			fnRenderStateDefineCall(&m_render_state);
			// Must rebuild the VkPipeline: initial create() used default depth_stencil from the desc (often all-off).
			// Engine applies depth/cull/blend via this callback after create (see RenderDevice::apply_program_state).
			if (!refresh_pipeline())
			{
				return false;
			}
		}
		return m_pipeline && m_pipeline->is_valid();
	}

	bool VulkanGraphicsRenderProgram::set_const_data_block(uint32_t size, const void* data)
	{
		return set_const_data_block_internal(size, data);
	}

	bool VulkanGraphicsRenderProgram::apply(std::shared_ptr<CommandBuffer> cb)
	{
		return apply_pipeline(cb, &m_render_state);
	}

	IRenderProgramBinder& VulkanGraphicsRenderProgram::begin_bind()
	{
		if (!m_binder)
		{
			m_binder = std::make_unique<VulkanRenderProgramBinder>(*this);
		}
		return m_binder->begin_bind();
	}

	bool VulkanGraphicsRenderProgram::end_bind()
	{
		return end_bind_internal();
	}

	bool VulkanGraphicsRenderProgram::refresh_pipeline()
	{
		m_desc.pipeline.rasterization = m_render_state.rasterization;
		m_desc.pipeline.depth_stencil = m_render_state.depth_stencil;
		m_desc.pipeline.blend_state = m_render_state.blend_state;
		m_desc.pipeline.primitiveTopology = m_render_state.primitive_topology;
		m_desc.pipeline.viewport = m_render_state.get_viewport_state();
		return VulkanRenderProgramBase::refresh_pipeline(m_desc.pipeline);
	}

	bool VulkanGraphicsRenderProgram::rebuild_pipeline_for_specialization()
	{
		return refresh_pipeline();
	}

	bool VulkanComputeRenderProgram::create(const ComputeProgramCreateDesc& desc)
	{
		destroy();
		m_desc = desc;
		if (!initialize_program(desc.pipeline, desc.pipeline.name ? desc.pipeline.name : "VulkanComputeRenderProgram"))
		{
			return false;
		}
		return VulkanRenderProgramBase::refresh_pipeline(m_desc.pipeline);
	}

	bool VulkanComputeRenderProgram::destroy()
	{
		return destroy_program();
	}

	bool VulkanComputeRenderProgram::set_const_data_block(uint32_t size, const void* data)
	{
		return set_const_data_block_internal(size, data);
	}

	bool VulkanComputeRenderProgram::apply(std::shared_ptr<CommandBuffer> cb)
	{
		return apply_pipeline(cb, nullptr);
	}

	IRenderProgramBinder& VulkanComputeRenderProgram::begin_bind()
	{
		if (!m_binder)
		{
			m_binder = std::make_unique<VulkanRenderProgramBinder>(*this);
		}
		return m_binder->begin_bind();
	}

	bool VulkanComputeRenderProgram::end_bind()
	{
		return end_bind_internal();
	}

	bool VulkanComputeRenderProgram::rebuild_pipeline_for_specialization()
	{
		return VulkanRenderProgramBase::refresh_pipeline(m_desc.pipeline);
	}
}
