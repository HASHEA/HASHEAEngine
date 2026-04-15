#pragma once
#include "Base/hplatform.h"
#include "RHIResource.h"
namespace RHI
{
	struct TextureViewCreation 
	{
		AshResourceViewType view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UNKNOWN;
		AshResourceViewDimension                view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D;
		AshSubresourceRange              sub_resource{};
		AshFormat                       format = ASH_FORMAT_UNDEFINED;
		const char*                     name = nullptr;
	}; // struct TextureViewCreation

	class TextureView : public RHIView
	{
	public:
		TextureView() = default;
		virtual ~TextureView() {};
	public:
		/*virtual auto get_render_target_view() -> void* = 0;*/
		virtual auto get_parent_texture() -> std::shared_ptr<Texture> = 0;
		virtual auto get_view_dim() -> AshResourceViewDimension = 0;
		virtual auto get_view_format() -> AshFormat = 0;
		virtual auto get_subresource_range() -> const AshSubresourceRange & = 0;
		virtual auto get_view_type() -> AshResourceViewType = 0;
	};

	struct TextureCreation {

		void*								 initial_data = nullptr;
		uint16_t                             width = 1;
		uint16_t                             height = 1;
		uint16_t                             depth = 1;
		uint16_t                             array_layer_count = 1;
		uint8_t                              mip_level_count = 1;
		AshTextureCreateFlags                flags = 0;    // TextureFlags bitmasks
		AshFormat							 format = ASH_FORMAT_UNDEFINED;
		AshImageType						 type = AshImageType::Ash_Texture2D;
		AshResourceState					 initial_state = AshResourceState::Unknown;
		AshResourceAccessType				 memoryType = AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		AshTextureUsageFlags				 uUsageFlags = 0;
		AshSampleCountFlag					 eSampleCount = ASH_SAMPLE_COUNT_1_BIT;

		std::shared_ptr<Texture>			 alias = nullptr;
		const char*                          name = nullptr;
		bool								 use_optimized_clear_value = false;
		AshColorValue						 optimized_clear_color{};
		AshDepthStencilValue				 optimized_clear_depth_stencil{ 1.0f, 0 };
	}; // struct TextureCreation
	class Sampler;
	class ASH_API Texture : public RHIResource,  public std::enable_shared_from_this<Texture>
	{
	public:
		Texture() = default;
		virtual ~Texture() {}
	public:
		virtual auto get_desciption()const ->const TextureCreation& = 0;
		virtual auto get_alias_texture() -> std::shared_ptr<Texture> = 0;
		virtual auto get_default_rtv() -> std::shared_ptr<TextureView> = 0;
		virtual auto get_default_srv() -> std::shared_ptr<TextureView> = 0;
		virtual auto get_default_uav() -> std::shared_ptr<TextureView> = 0;
		virtual auto is_cube_map() -> bool = 0;
		virtual auto is_sparse() -> bool = 0;
		virtual auto get_format() -> AshFormat = 0;
		virtual auto is_render_target() -> bool = 0;
		virtual auto get_mip_maps_count() -> uint8_t = 0;
		virtual auto get_layer_count() -> uint16_t = 0;
		virtual auto get_depth() -> uint16_t = 0;
		virtual auto get_width() -> uint16_t = 0;
		virtual auto get_height() -> uint16_t = 0;
		virtual auto get_type() -> AshImageType = 0;
		virtual auto get_resource_state() -> AshResourceState = 0;
		virtual auto set_resource_state(AshResourceState state) -> void = 0;
	};
}
