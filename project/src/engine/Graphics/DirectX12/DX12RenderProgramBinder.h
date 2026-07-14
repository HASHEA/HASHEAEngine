#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/RenderProgram.h"
#include <unordered_map>
#include <string>

namespace RHI
{
	class DX12DescriptorHeapManager;

	// Pending bind entry
	struct DX12PendingBind
	{
		std::string name;
		AshResourceViewType viewType;
		DX12DescriptorHandle descriptorHandle = {};
		std::vector<DX12DescriptorHandle> descriptorHandles; // for arrays
		bool isArray = false;
	};

	class DX12RenderProgramBinder : public IRenderProgramBinder
	{
	public:
		DX12RenderProgramBinder() = default;
		~DX12RenderProgramBinder() = default;

		void set_heap_manager(DX12DescriptorHeapManager* heapMgr) { m_heapMgr = heapMgr; }

		IRenderProgramBinder& begin_bind() override;
		IRenderProgramBinder& add_bind_uav(const char* name, std::shared_ptr<BufferView> uav) override;
		IRenderProgramBinder& add_bind_uav(const char* name, std::shared_ptr<TextureView> uav) override;
		IRenderProgramBinder& add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& uavs) override;
		IRenderProgramBinder& add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& uavs) override;
		IRenderProgramBinder& add_bind_srv(const char* name, std::shared_ptr<BufferView> srv) override;
		IRenderProgramBinder& add_bind_srv(const char* name, std::shared_ptr<TextureView> srv) override;
		IRenderProgramBinder& add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& srvs) override;
		IRenderProgramBinder& add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& srvs) override;
		IRenderProgramBinder& add_bind_cbv(const char* name, std::shared_ptr<BufferView> cbv) override;
		IRenderProgramBinder& add_bind_sampler(const char* name, std::shared_ptr<Sampler> sampler) override;
		IRenderProgramBinder& add_bind_sampler_array(const char* name, const std::vector<std::shared_ptr<Sampler>>& samplers) override;
		IRenderProgramBinder& add_bind_acceleration_structure(const char* name, std::shared_ptr<AccelerationStructureView> acceleration_structure) override;
		IRenderProgramBinder& set_const_data_block(uint32_t size, const void* data) override;
		IRenderProgramBinder& set_immutable_const_value_int(const char* name, int32_t value) override;
		IRenderProgramBinder& set_immutable_const_value_uint(const char* name, uint32_t value) override;
		IRenderProgramBinder& set_immutable_const_value_float(const char* name, float value) override;
		bool is_binding() const override { return m_isBinding; }

		const std::vector<DX12PendingBind>& get_pending_binds() const { return m_pendingBinds; }
		const std::vector<DX12PendingBind>& get_pending_sampler_binds() const { return m_pendingSamplerBinds; }
		const std::vector<uint8_t>& get_root_constants() const { return m_rootConstants; }
		bool has_root_constants() const { return !m_rootConstants.empty(); }

		void clear();
		void finish_binding() { m_isBinding = false; }

	private:
		std::vector<DX12PendingBind> m_pendingBinds;
		std::vector<DX12PendingBind> m_pendingSamplerBinds;
		std::vector<uint8_t> m_rootConstants;
		DX12DescriptorHeapManager* m_heapMgr = nullptr;
		bool m_isBinding = false;
	};
}
