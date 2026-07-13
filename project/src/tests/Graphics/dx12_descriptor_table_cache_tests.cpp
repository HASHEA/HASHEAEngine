#include "Graphics/DirectX12/DX12DescriptorHeap.h"
#include "doctest.h"

#include <initializer_list>
#include <unordered_map>
#include <vector>

namespace
{
	RHI::DX12DescriptorHandle make_handle(SIZE_T cpuAddress, uint64_t allocationSerial)
	{
		RHI::DX12DescriptorHandle handle{};
		handle.cpuHandle.ptr = cpuAddress;
		handle.allocationSerial = allocationSerial;
		return handle;
	}

	RHI::DX12DescriptorHeapManager::DescriptorTableCacheKey make_key(
		std::initializer_list<RHI::DX12DescriptorHandle> handles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		const std::vector<RHI::DX12DescriptorHandle> ownedHandles(handles);
		return RHI::DX12DescriptorHeapManager::DescriptorTableCacheKey::from_handles(
			heapType,
			ownedHandles.data(),
			static_cast<uint32_t>(ownedHandles.size()));
	}
}

TEST_CASE("DX12 descriptor table cache identity distinguishes allocation generations for reused CPU slots")
{
	const auto original = make_key({ make_handle(0x1000, 1) });
	const auto sameAllocation = make_key({ make_handle(0x1000, 1) });
	const auto reusedSlot = make_key({ make_handle(0x1000, 2) });
	const auto differentSlot = make_key({ make_handle(0x2000, 1) });

	CHECK(original == sameAllocation);
	CHECK_FALSE(original == reusedSlot);
	CHECK_FALSE(original == differentSlot);
}

TEST_CASE("DX12 descriptor table cache identity preserves element serial order count and heap type")
{
	const auto original = make_key({ make_handle(0x1000, 1), make_handle(0x2000, 2) });
	const auto changedElementSerial = make_key({ make_handle(0x1000, 1), make_handle(0x2000, 3) });
	const auto changedOrder = make_key({ make_handle(0x2000, 2), make_handle(0x1000, 1) });
	const auto changedCount = make_key({ make_handle(0x1000, 1) });
	const auto changedHeapType = make_key(
		{ make_handle(0x1000, 1), make_handle(0x2000, 2) },
		D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	CHECK_FALSE(original == changedElementSerial);
	CHECK_FALSE(original == changedOrder);
	CHECK_FALSE(original == changedCount);
	CHECK_FALSE(original == changedHeapType);
}

TEST_CASE("DX12 descriptor table cache identity separates overflow allocation generations during lookup")
{
	const auto original = make_key({
		make_handle(0x1000, 1),
		make_handle(0x2000, 2),
		make_handle(0x3000, 3),
		make_handle(0x4000, 4),
		make_handle(0x5000, 5),
		make_handle(0x6000, 6),
		make_handle(0x7000, 7),
		make_handle(0x8000, 8),
		make_handle(0x9000, 9),
	});
	const auto changedNinthSerial = make_key({
		make_handle(0x1000, 1),
		make_handle(0x2000, 2),
		make_handle(0x3000, 3),
		make_handle(0x4000, 4),
		make_handle(0x5000, 5),
		make_handle(0x6000, 6),
		make_handle(0x7000, 7),
		make_handle(0x8000, 8),
		make_handle(0x9000, 10),
	});
	std::unordered_map<
		RHI::DX12DescriptorHeapManager::DescriptorTableCacheKey,
		int,
		RHI::DX12DescriptorHeapManager::DescriptorTableCacheKeyHasher> cache{};
	const RHI::DX12DescriptorHeapManager::DescriptorTableCacheKeyHasher hasher{};
	cache.emplace(original, 1);

	CHECK_FALSE(original == changedNinthSerial);
	// Performance regression guard: overflow allocationSerial must reach the hash combine; this is not a general no-collision claim.
	CHECK(hasher(original) != hasher(changedNinthSerial));
	CHECK(cache.find(original) != cache.end());
	CHECK(cache.find(changedNinthSerial) == cache.end());
}
