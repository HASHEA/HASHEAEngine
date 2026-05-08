#include "DX12ResourceTracker.h"
#include "Base/hassert.h"

namespace RHI
{
	void DX12ResourceTracker::track_resource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState, uint32_t numSubresources)
	{
		if (!resource)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		ResourceState rs;
		rs.wholeResourceState = initialState;
		rs.subresourceStates.resize(numSubresources > 0 ? numSubresources : 1, initialState);
		rs.perSubresource = false;
		m_resourceStates[resource] = std::move(rs);
	}

	void DX12ResourceTracker::untrack_resource(ID3D12Resource* resource)
	{
		if (!resource)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		m_resourceStates.erase(resource);
	}

	bool DX12ResourceTracker::is_tracked(ID3D12Resource* resource) const
	{
		if (!resource)
		{
			return false;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		return m_resourceStates.find(resource) != m_resourceStates.end();
	}

	D3D12_RESOURCE_STATES DX12ResourceTracker::get_state(ID3D12Resource* resource, uint32_t subresource) const
	{
		if (!resource)
		{
			return D3D12_RESOURCE_STATE_COMMON;
		}

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
		if (!resource)
		{
			return;
		}

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
				bool uniform = true;
				for (const auto trackedState : rs.subresourceStates)
				{
					if (trackedState != rs.subresourceStates[0])
					{
						uniform = false;
						break;
					}
				}
				if (uniform)
				{
					rs.wholeResourceState = rs.subresourceStates[0];
					rs.perSubresource = false;
				}
			}
		}
	}

	void DX12ResourceTracker::generate_barriers(ID3D12Resource* resource, D3D12_RESOURCE_STATES targetState,
	                                             std::vector<D3D12_RESOURCE_BARRIER>& barriers, uint32_t subresource)
	{
		if (!resource)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_resourceStates.find(resource);
		if (it == m_resourceStates.end())
			return;

		auto& rs = it->second;

		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			if (rs.perSubresource)
			{
				for (uint32_t subresourceIndex = 0; subresourceIndex < rs.subresourceStates.size(); ++subresourceIndex)
				{
					const D3D12_RESOURCE_STATES currentState = rs.subresourceStates[subresourceIndex];
					if (currentState == targetState)
					{
						continue;
					}

					D3D12_RESOURCE_BARRIER barrier = {};
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					barrier.Transition.pResource = resource;
					barrier.Transition.Subresource = subresourceIndex;
					barrier.Transition.StateBefore = currentState;
					barrier.Transition.StateAfter = targetState;
					barriers.push_back(barrier);

					rs.subresourceStates[subresourceIndex] = targetState;
				}

				rs.wholeResourceState = targetState;
				rs.perSubresource = false;
			}
			else if (rs.wholeResourceState != targetState)
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
				bool uniform = true;
				for (const auto trackedState : rs.subresourceStates)
				{
					if (trackedState != rs.subresourceStates[0])
					{
						uniform = false;
						break;
					}
				}
				if (uniform)
				{
					rs.wholeResourceState = rs.subresourceStates[0];
					rs.perSubresource = false;
				}
			}
		}
	}
}
