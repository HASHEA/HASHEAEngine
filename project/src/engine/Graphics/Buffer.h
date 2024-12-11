#pragma once
#include "RHIResource.h"
namespace RHI
{
	struct BufferCreation
	{
		uint32_t					usage_flags					= 0;
		AshResourceUsageType::Enum	usage						= AshResourceUsageType::Immutable;
		uint32_t					size						= 0;
		uint32_t					persistent					= 0;
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
	};

}