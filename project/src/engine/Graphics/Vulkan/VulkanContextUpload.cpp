#include "VulkanContext.h"

#include "Base/hlog.h"
#include "Base/hthreading.h"
#include "Graphics/TextureUploadUtils.h"
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "VulkanTexture.h"

#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace RHI
{
	auto VulkanContext::queue_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			HLogError(
				"VulkanContext: queue_buffer_upload rejected request (buffer={}, data={}, size={}, offset={}).",
				buffer ? (buffer->get_name() ? buffer->get_name() : "UnnamedBuffer") : "<null>",
				data != nullptr,
				size,
				offset);
			return false;
		}

		if (!AshEngine::is_in_render_thread() || !frameActive)
		{
			const bool queued = _enqueue_pending_buffer_upload(buffer, offset, size, data);
			if (!queued)
			{
				HLogError(
					"VulkanContext: failed to enqueue pending buffer upload for '{}' (offset={}, size={}, in_render_thread={}, frameActive={}).",
					buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
					offset,
					size,
					AshEngine::is_in_render_thread(),
					frameActive);
			}
			return queued;
		}

		const bool recorded = _record_buffer_upload(buffer, offset, size, data);
		if (!recorded)
		{
			HLogError(
				"VulkanContext: live buffer upload failed for '{}' (offset={}, size={}, uploadCommandsPending={}, uploadCommandQueued={}, currentUploadCommandBuffer={}, currentFrame={}).",
				buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
				offset,
				size,
				uploadCommandsPending,
				uploadCommandQueued,
				currentUploadCommandBuffer != nullptr,
				currentFrame);
		}
		return recorded;
	}

	auto VulkanContext::queue_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}

		const auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
		const TextureCreation& creation = vulkanTexture->get_desciption();
		const AshTextureFormatInfo& formatInfo = get_vk_texture_format_info(creation.format);
		if (creation.eSampleCount != ASH_SAMPLE_COUNT_1_BIT ||
			vulkanTexture->is_sparse() ||
			formatInfo.vkFormat == VK_FORMAT_UNDEFINED ||
			TextureFormat::has_depth_or_stencil(formatInfo.vkFormat))
		{
			return false;
		}

		if (!AshEngine::is_in_render_thread() || !frameActive)
		{
			return _enqueue_pending_texture_upload(texture, data);
		}

		return _record_texture_upload(texture, data);
	}

	auto VulkanContext::_enqueue_pending_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			return false;
		}

		PendingBufferUpload upload{};
		upload.buffer = buffer;
		upload.offset = offset;
		upload.data.resize(size);
		std::memcpy(upload.data.data(), data, size);
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			pendingBufferUploads.push_back(std::move(upload));
		}
		return true;
	}

	auto VulkanContext::_enqueue_pending_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}

		const TextureCreation& creation = texture->get_desciption();
		const AshTextureFormatInfo& formatInfo = get_vk_texture_format_info(creation.format);
		TextureUploadFormatInfo uploadFormatInfo{};
		uploadFormatInfo.bytesPerBlock = formatInfo.uBytesPerBlock;
		uploadFormatInfo.widthPerBlock = formatInfo.uWidthPerBlock;
		uploadFormatInfo.heightPerBlock = formatInfo.uHeightPerBlock;

		std::vector<TextureUploadSubresource> subresources{};
		uint64_t totalBytes = 0;
		if (!build_tightly_packed_texture_upload_layout(creation, uploadFormatInfo, subresources, totalBytes) || totalBytes == 0 || totalBytes > UINT32_MAX)
		{
			return false;
		}

		PendingTextureUpload upload{};
		upload.texture = texture;
		upload.data.resize(static_cast<size_t>(totalBytes));
		std::memcpy(upload.data.data(), data, static_cast<size_t>(totalBytes));
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			pendingTextureUploads.push_back(std::move(upload));
		}
		return true;
	}

	auto VulkanContext::_ensure_upload_command_buffer_recording() -> bool
	{
		if (!frameActive)
		{
			HLogError("VulkanContext: requested live upload command buffer while frameActive=false.");
			return false;
		}

		const bool needNewCmd =
			!uploadCommandsPending ||
			!currentUploadCommandBuffer ||
			currentUploadCommandBuffer->get_state() != AshCommandBufferState::ASH_Recording;
		if (needNewCmd)
		{
			currentUploadCommandBuffer = static_cast<VulkanCommandBuffer*>(get_command_buffer(0));
			if (!currentUploadCommandBuffer)
			{
				HLogError("VulkanContext: failed to acquire upload command buffer for frame {}.", currentFrame);
				return false;
			}
			currentUploadCommandBuffer->begin_record();
			if (currentUploadCommandBuffer->has_error())
			{
				HLogError(
					"VulkanContext: failed to begin upload command buffer recording: {}",
					currentUploadCommandBuffer->get_last_error().empty() ? "<unknown>" : currentUploadCommandBuffer->get_last_error());
				return false;
			}
			uploadCommandsPending = true;
			uploadCommandQueued = false;
		}

		return currentUploadCommandBuffer != nullptr;
	}

	auto VulkanContext::_record_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			HLogError(
				"VulkanContext: _record_buffer_upload rejected request (buffer={}, data={}, size={}, offset={}).",
				buffer ? (buffer->get_name() ? buffer->get_name() : "UnnamedBuffer") : "<null>",
				data != nullptr,
				size,
				offset);
			return false;
		}
		if (!_ensure_upload_command_buffer_recording())
		{
			HLogError(
				"VulkanContext: failed to start live buffer upload for '{}' (offset={}, size={}).",
				buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
				offset,
				size);
			return false;
		}

		const bool updated = currentUploadCommandBuffer->cmd_update_sub_resource(buffer, offset, size, const_cast<void*>(data));
		if (!updated)
		{
			HLogError(
				"VulkanContext: cmd_update_sub_resource returned false for '{}' (offset={}, size={}, command_buffer_state={}).",
				buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
				offset,
				size,
				currentUploadCommandBuffer ? static_cast<uint32_t>(currentUploadCommandBuffer->get_state()) : UINT32_MAX);
		}
		return updated;
	}

	auto VulkanContext::_record_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}
		if (!_ensure_upload_command_buffer_recording())
		{
			return false;
		}

		return currentUploadCommandBuffer->cmd_update_texture_sub_resource(texture, data);
	}

	auto VulkanContext::_flush_pending_buffer_uploads() -> bool
	{
		std::vector<PendingBufferUpload> uploads{};
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			uploads.swap(pendingBufferUploads);
		}

		for (const PendingBufferUpload& upload : uploads)
		{
			if (!_record_buffer_upload(upload.buffer, upload.offset, static_cast<uint32_t>(upload.data.size()), upload.data.data()))
			{
				return false;
			}
		}

		return true;
	}

	auto VulkanContext::_flush_pending_texture_uploads() -> bool
	{
		std::vector<PendingTextureUpload> uploads{};
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			uploads.swap(pendingTextureUploads);
		}

		for (const PendingTextureUpload& upload : uploads)
		{
			if (!_record_texture_upload(upload.texture, upload.data.data()))
			{
				return false;
			}
		}

		return true;
	}

	auto VulkanContext::_finalize_upload_command_buffer() -> bool
	{
		if (!uploadCommandsPending || !currentUploadCommandBuffer)
		{
			return true;
		}
		if (currentUploadCommandBuffer->get_state() == AshCommandBufferState::ASH_Recording)
		{
			currentUploadCommandBuffer->end_record();
			if (currentUploadCommandBuffer->has_error())
			{
				HLogError(
					"VulkanContext: failed to end upload command buffer recording: {}",
					currentUploadCommandBuffer->get_last_error().empty() ? "<unknown>" : currentUploadCommandBuffer->get_last_error());
				return false;
			}
		}
		return !currentUploadCommandBuffer->has_error();
	}
}
