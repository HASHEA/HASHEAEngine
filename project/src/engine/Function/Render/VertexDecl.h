#pragma once

#include "Base/hcore.h"
#include "Graphics/VertexInputLayout.h"
#include <memory>
#include <string>

namespace AshEngine
{
	class ASH_API VertexDecl
	{
	public:
		VertexDecl() = default;
		VertexDecl(std::string name, const RHI::VertexInputCreation& vertex_input)
			: m_name(std::move(name))
			, m_vertex_input(vertex_input)
		{
		}

	public:
		auto get_name() const -> const char*
		{
			return m_name.c_str();
		}

		auto get_vertex_input() const -> const RHI::VertexInputCreation&
		{
			return m_vertex_input;
		}

	private:
		std::string m_name{};
		RHI::VertexInputCreation m_vertex_input{};
	};

	inline auto create_vertex_decl(const char* name, const RHI::VertexInputCreation& vertex_input) -> std::shared_ptr<const VertexDecl>
	{
		const char* debug_name = name ? name : "<unnamed>";
		if (!RHI::validate_vertex_input_layout(vertex_input, debug_name))
		{
			return nullptr;
		}

		return std::make_shared<const VertexDecl>(debug_name, vertex_input);
	}
}
