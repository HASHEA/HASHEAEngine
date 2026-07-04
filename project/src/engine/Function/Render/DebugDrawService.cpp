#include "Function/Render/DebugDrawService.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace AshEngine
{
	namespace
	{
		static auto normalize_or_fallback(const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
		{
			const float length = glm::length(value);
			return length > 0.0001f ? value / length : fallback;
		}

		static auto make_basis_from_normal(const glm::vec3& normal, glm::vec3& out_tangent, glm::vec3& out_bitangent) -> void
		{
			const glm::vec3 n = normalize_or_fallback(normal, glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::vec3 seed =
				std::abs(glm::dot(n, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f ?
				glm::vec3(1.0f, 0.0f, 0.0f) :
				glm::vec3(0.0f, 1.0f, 0.0f);
			out_tangent = normalize_or_fallback(glm::cross(seed, n), glm::vec3(1.0f, 0.0f, 0.0f));
			out_bitangent = normalize_or_fallback(glm::cross(n, out_tangent), glm::vec3(0.0f, 0.0f, 1.0f));
		}

		static auto transform_point(const glm::mat4& transform, const glm::vec3& point) -> glm::vec3
		{
			return glm::vec3(transform * glm::vec4(point, 1.0f));
		}
	}

	void DebugDrawService::draw_line(
		const glm::vec3& start,
		const glm::vec3& end,
		const glm::vec4& color,
		float thickness)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		DebugDrawLine line{};
		line.start = start;
		line.end = end;
		line.color = color;
		line.thickness = std::max(thickness, 1.0f);
		append_line_locked(line);
	}

	void DebugDrawService::draw_box(
		const glm::vec3& minimum,
		const glm::vec3& maximum,
		const glm::vec4& color,
		float thickness)
	{
		const glm::vec3 min = glm::min(minimum, maximum);
		const glm::vec3 max = glm::max(minimum, maximum);
		const glm::vec3 corners[] = {
			{ min.x, min.y, min.z },
			{ max.x, min.y, min.z },
			{ max.x, max.y, min.z },
			{ min.x, max.y, min.z },
			{ min.x, min.y, max.z },
			{ max.x, min.y, max.z },
			{ max.x, max.y, max.z },
			{ min.x, max.y, max.z }
		};
		const uint8_t edges[][2] = {
			{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
			{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
		};

		std::scoped_lock<std::mutex> lock(m_mutex);
		for (const auto& edge : edges)
		{
			DebugDrawLine line{};
			line.start = corners[edge[0]];
			line.end = corners[edge[1]];
			line.color = color;
			line.thickness = std::max(thickness, 1.0f);
			append_line_locked(line);
		}
	}

	void DebugDrawService::draw_circle(
		const glm::vec3& center,
		const glm::vec3& normal,
		float radius,
		const glm::vec4& color,
		uint32_t segments,
		float thickness)
	{
		if (radius <= 0.0f)
		{
			return;
		}

		segments = std::max<uint32_t>(segments, 3u);
		glm::vec3 tangent{};
		glm::vec3 bitangent{};
		make_basis_from_normal(normal, tangent, bitangent);

		std::scoped_lock<std::mutex> lock(m_mutex);
		for (uint32_t index = 0; index < segments; ++index)
		{
			const float a0 = (static_cast<float>(index) / static_cast<float>(segments)) * glm::two_pi<float>();
			const float a1 = (static_cast<float>(index + 1u) / static_cast<float>(segments)) * glm::two_pi<float>();
			DebugDrawLine line{};
			line.start = center + radius * (std::cos(a0) * tangent + std::sin(a0) * bitangent);
			line.end = center + radius * (std::cos(a1) * tangent + std::sin(a1) * bitangent);
			line.color = color;
			line.thickness = std::max(thickness, 1.0f);
			append_line_locked(line);
		}
	}

	void DebugDrawService::draw_cone(
		const glm::vec3& apex,
		const glm::vec3& direction,
		float length,
		float angle_degrees,
		const glm::vec4& color,
		uint32_t segments,
		float thickness)
	{
		if (length <= 0.0f)
		{
			return;
		}

		segments = std::max<uint32_t>(segments, 3u);
		const glm::vec3 axis = normalize_or_fallback(direction, glm::vec3(0.0f, 0.0f, 1.0f));
		glm::vec3 tangent{};
		glm::vec3 bitangent{};
		make_basis_from_normal(axis, tangent, bitangent);

		const float clamped_angle = std::clamp(angle_degrees, 0.1f, 89.0f);
		const float radius = std::tan(glm::radians(clamped_angle)) * length;
		const glm::vec3 base_center = apex + axis * length;

		std::scoped_lock<std::mutex> lock(m_mutex);
		for (uint32_t index = 0; index < segments; ++index)
		{
			const float a0 = (static_cast<float>(index) / static_cast<float>(segments)) * glm::two_pi<float>();
			const float a1 = (static_cast<float>(index + 1u) / static_cast<float>(segments)) * glm::two_pi<float>();
			const glm::vec3 p0 = base_center + radius * (std::cos(a0) * tangent + std::sin(a0) * bitangent);
			const glm::vec3 p1 = base_center + radius * (std::cos(a1) * tangent + std::sin(a1) * bitangent);

			DebugDrawLine side{};
			side.start = apex;
			side.end = p0;
			side.color = color;
			side.thickness = std::max(thickness, 1.0f);
			append_line_locked(side);

			DebugDrawLine ring{};
			ring.start = p0;
			ring.end = p1;
			ring.color = color;
			ring.thickness = std::max(thickness, 1.0f);
			append_line_locked(ring);
		}
	}

	void DebugDrawService::draw_axes(const glm::mat4& transform, float length, float thickness)
	{
		const glm::vec3 origin = transform_point(transform, glm::vec3(0.0f));
		draw_line(origin, transform_point(transform, glm::vec3(length, 0.0f, 0.0f)), glm::vec4(1.0f, 0.1f, 0.1f, 1.0f), thickness);
		draw_line(origin, transform_point(transform, glm::vec3(0.0f, length, 0.0f)), glm::vec4(0.1f, 1.0f, 0.1f, 1.0f), thickness);
		draw_line(origin, transform_point(transform, glm::vec3(0.0f, 0.0f, length)), glm::vec4(0.1f, 0.35f, 1.0f, 1.0f), thickness);
	}

	void DebugDrawService::snapshot_lines(std::vector<DebugDrawLine>& out_lines) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		out_lines = m_lines;
	}

	void DebugDrawService::commit_frame()
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_lines.swap(m_pending_lines);
		m_pending_lines.clear();
	}

	void DebugDrawService::clear_frame()
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_pending_lines.clear();
		m_lines.clear();
	}

	size_t DebugDrawService::get_line_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_lines.size();
	}

	bool DebugDrawService::empty() const
	{
		return get_line_count() == 0u;
	}

	void DebugDrawService::append_line_locked(const DebugDrawLine& line)
	{
		m_pending_lines.push_back(line);
	}
}
