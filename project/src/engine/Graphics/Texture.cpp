#include "Texture.h"
namespace RHI
{
	auto access_texture(const TextureHandle& texture) -> Texture*
	{
		return nullptr;
	}
	auto create_texture(const TextureCreation& textureCreation) -> TextureHandle
	{
		return TextureHandle();
	}
	auto get_texture_description(const TextureHandle& texture, TextureDescription& desc) -> void
	{
	}
	auto destroy_texture(const TextureHandle& texture) -> void
	{
	}
	auto Texture::get_texture_handle() -> const TextureHandle&
	{
		// TODO: 在此处插入 return 语句
		return handle;
	}
}