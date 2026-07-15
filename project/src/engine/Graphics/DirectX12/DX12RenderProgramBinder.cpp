#include "DX12RenderProgramBinder.h"
#include "DX12BufferView.h"
#include "DX12TextureView.h"
#include "DX12Sampler.h"
#include "DX12DescriptorHeap.h"
#include <utility>

namespace RHI
{
	IRenderProgramBinder& DX12RenderProgramBinder::begin_bind()
	{
		clear();
		m_isBinding = true;
		return *this;
	}

	void DX12RenderProgramBinder::clear()
	{
		m_pendingBinds.clear();
		m_pendingSamplerBinds.clear();
		m_rootConstants.clear();
		m_isBinding = false;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_uav(const char* name, std::shared_ptr<BufferView> uav)
	{
		auto* dx12View = static_cast<DX12BufferView*>(uav.get());
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
		bind.descriptorHandle = dx12View->get_descriptor_handle();
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_uav(const char* name, std::shared_ptr<TextureView> uav)
	{
		auto* dx12View = static_cast<DX12TextureView*>(uav.get());
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
		bind.descriptorHandle = dx12View->get_descriptor_handle();
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& uavs)
	{
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
		bind.isArray = true;
		bind.descriptorHandles.reserve(uavs.size());
		for (auto& uav : uavs)
		{
			auto* dx12View = static_cast<DX12BufferView*>(uav.get());
			bind.descriptorHandles.push_back(dx12View->get_descriptor_handle());
		}
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& uavs)
	{
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
		bind.isArray = true;
		bind.descriptorHandles.reserve(uavs.size());
		for (auto& uav : uavs)
		{
			auto* dx12View = static_cast<DX12TextureView*>(uav.get());
			bind.descriptorHandles.push_back(dx12View->get_descriptor_handle());
		}
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_srv(const char* name, std::shared_ptr<BufferView> srv)
	{
		auto* dx12View = static_cast<DX12BufferView*>(srv.get());
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
		bind.descriptorHandle = dx12View->get_descriptor_handle();
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_srv(const char* name, std::shared_ptr<TextureView> srv)
	{
		auto* dx12View = static_cast<DX12TextureView*>(srv.get());
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
		bind.descriptorHandle = dx12View->get_descriptor_handle();
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& srvs)
	{
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
		bind.isArray = true;
		bind.descriptorHandles.reserve(srvs.size());
		for (auto& srv : srvs)
		{
			auto* dx12View = static_cast<DX12BufferView*>(srv.get());
			bind.descriptorHandles.push_back(dx12View->get_descriptor_handle());
		}
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& srvs)
	{
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
		bind.isArray = true;
		bind.descriptorHandles.reserve(srvs.size());
		for (auto& srv : srvs)
		{
			auto* dx12View = static_cast<DX12TextureView*>(srv.get());
			bind.descriptorHandles.push_back(dx12View->get_descriptor_handle());
		}
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_cbv(const char* name, std::shared_ptr<BufferView> cbv)
	{
		auto* dx12View = static_cast<DX12BufferView*>(cbv.get());
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_CBV;
		bind.descriptorHandle = dx12View->get_descriptor_handle();
		m_pendingBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_sampler(const char* name, std::shared_ptr<Sampler> sampler)
	{
		auto* dx12Sampler = static_cast<DX12Sampler*>(sampler.get());
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SAMPLER;
		bind.descriptorHandle = dx12Sampler->get_descriptor_handle();
		m_pendingSamplerBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_sampler_array(const char* name, const std::vector<std::shared_ptr<Sampler>>& samplers)
	{
		DX12PendingBind bind;
		bind.name = name;
		bind.viewType = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SAMPLER;
		bind.isArray = true;
		bind.descriptorHandles.reserve(samplers.size());
		for (auto& sampler : samplers)
		{
			auto* dx12Sampler = static_cast<DX12Sampler*>(sampler.get());
			bind.descriptorHandles.push_back(dx12Sampler->get_descriptor_handle());
		}
		m_pendingSamplerBinds.push_back(std::move(bind));
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::add_bind_acceleration_structure(const char* name, std::shared_ptr<AccelerationStructureView> acceleration_structure)
	{
		// TODO: Implement RT acceleration structure binding
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::set_const_data_block(uint32_t size, const void* data)
	{
		m_rootConstants.resize(size);
		if (size > 0 && data)
		{
			memcpy(m_rootConstants.data(), data, size);
		}
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::set_immutable_const_value_int(const char* name, int32_t value)
	{
		// DX12: immutable constants are handled as shader macros triggering recompilation
		// For runtime, we pass them as root constants
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::set_immutable_const_value_uint(const char* name, uint32_t value)
	{
		return *this;
	}

	IRenderProgramBinder& DX12RenderProgramBinder::set_immutable_const_value_float(const char* name, float value)
	{
		return *this;
	}
}
