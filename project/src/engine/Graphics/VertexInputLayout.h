#pragma once

#include "Pipeline.h"
#include "Base/hlog.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RHI
{
	struct VertexStreamDesc
	{
		uint16_t binding = 0;
		uint16_t stride = 0;
		AshVertexInputRate input_rate = AshVertexInputRate::PerVertex;
	};

	struct VertexAttributeDesc
	{
		uint16_t location = 0;
		uint16_t binding = 0;
		uint32_t offset = 0;
		AshVertexComponentFormat format = AshVertexComponentFormat::FormatCount;
		AshVertexSemantic semantic = AshVertexSemantic::Unspecified;
		uint16_t semantic_index = 0;
		const char* semantic_name = nullptr;
	};

	inline void copy_vertex_semantic_name(char (&dst)[32], const char* src)
	{
		dst[0] = '\0';
		if (!src || src[0] == '\0')
		{
			return;
		}

		const size_t copy_size = std::min<size_t>(sizeof(dst) - 1, std::strlen(src));
		if (copy_size > 0)
		{
			for (size_t index = 0; index < copy_size; ++index)
			{
				dst[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(src[index])));
			}
		}
		dst[copy_size] = '\0';
	}

	inline bool try_get_default_vertex_semantic_binding(
		AshVertexSemantic semantic,
		const char** out_name,
		uint16_t* out_index)
	{
		switch (semantic)
		{
		case AshVertexSemantic::Position:
			*out_name = "POSITION";
			*out_index = 0;
			return true;
		case AshVertexSemantic::Normal:
			*out_name = "NORMAL";
			*out_index = 0;
			return true;
		case AshVertexSemantic::Tangent:
			*out_name = "TANGENT";
			*out_index = 0;
			return true;
		case AshVertexSemantic::TexCoord0:
			*out_name = "TEXCOORD";
			*out_index = 0;
			return true;
		case AshVertexSemantic::TexCoord1:
			*out_name = "TEXCOORD";
			*out_index = 1;
			return true;
		case AshVertexSemantic::Color0:
			*out_name = "COLOR";
			*out_index = 0;
			return true;
		default:
			break;
		}

		return false;
	}

	template <size_t StreamCount, size_t AttributeCount>
	inline auto make_vertex_input_layout(
		const std::array<VertexStreamDesc, StreamCount>& streams,
		const std::array<VertexAttributeDesc, AttributeCount>& attributes) -> VertexInputCreation
	{
		static_assert(StreamCount <= k_max_vertex_streams, "Vertex stream count exceeds RHI limit.");
		static_assert(AttributeCount <= k_max_vertex_attributes, "Vertex attribute count exceeds RHI limit.");

		VertexInputCreation vertex_input{};
		for (const VertexStreamDesc& stream : streams)
		{
			vertex_input.add_vertex_stream({ stream.binding, stream.stride, stream.input_rate });
		}
		for (const VertexAttributeDesc& attribute : attributes)
		{
			vertex_input.add_vertex_attribute({
				attribute.location,
				attribute.binding,
				attribute.offset,
				attribute.format,
				attribute.semantic,
				attribute.semantic_index
			});
			copy_vertex_semantic_name(
				vertex_input.vertex_attributes[vertex_input.num_vertex_attributes - 1].semantic_name,
				attribute.semantic_name);
		}
		std::sort(
			vertex_input.vertex_attributes,
			vertex_input.vertex_attributes + vertex_input.num_vertex_attributes,
			[](const VertexAttribute& lhs, const VertexAttribute& rhs)
			{
				return lhs.location < rhs.location;
			});
		return vertex_input;
	}

	inline auto has_explicit_vertex_input(const VertexInputCreation& vertex_input) -> bool
	{
		return vertex_input.num_vertex_streams > 0 || vertex_input.num_vertex_attributes > 0;
	}

	inline auto validate_vertex_input_layout_basic(const VertexInputCreation& vertex_input, const char* debug_name) -> bool;

	inline auto vertex_input_layouts_equal(const VertexInputCreation& lhs, const VertexInputCreation& rhs) -> bool
	{
		if (lhs.num_vertex_streams != rhs.num_vertex_streams ||
			lhs.num_vertex_attributes != rhs.num_vertex_attributes)
		{
			return false;
		}

		for (uint32_t stream_index = 0; stream_index < lhs.num_vertex_streams; ++stream_index)
		{
			const VertexStream& lhs_stream = lhs.vertex_streams[stream_index];
			bool found_stream = false;
			for (uint32_t rhs_stream_index = 0; rhs_stream_index < rhs.num_vertex_streams; ++rhs_stream_index)
			{
				const VertexStream& rhs_stream = rhs.vertex_streams[rhs_stream_index];
				if (lhs_stream.binding == rhs_stream.binding)
				{
					found_stream =
						lhs_stream.stride == rhs_stream.stride &&
						lhs_stream.input_rate == rhs_stream.input_rate;
					break;
				}
			}
			if (!found_stream)
			{
				return false;
			}
		}

		for (uint32_t attribute_index = 0; attribute_index < lhs.num_vertex_attributes; ++attribute_index)
		{
			const VertexAttribute& lhs_attribute = lhs.vertex_attributes[attribute_index];
			bool found_attribute = false;
			for (uint32_t rhs_attribute_index = 0; rhs_attribute_index < rhs.num_vertex_attributes; ++rhs_attribute_index)
			{
				const VertexAttribute& rhs_attribute = rhs.vertex_attributes[rhs_attribute_index];
				if (lhs_attribute.location == rhs_attribute.location)
				{
					found_attribute =
						lhs_attribute.binding == rhs_attribute.binding &&
						lhs_attribute.offset == rhs_attribute.offset &&
						lhs_attribute.format == rhs_attribute.format &&
						lhs_attribute.semantic == rhs_attribute.semantic &&
						lhs_attribute.semantic_index == rhs_attribute.semantic_index &&
						std::strcmp(lhs_attribute.semantic_name, rhs_attribute.semantic_name) == 0;
					break;
				}
			}
			if (!found_attribute)
			{
				return false;
			}
		}

		return true;
	}

	inline auto validate_vertex_input_layout_basic(const VertexInputCreation& vertex_input, const char* debug_name) -> bool
	{
		const bool has_streams = vertex_input.num_vertex_streams > 0;
		const bool has_attributes = vertex_input.num_vertex_attributes > 0;
		if (has_streams != has_attributes)
		{
			HLogError(
				"Vertex input layout '{}' must define streams and attributes together.",
				debug_name ? debug_name : "<unnamed>");
			return false;
		}
		if (!has_streams)
		{
			return true;
		}

		for (uint32_t stream_index = 0; stream_index < vertex_input.num_vertex_streams; ++stream_index)
		{
			const VertexStream& stream = vertex_input.vertex_streams[stream_index];
			if (stream.stride == 0)
			{
				HLogError(
					"Vertex input layout '{}' stream binding {} has zero stride.",
					debug_name ? debug_name : "<unnamed>",
					stream.binding);
				return false;
			}
			for (uint32_t other_stream_index = stream_index + 1; other_stream_index < vertex_input.num_vertex_streams; ++other_stream_index)
			{
				if (vertex_input.vertex_streams[other_stream_index].binding == stream.binding)
				{
					HLogError(
						"Vertex input layout '{}' contains duplicate stream binding {}.",
						debug_name ? debug_name : "<unnamed>",
						stream.binding);
					return false;
				}
			}
		}

		for (uint32_t attribute_index = 0; attribute_index < vertex_input.num_vertex_attributes; ++attribute_index)
		{
			const VertexAttribute& attribute = vertex_input.vertex_attributes[attribute_index];
			if (attribute.format == AshVertexComponentFormat::FormatCount)
			{
				HLogError(
					"Vertex input layout '{}' attribute {} has invalid format.",
					debug_name ? debug_name : "<unnamed>",
					attribute_index);
				return false;
			}

			bool found_binding = false;
			for (uint32_t stream_index = 0; stream_index < vertex_input.num_vertex_streams; ++stream_index)
			{
				if (vertex_input.vertex_streams[stream_index].binding == attribute.binding)
				{
					found_binding = true;
					break;
				}
			}
			if (!found_binding)
			{
				HLogError(
					"Vertex input layout '{}' attribute location {} references missing binding {}.",
					debug_name ? debug_name : "<unnamed>",
					attribute.location,
					attribute.binding);
				return false;
			}

			for (uint32_t other_attribute_index = attribute_index + 1; other_attribute_index < vertex_input.num_vertex_attributes; ++other_attribute_index)
			{
				if (vertex_input.vertex_attributes[other_attribute_index].location == attribute.location)
				{
					HLogError(
						"Vertex input layout '{}' contains duplicate attribute location {}.",
						debug_name ? debug_name : "<unnamed>",
						attribute.location);
					return false;
				}
			}
		}

		return true;
	}

	inline auto validate_vertex_input_layout(const VertexInputCreation& vertex_input, const char* debug_name) -> bool
	{
		if (!validate_vertex_input_layout_basic(vertex_input, debug_name))
		{
			return false;
		}
		if (!has_explicit_vertex_input(vertex_input))
		{
			return true;
		}

		for (uint32_t attribute_index = 0; attribute_index < vertex_input.num_vertex_attributes; ++attribute_index)
		{
			const VertexAttribute& attribute = vertex_input.vertex_attributes[attribute_index];
			const bool has_named_semantic = attribute.semantic_name[0] != '\0';
			if (has_named_semantic && attribute.semantic != AshVertexSemantic::Unspecified)
			{
				const char* default_semantic_name = nullptr;
				uint16_t default_semantic_index = 0;
				if (!try_get_default_vertex_semantic_binding(
					attribute.semantic,
					&default_semantic_name,
					&default_semantic_index))
				{
					HLogError(
						"Vertex input layout '{}' attribute location {} uses an unsupported semantic enum.",
						debug_name ? debug_name : "<unnamed>",
						attribute.location);
					return false;
				}
				if (std::strcmp(default_semantic_name, attribute.semantic_name) != 0 ||
					default_semantic_index != attribute.semantic_index)
				{
					HLogError(
						"Vertex input layout '{}' attribute location {} provides inconsistent semantic enum/name binding.",
						debug_name ? debug_name : "<unnamed>",
						attribute.location);
					return false;
				}
			}
			if (!has_named_semantic && attribute.semantic == AshVertexSemantic::Unspecified)
			{
				HLogError(
					"Vertex input layout '{}' attribute location {} does not declare a semantic.",
					debug_name ? debug_name : "<unnamed>",
					attribute.location);
				return false;
			}

			for (uint32_t other_attribute_index = attribute_index + 1; other_attribute_index < vertex_input.num_vertex_attributes; ++other_attribute_index)
			{
				if (attribute.semantic != AshVertexSemantic::Unspecified &&
					vertex_input.vertex_attributes[other_attribute_index].semantic == attribute.semantic &&
					vertex_input.vertex_attributes[other_attribute_index].semantic_index == attribute.semantic_index)
				{
					HLogError(
						"Vertex input layout '{}' contains duplicate semantic enum/index pair on location {}.",
						debug_name ? debug_name : "<unnamed>",
						attribute.location);
					return false;
				}
				if (has_named_semantic &&
					std::strcmp(vertex_input.vertex_attributes[other_attribute_index].semantic_name, attribute.semantic_name) == 0 &&
					vertex_input.vertex_attributes[other_attribute_index].semantic_index == attribute.semantic_index)
				{
					HLogError(
						"Vertex input layout '{}' contains duplicate semantic name/index pair '{}' {}.",
						debug_name ? debug_name : "<unnamed>",
						attribute.semantic_name,
						attribute.semantic_index);
					return false;
				}
			}
		}

		return true;
	}
}
