#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/RHIResource.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace RHI
{
	class DX12ResourceTracker
	{
	public:
		DX12ResourceTracker() = default;
		~DX12ResourceTracker() = default;

		void track_resource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState, uint32_t numSubresources = 1);
		void untrack_resource(ID3D12Resource* resource);
		bool is_tracked(ID3D12Resource* resource) const;

		D3D12_RESOURCE_STATES get_state(ID3D12Resource* resource, uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const;
		void set_state(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

		// Generate barriers needed to transition resources
		void generate_barriers(ID3D12Resource* resource, D3D12_RESOURCE_STATES targetState,
		                       std::vector<D3D12_RESOURCE_BARRIER>& barriers,
		                       uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	private:
		struct ResourceState
		{
			D3D12_RESOURCE_STATES wholeResourceState = D3D12_RESOURCE_STATE_COMMON;
			std::vector<D3D12_RESOURCE_STATES> subresourceStates;
			bool perSubresource = false;
		};

		mutable std::mutex m_mutex;
		std::unordered_map<ID3D12Resource*, ResourceState> m_resourceStates;
	};
}
