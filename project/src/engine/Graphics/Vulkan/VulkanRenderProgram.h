#pragma once
#include "Graphics/RenderProgram.h"
#include "SpvHelper.h"
#include "VulkanWrapper.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace RHI
{
	class VulkanCommandBuffer;
	class VulkanDescriptorSet;
	class VulkanDescriptorSetLayout;
	class VulkanPipeline;

	struct VulkanProgramBindingInfo
	{
		uint32_t set_index = 0;
		uint32_t binding = 0;
		uint32_t descriptor_count = 1;
		AshDescriptorType descriptor_type = ASH_DESCRIPTOR_TYPE_MAX_ENUM;
		AshShaderStageFlagBits stage_flags = static_cast<AshShaderStageFlagBits>(0);
	};

	struct VulkanProgramDescriptorSetFrameBucket
	{
		std::vector<std::shared_ptr<VulkanDescriptorSet>> descriptor_sets;
		uint64_t absolute_frame = UINT64_MAX;
	};

	struct VulkanProgramDescriptorSetState
	{
		DescriptorSetLayoutCreation layout_creation{};
		std::shared_ptr<VulkanDescriptorSetLayout> layout = nullptr;
		std::vector<VulkanProgramDescriptorSetFrameBucket> frame_buckets;
		std::shared_ptr<VulkanDescriptorSet> current_descriptor_set = nullptr;
		uint32_t image_descriptor_count = 0;
		uint32_t buffer_descriptor_count = 0;
		uint32_t write_count = 0;
		bool has_bindings = false;
	};

	class VulkanRenderProgramBase
	{
	public:
		VulkanRenderProgramBase();
		virtual ~VulkanRenderProgramBase();

	public:
		void record_binding_result(bool success);
		bool begin_resource_binding();
		bool bind_uav(const char* name, const std::vector<std::shared_ptr<BufferView>>& uavs);
		bool bind_uav(const char* name, const std::vector<std::shared_ptr<TextureView>>& uavs);
		bool bind_srv(const char* name, const std::vector<std::shared_ptr<BufferView>>& srvs);
		bool bind_srv(const char* name, const std::vector<std::shared_ptr<TextureView>>& srvs);
		bool bind_cbv(const char* name, std::shared_ptr<BufferView> cbv);
		bool bind_sampler(const char* name, const std::vector<std::shared_ptr<Sampler>>& samplers);
		bool bind_acceleration_structure(const char* name, std::shared_ptr<AccelerationStructureView> acceleration_structure);
		bool set_const_data_block_internal(uint32_t size, const void* data);
		bool set_immutable_const_int(const char* name, int32_t value);
		bool set_immutable_const_uint(const char* name, uint32_t value);
		bool set_immutable_const_float(const char* name, float value);
		bool is_resource_binding() const;

	protected:
		bool initialize_program(const PipelineCreation& creation, const char* debug_name);
		bool destroy_program();
		bool refresh_pipeline(PipelineCreation& creation);
		bool end_bind_internal();
		bool apply_pipeline(std::shared_ptr<CommandBuffer> cb, const RenderState* render_state);
		virtual bool rebuild_pipeline_for_specialization() = 0;

	protected:
		std::string m_debug_name;
		PipelineCreation m_pipeline_creation{};
		ParseResult m_reflection{};
		std::unique_ptr<VulkanPipeline> m_pipeline = nullptr;
		std::vector<VulkanProgramDescriptorSetState> m_descriptor_sets;
		std::unordered_map<std::string, VulkanProgramBindingInfo> m_binding_infos;
		std::unordered_set<std::string> m_bound_resource_names;
		std::unordered_map<std::string, ConstantValue> m_immutable_constants;
		std::unordered_map<std::string, ConstantValue::Type> m_specialization_constant_types;
		std::vector<uint8_t> m_push_constant_data;
		std::vector<std::shared_ptr<SamplerView>> m_cached_sampler_views;
		std::unique_ptr<IRenderProgramBinder> m_binder = nullptr;
		bool m_is_binding = false;
		bool m_binding_failed = false;
		bool m_specialization_dirty = false;

	private:
		bool build_reflection_from_pipeline(const PipelineCreation& creation);
		bool rebuild_descriptor_sets(const PipelineCreation& creation);
		bool validate_binding_name(const char* name, VulkanProgramBindingInfo& binding_info, bool& should_bind);
		bool validate_binding_count(const char* name, const VulkanProgramBindingInfo& binding_info, uint32_t count) const;
		bool store_immutable_constant(const char* name, ConstantValue::Type type, ConstantValue::Value value);
	};

	class VulkanGraphicsRenderProgram : public IGraphicsRenderProgram, public VulkanRenderProgramBase
	{
	public:
		VulkanGraphicsRenderProgram();
		~VulkanGraphicsRenderProgram() override;

	public:
		bool create(const GraphicProgramCreateDesc& desc) override;
		bool destroy() override;
		bool apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall) override;
		bool set_const_data_block(uint32_t size, const void* data) override;
		bool apply(std::shared_ptr<CommandBuffer> cb) override;
		IRenderProgramBinder& begin_bind() override;
		bool end_bind() override;
		bool requires_resource_binding_commit_per_apply() const override;

	private:
		bool refresh_pipeline();
		bool rebuild_pipeline_for_specialization() override;

	private:
		GraphicProgramCreateDesc m_desc{};
		RenderState m_render_state{};
	};

	class VulkanComputeRenderProgram : public IComputeRenderProgram, public VulkanRenderProgramBase
	{
	public:
		VulkanComputeRenderProgram();
		~VulkanComputeRenderProgram() override;

	public:
		bool create(const ComputeProgramCreateDesc& desc) override;
		bool destroy() override;
		bool set_const_data_block(uint32_t size, const void* data) override;
		bool apply(std::shared_ptr<CommandBuffer> cb) override;
		IRenderProgramBinder& begin_bind() override;
		bool end_bind() override;

	private:
		bool rebuild_pipeline_for_specialization() override;

	private:
		ComputeProgramCreateDesc m_desc{};
	};
}
