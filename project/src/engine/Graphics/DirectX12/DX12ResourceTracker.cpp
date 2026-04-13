#include "DX12ResourceTracker.h"
#include "Base/hassert.h"

namespace RHI
{
	void DX12ResourceTracker::track_resource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState, uint32_t numSubresources)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		ResourceState rs;
		rs.wholeResourceState = initialState;
		rs.subresourceStates.resize(numSubresources, initialState);
		rs.perSubresource = false;
		m_resourceStates[resource] = std::move(rs);
	}

	void DX12ResourceTracker::untrack_resource(ID3D12Resource* resource)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_resourceStates.erase(resource);
	}

	D3D12_RESOURCE_STATES DX12ResourceTracker::get_state(ID3D12Resource* resource, uint32_t subresource) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_resourceStates.find(resource);
		if (it == m_resourceStates.end())
			return D3D12_RESOURCE_STATE_COMMON;

		const auto& rs = it->second;
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || !rs.perSubresource)
			return rs.wholeResourceState;

		if (subresource < rs.subresourceStates.size())
			return rs.subresourceStates[subresource];

		return rs.wholeResourceState;
	}

	void DX12ResourceTracker::set_state(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, uint32_t subresource)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_resourceStates.find(resource);
		if (it == m_resourceStates.end())
			return;

		auto& rs = it->second;
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			rs.wholeResourceState = state;
			for (auto& s : rs.subresourceStates)
				s = state;
			rs.perSubresource = false;
		}
		else
		{
			if (subresource < rs.subresourceStates.size())
			{
				rs.subresourceStates[subresource] = state;
				rs.perSubresource = true;
			}
		}
	}

	void DX12ResourceTracker::generate_barriers(ID3D12Resource* resource, D3D12_RESOURCE_STATES targetState,
	                                             std::vector<D3D12_RESOURCE_BARRIER>& barriers, uint32_t subresource)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_resourceStates.find(resource);
		if (it == m_resourceStates.end())
			return;

		auto& rs = it->second;

		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			if (rs.wholeResourceState != targetState)
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = resource;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = rs.wholeResourceState;
				barrier.Transition.StateAfter = targetState;
				barriers.push_back(barrier);

				rs.wholeResourceState = targetState;
				for (auto& s : rs.subresourceStates)
					s = targetState;
				rs.perSubresource = false;
			}
		}
		else if (subresource < rs.subresourceStates.size())
		{
			D3D12_RESOURCE_STATES currentState = rs.perSubresource ?
				rs.subresourceStates[subresource] : rs.wholeResourceState;

			if (currentState != targetState)
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = resource;
				barrier.Transition.Subresource = subresource;
				barrier.Transition.StateBefore = currentState;
				barrier.Transition.StateAfter = targetState;
				barriers.push_back(barrier);

				rs.subresourceStates[subresource] = targetState;
				rs.perSubresource = true;
			}
		}
	}
}
