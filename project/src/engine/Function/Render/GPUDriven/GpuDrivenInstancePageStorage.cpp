#include "Function/Render/GPUDriven/GpuDrivenInstancePageStorage.h"
#include "Function/Render/RenderDevice.h"

#include <utility>

namespace AshEngine
{
	bool GpuDrivenInstancePageStorage::create(
		const GpuDrivenInstancePageDesc& desc,
		GpuDrivenPageHandle handle,
		GpuDrivenStorageBufferFactory factory,
		GpuDrivenInstancePageStorage& out_storage,
		const char* name)
	{
		const GpuDrivenInstancePageValidationResult validation =
			validate_gpu_driven_instance_page_desc(desc);
		if (!validation.valid || !handle.is_valid() || factory.create == nullptr)
		{
			return false;
		}

		StorageBufferDesc buffer_desc{};
		buffer_desc.size = validation.payload_byte_size;
		buffer_desc.stride = desc.instance_stride;
		buffer_desc.cpu_write = false;
		buffer_desc.indirect_args = false;
		buffer_desc.initial_data = nullptr;
		buffer_desc.name = name;

		std::shared_ptr<StorageBuffer> buffer = factory.create(factory.user_data, buffer_desc);
		if (buffer == nullptr)
		{
			return false;
		}

		GpuDrivenInstancePageStorage storage{};
		storage.m_desc = desc;
		storage.m_handle = handle;
		storage.m_buffer = std::move(buffer);
		out_storage = std::move(storage);
		return true;
	}

	bool GpuDrivenInstancePageStorage::is_valid() const
	{
		return m_handle.is_valid() && m_buffer != nullptr;
	}

	GpuDrivenPageHandle GpuDrivenInstancePageStorage::handle() const
	{
		return m_handle;
	}

	const GpuDrivenInstancePageDesc& GpuDrivenInstancePageStorage::desc() const
	{
		return m_desc;
	}

	const std::shared_ptr<StorageBuffer>& GpuDrivenInstancePageStorage::buffer() const
	{
		return m_buffer;
	}
}
