#pragma once
#include "Base/hplatform.h"
#include "RHICommon.h"
namespace RHI
{
	struct TextureHandle {
		ResourceHandle                  index;
	};
	struct TextureDescription {

		void*							native_handle = nullptr;
		const char*                     name = nullptr;
		uint16_t                        width = 1;
		uint16_t                        height = 1;
		uint16_t                        depth = 1;
		uint8_t                         mipmaps = 1;
		uint8_t                         render_target = 0;
		uint8_t                         compute_access = 0;
		AshFormat                       format = ASH_FORMAT_UNDEFINED;
		AshTextureType					type = AshTextureType::Ash_Texture2D;
	}; // struct TextureDescription

	struct TextureCreation {

		void* initial_data = nullptr;
		uint16_t                             width = 1;
		uint16_t                             height = 1;
		uint16_t                             depth = 1;
		uint16_t                             array_layer_count = 1;
		uint8_t                              mip_level_count = 1;
		uint8_t                              flags = 0;    // TextureFlags bitmasks
		AshFormat							 format = ASH_FORMAT_UNDEFINED;
		AshTextureType						 type = AshTextureType::Ash_Texture2D;
		ResourceHandle						 alias = k_invalid_resource;
		const char*                          name = nullptr;
		TextureCreation& reset();
		TextureCreation& set_size(uint16_t width, uint16_t height, uint16_t depth);
		TextureCreation& set_flags(uint8_t flags);
		TextureCreation& set_mips(uint32_t mip_level_count);
		TextureCreation& set_layers(uint32_t layer_count);
		TextureCreation& set_format_type(AshFormat format, AshTextureType type);
		TextureCreation& set_name(const char* name);
		TextureCreation& set_data(void* data);
		TextureCreation& set_alias(uint32_t alias);
	}; // struct TextureCreation
	class Sampler;
	class ASH_API Texture
	{
	public:
		Texture() = default;
		virtual ~Texture() {}
	public:
		virtual auto get_desciption(TextureDescription& desc) -> void = 0;
		virtual auto bind(uint32_t slot) -> void = 0;
		virtual auto unbind(uint32_t slot) -> void = 0;
	protected:
		void*							native_handle = nullptr;
		const char*						name = nullptr;
		uint16_t                        width = 1;
		uint16_t                        height = 1;
		uint16_t                        depth = 1;
		uint8_t                         mipmaps = 1;
		uint8_t                         render_target = 0;
		uint8_t                         compute_access = 0;
		AshFormat                       format = ASH_FORMAT_UNDEFINED;
		AshTextureType					type = AshTextureType::Ash_Texture2D;
		bool                            sparse = false;
		Sampler* sampler = nullptr;
		ResourceHandle					handle;
		ResourceHandle					parentTexture;
		ResourceHandle					aliasTexture;
	};

}