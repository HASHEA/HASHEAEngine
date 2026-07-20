#include "Function/Render/GPUDriven/GpuDrivenPageAllocator.h"

#include <limits>

namespace AshEngine
{
	GpuDrivenPageAllocator::GpuDrivenPageAllocator(uint32_t capacity, uint32_t initial_generation)
		: m_slots(capacity)
	{
		const uint32_t generation = initial_generation == 0u ? 1u : initial_generation;
		for (Slot& slot : m_slots)
		{
			slot.generation = generation;
		}
	}

	GpuDrivenPageHandle GpuDrivenPageAllocator::allocate()
	{
		for (uint32_t slot_index = 0u; slot_index < m_slots.size(); ++slot_index)
		{
			Slot& slot = m_slots[slot_index];
			if (slot.state != SlotState::Available)
			{
				continue;
			}

			slot.state = SlotState::Allocated;
			return { slot_index, slot.generation };
		}

		return {};
	}

	bool GpuDrivenPageAllocator::retire(GpuDrivenPageHandle handle, uint64_t last_gpu_frame)
	{
		if (!handle.is_valid() || handle.slot >= m_slots.size())
		{
			return false;
		}

		Slot& slot = m_slots[handle.slot];
		if (slot.state != SlotState::Allocated || slot.generation != handle.generation)
		{
			return false;
		}

		slot.last_gpu_frame = last_gpu_frame;
		slot.state = SlotState::Retired;
		return true;
	}

	uint32_t GpuDrivenPageAllocator::collect_completed(uint64_t completed_frame)
	{
		uint32_t reclaimed_count = 0u;
		for (Slot& slot : m_slots)
		{
			if (slot.state != SlotState::Retired || completed_frame < slot.last_gpu_frame)
			{
				continue;
			}

			if (slot.generation == std::numeric_limits<uint32_t>::max())
			{
				slot.state = SlotState::Sealed;
				++m_sealed_count;
				continue;
			}

			++slot.generation;
			slot.state = SlotState::Available;
			++reclaimed_count;
		}

		return reclaimed_count;
	}

	uint32_t GpuDrivenPageAllocator::sealed_count() const
	{
		return m_sealed_count;
	}
}
