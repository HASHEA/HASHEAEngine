#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include <cstddef>
#include <mutex>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	struct ASH_API DebugDrawLine
	{
		glm::vec3 start{ 0.0f };
		glm::vec3 end{ 0.0f };
		glm::vec4 color{ 1.0f };
		float thickness = 1.0f;
	};

	class ASH_API DebugDrawService
	{
	public:
		void draw_line(
			const glm::vec3& start,
			const glm::vec3& end,
			const glm::vec4& color,
			float thickness = 1.0f);
		void draw_box(
			const glm::vec3& minimum,
			const glm::vec3& maximum,
			const glm::vec4& color,
			float thickness = 1.0f);
		void draw_circle(
			const glm::vec3& center,
			const glm::vec3& normal,
			float radius,
			const glm::vec4& color,
			uint32_t segments = 32,
			float thickness = 1.0f);
		void draw_cone(
			const glm::vec3& apex,
			const glm::vec3& direction,
			float length,
			float angle_degrees,
			const glm::vec4& color,
			uint32_t segments = 32,
			float thickness = 1.0f);
		void draw_axes(
			const glm::mat4& transform,
			float length = 1.0f,
			float thickness = 1.0f);

		void snapshot_lines(std::vector<DebugDrawLine>& out_lines) const;
		void clear_frame();
		size_t get_line_count() const;
		bool empty() const;

	private:
		void append_line_locked(const DebugDrawLine& line);

	private:
		mutable std::mutex m_mutex{};
		std::vector<DebugDrawLine> m_lines{};
	};
}
