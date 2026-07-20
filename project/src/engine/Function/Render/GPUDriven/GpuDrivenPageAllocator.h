#pragma once

#include "Base/hcore.h"
#include "Function/Render/GPUDriven/GpuDrivenTypes.h"

#include <cstdint>
#include <vector>

namespace AshEngine
{
	class GpuDrivenPageAllocator
	{
	public:
		ASH_API explicit GpuDrivenPageAllocator(uint32_t capacity, uint32_t initial_generation = 1u);

		ASH_API GpuDrivenPageHandle allocate();
		ASH_API bool retire(GpuDrivenPageHandle handle, uint64_t last_gpu_frame);
		ASH_API uint32_t collect_completed(uint64_t completed_frame);
		ASH_API uint32_t sealed_count() const;

	private:
		enum class SlotState : uint8_t
		{
			Available = 0,
			Allocated,
			Retired,
			Sealed
		};

		struct Slot
		{
			uint32_t generation = 1u;
			uint64_t last_gpu_frame = 0u;
			SlotState state = SlotState::Available;
		};

		std::vector<Slot> m_slots;
		uint32_t m_sealed_count = 0u;
	};
}
