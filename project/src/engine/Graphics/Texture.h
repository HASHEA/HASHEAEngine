#pragma once
#include "Base/hplatform.h"
#include "RHICommon.h"
namespace RHI
{

	struct TextureHandle {
		ResourceHandle                  index;
	};

	struct TextureViewHandle
	{
		ResourceHandle                  index;
	};
	struct TextureSubResource {

		uint16_t                             mip_base_level = 0;
		uint16_t                             mip_level_count = 1;
		uint16_t                             array_base_layer = 0;
		uint16_t                             array_layer_count = 1;

	}; // struct TextureSubResource
	struct TextureViewCreation {

		TextureHandle                   parent_texture = { k_invalid_resource };
		AshImageViewType                view_type = ASH_IMAGE_VIEW_TYPE_1D;
		TextureSubResource              sub_resource;
		const char*                     name = nullptr;
		TextureViewCreation& reset();
		TextureViewCreation& set_parent_texture(TextureHandle parent_texture);
		TextureViewCreation& set_mips(uint32_t base_mip, uint32_t mip_level_count);
		TextureViewCreation& set_array(uint32_t base_layer, uint32_t layer_count);
		TextureViewCreation& set_name(const char* name);
		TextureViewCreation& set_view_type(AshImageViewType view_type);

	}; // struct TextureViewCreation

	class TextureView
	{
	public:
		TextureView() = default;
		~TextureView() {};
		static auto create(TextureViewCreation& ci) -> TextureView*;
	private:

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
		AshImageType					type = AshImageType::Ash_Texture2D;
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
		AshImageType						 type = AshImageType::Ash_Texture2D;
		TextureHandle						 alias = { k_invalid_resource };
		const char*                          name = nullptr;
		TextureCreation& reset();
		TextureCreation& set_size(uint16_t width, uint16_t height, uint16_t depth);
		TextureCreation& set_flags(uint8_t flags);
		TextureCreation& set_mips(uint32_t mip_level_count);
		TextureCreation& set_layers(uint32_t layer_count);
		TextureCreation& set_format_type(AshFormat format, AshImageType type);
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
		static auto create(TextureCreation& ci) -> Texture*;
	public:
		virtual auto get_desciption(TextureDescription& desc) -> void = 0;
		virtual auto bind(uint32_t slot) -> void = 0;
		virtual auto unbind(uint32_t slot) -> void = 0;
		virtual auto get_sampler() -> Sampler* = 0;
	public:	
		virtual auto get_texture_handle() -> const TextureHandle&;
	protected:
		const char*						name = nullptr;
		uint16_t                        width = 1;
		uint16_t                        height = 1;
		uint16_t                        depth = 1;
		uint8_t                         mipmaps = 1;
		uint8_t                         render_target = 0;
		uint8_t                         compute_access = 0;
		AshFormat                       format = ASH_FORMAT_UNDEFINED;
		AshImageType					type = AshImageType::Ash_Texture2D;
		bool                            sparse = false;
		TextureHandle					handle;
		TextureHandle					aliasTexture;    //the handle of the alias texture with this 
	};

	auto access_texture(const TextureHandle& texture) -> Texture*;

	auto create_texture(const TextureCreation& textureCreation) -> TextureHandle;

	auto get_texture_description(const TextureHandle& texture, TextureDescription& desc) -> void;

	auto destroy_texture(const TextureHandle& texture) -> void;
}