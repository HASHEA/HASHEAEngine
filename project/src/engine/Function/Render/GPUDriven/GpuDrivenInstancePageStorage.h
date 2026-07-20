#pragma once

#include "Base/hcore.h"
#include "Function/Render/GPUDriven/GpuDrivenTypes.h"

#include <memory>

namespace AshEngine
{
	struct StorageBufferDesc;
	class StorageBuffer;

	struct GpuDrivenStorageBufferFactory
	{
		void* user_data = nullptr;
		std::shared_ptr<StorageBuffer>(*create)(void* user_data, const StorageBufferDesc& desc) = nullptr;
	};

	class GpuDrivenInstancePageStorage
	{
	public:
		ASH_API static bool create(
			const GpuDrivenInstancePageDesc& desc,
			GpuDrivenPageHandle handle,
			GpuDrivenStorageBufferFactory factory,
			GpuDrivenInstancePageStorage& out_storage,
			const char* name = nullptr);

		ASH_API bool is_valid() const;
		ASH_API GpuDrivenPageHandle handle() const;
		ASH_API const GpuDrivenInstancePageDesc& desc() const;
		ASH_API const std::shared_ptr<StorageBuffer>& buffer() const;

	private:
		GpuDrivenInstancePageDesc m_desc{};
		GpuDrivenPageHandle m_handle{};
		std::shared_ptr<StorageBuffer> m_buffer;
	};
}
