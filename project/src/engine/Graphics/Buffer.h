#pragma once
#include "RHIResource.h"
namespace RHI
{
	typedef uint32_t AshBufferUsageFlags;
	//flags that can be used to dynamic buffer
	static const AshBufferUsageFlags k_dynamic_buffer_mask = ASH_BUFFER_USAGE_VERTEX_BUFFER_BIT | ASH_BUFFER_USAGE_INDEX_BUFFER_BIT | ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	struct MapBufferParameters {
		std::shared_ptr<Buffer>     buffer	= nullptr;
		/*uint32_t					offset	= 0;*/ // vma does not support offset mapping
		uint32_t					size	= 0;

	}; // struct MapBufferParameters
	struct BufferCreation
	{
		AshBufferUsageFlags			type_flags					= 0;
		AshResourceUsageType::Enum	usage						= AshResourceUsageType::Immutable;
		uint32_t					size						= 0;
		uint32_t					persistent					= 0;//hold a mappable pointer
		bool						device_only					= false;
		void*						initial_data				= nullptr;
		const char*					name						= nullptr;
	};
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
	};

}