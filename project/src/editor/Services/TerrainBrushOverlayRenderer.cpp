#include "Services/TerrainBrushOverlayRenderer.h"

#include "Function/Scene/TerrainQuery.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace AshEditor
{
	namespace
	{
		constexpr uint32_t kTerrainBrushOverlaySegmentCount = 64u;
		constexpr double kTwoPi = 6.283185307179586476925286766559;
		constexpr float kTransformEpsilon = 1.0e-5f;
		constexpr glm::vec4 kTerrainBrushReadyColor{ 0.20f, 1.00f, 0.25f, 1.00f };
		constexpr glm::vec4 kTerrainBrushPendingColor{ 1.00f, 0.65f, 0.10f, 1.00f };
		constexpr glm::vec4 kTerrainBrushFailedColor{ 1.00f, 0.20f, 0.20f, 1.00f };

		struct AxisAlignedTerrainTransform
		{
			glm::vec3 translation{ 0.0f };
			glm::vec3 scale{ 1.0f };
		};

		bool IsFinite(float value)
		{
			return std::isfinite(value);
		}

		bool IsFinite(const glm::vec3& value)
		{
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
		}

		bool IsNearlyZero(float value)
		{
			return std::abs(value) <= kTransformEpsilon;
		}

		bool TryExtractAxisAlignedTerrainTransform(
			const glm::mat4& matrix,
			AxisAlignedTerrainTransform& outTransform)
		{
			for (glm::length_t column = 0; column < 4; ++column)
			{
				for (glm::length_t row = 0; row < 4; ++row)
				{
					if (!IsFinite(matrix[column][row]))
					{
						return false;
					}
				}
			}

			if (!IsNearlyZero(matrix[0][1]) || !IsNearlyZero(matrix[0][2]) || !IsNearlyZero(matrix[0][3]) ||
				!IsNearlyZero(matrix[1][0]) || !IsNearlyZero(matrix[1][2]) || !IsNearlyZero(matrix[1][3]) ||
				!IsNearlyZero(matrix[2][0]) || !IsNearlyZero(matrix[2][1]) || !IsNearlyZero(matrix[2][3]) ||
				!IsNearlyZero(matrix[3][3] - 1.0f))
			{
				return false;
			}

			const glm::vec3 scale{ matrix[0][0], matrix[1][1], matrix[2][2] };
			const glm::vec3 translation{ matrix[3][0], matrix[3][1], matrix[3][2] };
			if (!IsFinite(scale) || !IsFinite(translation) ||
				scale.x <= 0.0f ||
				scale.y <= 0.0f ||
				scale.z <= 0.0f)
			{
				return false;
			}

			outTransform.translation = translation;
			outTransform.scale = scale;
			return true;
		}

		glm::vec4 ResolveTerrainBrushOverlayColor(const TerrainEditorPreviewState& refPreview)
		{
			if (refPreview.layer_locked ||
				refPreview.viewport.query_status == AshEngine::TerrainQueryStatus::Failed)
			{
				return kTerrainBrushFailedColor;
			}
			if (refPreview.viewport.query_status == AshEngine::TerrainQueryStatus::Pending)
			{
				return kTerrainBrushPendingColor;
			}
			return kTerrainBrushReadyColor;
		}
	}

	std::vector<AshEngine::SceneOverlayLine> TerrainBrushOverlayRenderer::BuildLines(
		const TerrainEditorPreviewState& refPreview,
		const AshEngine::TerrainAssetSnapshot& refSnapshot,
		const glm::mat4& matTerrainLocalToWorld)
	{
		std::vector<AshEngine::SceneOverlayLine> lines{};
		const TerrainViewportPreviewState& viewport = refPreview.viewport;
		if (refPreview.query_status != AshEngine::TerrainQueryStatus::Ready ||
			!viewport.has_world_position ||
			viewport.query_status == AshEngine::TerrainQueryStatus::Outside ||
			!IsFinite(viewport.center_ws) ||
			!IsFinite(viewport.radius_meters) ||
			viewport.radius_meters <= 0.0f)
		{
			return lines;
		}

		AxisAlignedTerrainTransform transform{};
		if (!TryExtractAxisAlignedTerrainTransform(matTerrainLocalToWorld, transform))
		{
			return lines;
		}

		std::array<glm::vec3, kTerrainBrushOverlaySegmentCount> worldPoints{};
		std::array<bool, kTerrainBrushOverlaySegmentCount> pointReady{};
		for (uint32_t pointIndex = 0u; pointIndex < kTerrainBrushOverlaySegmentCount; ++pointIndex)
		{
			const double angle = kTwoPi * static_cast<double>(pointIndex) /
				static_cast<double>(kTerrainBrushOverlaySegmentCount);
			const float worldX = viewport.center_ws.x +
				viewport.radius_meters * static_cast<float>(std::cos(angle));
			const float worldZ = viewport.center_ws.z +
				viewport.radius_meters * static_cast<float>(std::sin(angle));
			const glm::vec2 terrainLocalXz{
				(worldX - transform.translation.x) / transform.scale.x,
				(worldZ - transform.translation.z) / transform.scale.z
			};
			float terrainLocalHeight = 0.0f;
			if (AshEngine::query_height(refSnapshot, terrainLocalXz, terrainLocalHeight) !=
				AshEngine::TerrainQueryStatus::Ready)
			{
				continue;
			}

			const float worldY = transform.translation.y + terrainLocalHeight * transform.scale.y;
			const glm::vec3 point{ worldX, worldY, worldZ };
			if (!IsFinite(point))
			{
				continue;
			}
			worldPoints[pointIndex] = point;
			pointReady[pointIndex] = true;
		}

		const glm::vec4 color = ResolveTerrainBrushOverlayColor(refPreview);
		lines.reserve(kTerrainBrushOverlaySegmentCount);
		for (uint32_t pointIndex = 0u; pointIndex < kTerrainBrushOverlaySegmentCount; ++pointIndex)
		{
			const uint32_t nextIndex = (pointIndex + 1u) % kTerrainBrushOverlaySegmentCount;
			if (!pointReady[pointIndex] || !pointReady[nextIndex])
			{
				continue;
			}

			AshEngine::SceneOverlayLine line{};
			line.start = worldPoints[pointIndex];
			line.end = worldPoints[nextIndex];
			line.color = color;
			line.thickness = 2.0f;
			line.depth_bias = 0.0f;
			line.depth_mode = AshEngine::SceneOverlayDepthMode::DepthTestNoWrite;
			lines.push_back(line);
		}
		return lines;
	}

	bool TerrainBrushOverlayRenderer::Submit(
		const TerrainEditorPreviewState& refPreview,
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot,
		const glm::mat4& matTerrainLocalToWorld,
		AshEngine::SceneViewBindingHandle binding)
	{
		return Submit(
			refPreview,
			refSnapshot,
			matTerrainLocalToWorld,
			binding,
			&AshEngine::submit_scene_overlay);
	}

	bool TerrainBrushOverlayRenderer::Submit(
		const TerrainEditorPreviewState& refPreview,
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot,
		const glm::mat4& matTerrainLocalToWorld,
		AshEngine::SceneViewBindingHandle binding,
		SubmitOverlayFunction pSubmitOverlay)
	{
		if (!binding.is_valid() || !refSnapshot || pSubmitOverlay == nullptr)
		{
			return false;
		}

		const std::vector<AshEngine::SceneOverlayLine> lines =
			BuildLines(refPreview, *refSnapshot, matTerrainLocalToWorld);
		if (lines.empty())
		{
			return false;
		}

		const AshEngine::SceneOverlayBatchDesc desc{
			lines.data(),
			static_cast<uint32_t>(lines.size())
		};
		return pSubmitOverlay(binding, desc);
	}
}
