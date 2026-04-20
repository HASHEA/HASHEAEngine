#pragma once

#include "Function/Asset/AssetData.h"
#include "Function/Render/VertexDecl.h"
#include "Graphics/VertexInputLayout.h"
#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace AshEngine
{
	inline auto make_mesh_vertex_input_layout() -> RHI::VertexInputCreation
	{
		static_assert(std::is_standard_layout_v<MeshVertex>, "MeshVertex must remain standard-layout.");
		static_assert(sizeof(MeshVertex) <= UINT16_MAX, "MeshVertex stride exceeds RHI vertex stream range.");

		constexpr std::array<RHI::VertexStreamDesc, 1> streams = {
			RHI::VertexStreamDesc{
				0,
				static_cast<uint16_t>(sizeof(MeshVertex)),
				RHI::AshVertexInputRate::PerVertex
			}
		};

		constexpr std::array<RHI::VertexAttributeDesc, 6> attributes = {
			RHI::VertexAttributeDesc{ 0, 0, static_cast<uint32_t>(offsetof(MeshVertex, position)), RHI::AshVertexComponentFormat::Float3, RHI::AshVertexSemantic::Position, 0, "POSITION" },
			RHI::VertexAttributeDesc{ 1, 0, static_cast<uint32_t>(offsetof(MeshVertex, normal)), RHI::AshVertexComponentFormat::Float3, RHI::AshVertexSemantic::Normal, 0, "NORMAL" },
			RHI::VertexAttributeDesc{ 2, 0, static_cast<uint32_t>(offsetof(MeshVertex, tangent)), RHI::AshVertexComponentFormat::Float4, RHI::AshVertexSemantic::Tangent, 0, "TANGENT" },
			RHI::VertexAttributeDesc{ 3, 0, static_cast<uint32_t>(offsetof(MeshVertex, uv0)), RHI::AshVertexComponentFormat::Float2, RHI::AshVertexSemantic::TexCoord0, 0, "TEXCOORD" },
			RHI::VertexAttributeDesc{ 4, 0, static_cast<uint32_t>(offsetof(MeshVertex, uv1)), RHI::AshVertexComponentFormat::Float2, RHI::AshVertexSemantic::TexCoord1, 1, "TEXCOORD" },
			RHI::VertexAttributeDesc{ 5, 0, static_cast<uint32_t>(offsetof(MeshVertex, color)), RHI::AshVertexComponentFormat::Float4, RHI::AshVertexSemantic::Color0, 0, "COLOR" },
		};

		static_assert(offsetof(MeshVertex, position) == 0, "MeshVertex position must stay at offset 0.");
		static_assert(offsetof(MeshVertex, normal) < sizeof(MeshVertex), "MeshVertex normal offset is invalid.");
		static_assert(offsetof(MeshVertex, tangent) < sizeof(MeshVertex), "MeshVertex tangent offset is invalid.");
		static_assert(offsetof(MeshVertex, uv0) < sizeof(MeshVertex), "MeshVertex uv0 offset is invalid.");
		static_assert(offsetof(MeshVertex, uv1) < sizeof(MeshVertex), "MeshVertex uv1 offset is invalid.");
		static_assert(offsetof(MeshVertex, color) < sizeof(MeshVertex), "MeshVertex color offset is invalid.");

		return RHI::make_vertex_input_layout(streams, attributes);
	}

	inline auto get_mesh_vertex_decl() -> const std::shared_ptr<const VertexDecl>&
	{
		static const std::shared_ptr<const VertexDecl> decl =
			create_vertex_decl("MeshVertex", make_mesh_vertex_input_layout());
		return decl;
	}
}
