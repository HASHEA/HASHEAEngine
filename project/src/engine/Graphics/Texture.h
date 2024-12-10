#pragma once
#include "Base/hplatform.h"
#include "RHIResource.h"
namespace RHI
{
	struct TextureSubResource {

		uint16_t                             mip_base_level = 0;
		uint16_t                             mip_level_count = 1;
		uint16_t                             array_base_layer = 0;
		uint16_t                             array_layer_count = 1;

	}; // struct TextureSubResource
	struct TextureViewCreation {
		AshImageViewType                view_type = ASH_IMAGE_VIEW_TYPE_1D;
		TextureSubResource              sub_resource{};
		AshFormat                       format = ASH_FORMAT_UNDEFINED;
		const char*                     name = nullptr;
	}; // struct TextureViewCreation

	class TextureView : public RHIView
	{
	public:
		TextureView() = default;
		~TextureView() {};
	public:
		/*virtual auto get_render_target_view() -> void* = 0;*/
		virtual auto get_parent_texture() -> std::shared_ptr<Texture> = 0;
		virtual auto get_native_view_handle() -> void* = 0;
		virtual auto get_view_type() -> AshImageViewType = 0;
		virtual auto get_view_format() -> AshFormat = 0;
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

		void*								 initial_data = nullptr;
		uint16_t                             width = 1;
		uint16_t                             height = 1;
		uint16_t                             depth = 1;
		uint16_t                             array_layer_count = 1;
		uint8_t                              mip_level_count = 1;
		uint8_t                              flags = 0;    // TextureFlags bitmasks
		AshFormat							 format = ASH_FORMAT_UNDEFINED;
		AshImageType						 type = AshImageType::Ash_Texture2D;
		std::shared_ptr<Texture>			 alias = nullptr;
		const char*                          name = nullptr;
	}; // struct TextureCreation
	class Sampler;
	class ASH_API Texture : public RHIResource,  public std::enable_shared_from_this<Texture>
	{
	public:
		Texture() = default;
		virtual ~Texture() {}
	public:
		virtual auto get_desciption(TextureDescription& desc) -> void = 0;
		virtual auto get_native_texture_handle() -> void* = 0;
		virtual auto get_alias_texture() -> std::shared_ptr<Texture> = 0;
		virtual auto get_default_render_target_view() -> std::shared_ptr<TextureView> = 0;
		virtual auto get_default_shader_resource_view() -> std::shared_ptr<TextureView> = 0;
		virtual auto get_default_unordered_access_view() -> std::shared_ptr<TextureView> = 0;
		virtual auto get_default_sampler() -> std::shared_ptr<Sampler> = 0;
		virtual auto is_cube_map() -> bool = 0;
		virtual auto is_sparse() -> bool = 0;
		virtual auto get_format() -> AshFormat = 0;
		virtual auto is_render_target() -> bool = 0;
		virtual auto get_mip_maps_count() -> uint8_t = 0;
		virtual auto get_layer_count() -> uint16_t = 0;
		virtual auto get_depth() -> uint16_t = 0;
		virtual auto get_width() -> uint16_t = 0;
		virtual auto get_height() -> uint16_t = 0;
		virtual auto get_name() -> const char* = 0;
		virtual auto get_type() -> AshImageType = 0;
	};
}