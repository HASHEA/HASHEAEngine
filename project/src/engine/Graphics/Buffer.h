#pragma once
#include "RHIResource.h"
#include "Base/hassert.h"
namespace RHI
{

	//flags that can be used to dynamic buffer
	static const AshBufferUsageFlags k_dynamic_buffer_mask = ASH_BUFFER_USAGE_VERTEX_BUFFER_BIT | ASH_BUFFER_USAGE_INDEX_BUFFER_BIT | ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	struct BufferCreation
	{
		AshBufferUsageFlags			usage_flags					= 0;
		AshResourceAccessType		access_type					= AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		uint32_t					size						= 0;
		uint32_t					struct_byte_stride = 0;
		bool						force_static = true;//force not dynamic
		void*						initial_data				= nullptr;
		const char*					name						= nullptr;

		static BufferCreation get_ubo_creation(uint32_t uByteWidth)
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
			bufDesc.struct_byte_stride = 0;
			bufDesc.force_static = true;
			return bufDesc;
		}
		//ubo that update multi-times
		static BufferCreation get_dynamic_write_ubo_creation(uint32_t uByteWidth)
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE;
			bufDesc.struct_byte_stride = 0;
			bufDesc.force_static = true;
			return bufDesc;
		}

		static BufferCreation get_gpu_rwbuffer_creation(uint32_t uByteWidth, AshBufferUsageFlags uUsageFlags = static_cast<AshBufferUsageFlagBits>(0))
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT | uUsageFlags;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
			bufDesc.struct_byte_stride = 0;
			bufDesc.force_static = true;
			return bufDesc;
		}

		static BufferCreation get_dynamic_gpu_rbuffer_creation(uint32_t uByteWidth, uint32_t InStructStride, AshBufferUsageFlags uUsageFlags = static_cast<AshBufferUsageFlagBits>(0))
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT | uUsageFlags;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE;
			bufDesc.struct_byte_stride = InStructStride;
			bufDesc.force_static = false;
			return bufDesc;
		}

		static BufferCreation get_cpuw_gpur_buffer_creation(uint32_t uByteWidth, AshBufferUsageFlags uUsageFlags = static_cast<AshBufferUsageFlagBits>(0))
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT | uUsageFlags;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE;
			bufDesc.struct_byte_stride = 0;
			bufDesc.force_static = true;
			return bufDesc;
		}
		static BufferCreation get_gpufmt_rwbuffer_creation(uint32_t uByteWidth, AshBufferUsageFlags uUsageFlags = static_cast<AshBufferUsageFlagBits>(0))
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | uUsageFlags;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
			bufDesc.struct_byte_stride = 0;
			bufDesc.force_static = true;
			return bufDesc;
		}

		static BufferCreation get_cpur_staging_buffer_creation(uint32_t uByteWidth, AshBufferUsageFlags uUsageFlags = static_cast<AshBufferUsageFlagBits>(0))
		{
			BufferCreation bufDesc;
			H_ASSERT(uByteWidth > 0);
			bufDesc.size = uByteWidth;
			bufDesc.usage_flags = AshBufferUsageFlagBits::ASH_BUFFER_USAGE_TRANSFER_DST_BIT | uUsageFlags;
			bufDesc.access_type = AshResourceAccessType::ASH_RESOURCE_ACCESS_READ;
			bufDesc.struct_byte_stride = 0;
			bufDesc.force_static = true;
			return bufDesc;
		}
	};
	class BufferView;
	class Buffer : public RHIResource, public std::enable_shared_from_this<Buffer>
	{
	public:
		Buffer() = default;
		virtual ~Buffer() {}

	public:
		virtual auto get_size() -> uint32_t = 0;
		virtual auto get_name() -> const char* = 0;
		virtual auto get_global_offset() -> uint32_t = 0;
		virtual auto is_ready() -> bool = 0;
		virtual auto get_mapped_data() -> uint8_t* = 0;
		virtual auto is_dynamic() -> bool = 0;
		virtual auto get_default_cbv() -> std::shared_ptr<BufferView> = 0;
		virtual auto get_default_srv() -> std::shared_ptr<BufferView> = 0;
		virtual auto get_default_uav() -> std::shared_ptr<BufferView> = 0;
		virtual auto update(uint32_t offset, uint32_t size, void* pData) -> bool = 0;
		virtual auto get_buffer_device_address() -> uint64_t = 0;
		virtual auto get_buffer_creation_info()const -> const BufferCreation& = 0;
	};
	enum AshBufferResourceViewFlagBit : uint32_t
	{
		ASH_BUFFER_RESOURCE_VIEW_FLAG_NONE = 0,
		ASH_BUFFER_RESOURCE_VIEW_FLAG_RAW = 1,
	};
	typedef uint32_t AshBufferResourceViewFlagBits;
	struct BufferViewCreation
	{
		AshResourceViewType view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UNKNOWN;
		AshFormat format = AshFormat::ASH_FORMAT_UNDEFINED;
		uint32_t uByteOffset = 0;
		uint32_t uByteRange = 0;
		uint32_t uStructureStride = 0;
		AshBufferResourceViewFlagBits Flags;

		bool operator==(const BufferViewCreation& other) const
		{
			return view_type == other.view_type && format == other.format &&
				uByteOffset == other.uByteOffset && uByteRange == other.uByteRange &&
				uStructureStride == other.uStructureStride && Flags == other.Flags;
		}
		static BufferViewCreation GetViewDesc_FullView(AshFormat eFmt)
		{
			BufferViewCreation viewDesc{};
			viewDesc.format = eFmt;
			viewDesc.uByteOffset = 0;
			viewDesc.uByteRange = 0;
			viewDesc.uStructureStride = 0;
			viewDesc.Flags = ASH_BUFFER_RESOURCE_VIEW_FLAG_NONE;
			return viewDesc;
		}
		static BufferViewCreation GetViewDesc_StructuredBuffer(uint32_t uStructureStride)
		{
			BufferViewCreation viewDesc{};
			viewDesc.format = ASH_FORMAT_UNDEFINED;
			viewDesc.uByteOffset = 0;
			viewDesc.uByteRange = 0;
			viewDesc.uStructureStride = uStructureStride;
			viewDesc.Flags = ASH_BUFFER_RESOURCE_VIEW_FLAG_NONE;
			return viewDesc;
		}
		static BufferViewCreation GetViewDesc_RawBuffer()
		{
			BufferViewCreation viewDesc;
			viewDesc.format = ASH_FORMAT_UNDEFINED;
			viewDesc.uByteOffset = 0;
			viewDesc.uByteRange = 0;
			viewDesc.uStructureStride = 0;
			viewDesc.Flags = ASH_BUFFER_RESOURCE_VIEW_FLAG_RAW;
			return viewDesc;
		}
	};
	class BufferView : public RHIView
	{
	public:
		BufferView() = default;
		virtual ~BufferView() {};
	public:
		/*virtual auto get_render_target_view() -> void* = 0;*/
		virtual auto get_parent_buffer() -> std::shared_ptr<Buffer> = 0;
		virtual auto get_view_type() -> AshResourceViewType = 0;
		virtual auto get_view_format() -> AshFormat = 0;
		virtual auto get_view_desc() -> const BufferViewCreation& = 0;
	};

}