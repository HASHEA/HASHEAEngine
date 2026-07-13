#include "Panels/Inspector/ParticleComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorAssetPathWidgets.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace AshEditor
{
	namespace
	{
		constexpr float kParticleLifetimePreviewHeight = 36.0f;
		constexpr float kParticlePreviewMaximumExtent = 100000.0f;

		void DrawParticleSizeLifetimePreview(
			AshEngine::UIContext& refUi,
			const float fStartSize,
			const float fEndSize)
		{
			const float fAvailableWidth = refUi.get_content_region_avail().x;
			const float fPreviewWidth = std::isfinite(fAvailableWidth)
				? std::clamp(fAvailableWidth, 1.0f, kParticlePreviewMaximumExtent)
				: 1.0f;
			const float fSafeStartSize = std::isfinite(fStartSize)
				? std::clamp(fStartSize, 0.0f, kParticlePreviewMaximumExtent)
				: 0.0f;
			const float fSafeEndSize = std::isfinite(fEndSize)
				? std::clamp(fEndSize, 0.0f, kParticlePreviewMaximumExtent)
				: 0.0f;
			const AshEngine::UIVec2 vecPreviewSize{
				fPreviewWidth,
				kParticleLifetimePreviewHeight
			};
			refUi.dummy(vecPreviewSize);
			const AshEngine::UIRect rectRawPreview = refUi.get_item_rect();
			const AshEngine::UIRect rectPreview{
				std::isfinite(rectRawPreview.x)
					? std::clamp(rectRawPreview.x, -kParticlePreviewMaximumExtent, kParticlePreviewMaximumExtent)
					: 0.0f,
				std::isfinite(rectRawPreview.y)
					? std::clamp(rectRawPreview.y, -kParticlePreviewMaximumExtent, kParticlePreviewMaximumExtent)
					: 0.0f,
				std::isfinite(rectRawPreview.width)
					? std::clamp(rectRawPreview.width, 1.0f, kParticlePreviewMaximumExtent)
					: fPreviewWidth,
				std::isfinite(rectRawPreview.height)
					? std::clamp(rectRawPreview.height, 1.0f, kParticlePreviewMaximumExtent)
					: kParticleLifetimePreviewHeight
			};
			refUi.draw_window_rect_filled(rectPreview, { 0.055f, 0.065f, 0.085f, 1.0f }, 3.0f);

			const float fMaximumSize = std::max({ fSafeStartSize, fSafeEndSize, 0.001f });
			constexpr float kPreviewPadding = 4.0f;
			const float fUsableHeight = std::max(rectPreview.height - kPreviewPadding * 2.0f, 1.0f);
			const float fStartRatio = std::clamp(fSafeStartSize / fMaximumSize, 0.0f, 1.0f);
			const float fEndRatio = std::clamp(fSafeEndSize / fMaximumSize, 0.0f, 1.0f);
			const AshEngine::UIVec2 vecStart{
				rectPreview.x + kPreviewPadding,
				rectPreview.y + rectPreview.height - kPreviewPadding - fStartRatio * fUsableHeight
			};
			const AshEngine::UIVec2 vecEnd{
				rectPreview.x + rectPreview.width - kPreviewPadding,
				rectPreview.y + rectPreview.height - kPreviewPadding - fEndRatio * fUsableHeight
			};
			refUi.draw_window_line(vecStart, vecEnd, { 0.20f, 0.56f, 0.96f, 1.0f }, 2.0f);
			refUi.draw_window_rect(rectPreview, GetEditorOverlayBorderColor(refUi), 3.0f, 1.0f);
		}

		void DrawParticleColorLifetimePreview(
			AshEngine::UIContext& refUi,
			const glm::vec4& refStartColor,
			const glm::vec4& refEndColor)
		{
			const float fAvailableWidth = refUi.get_content_region_avail().x;
			const float fPreviewWidth = std::isfinite(fAvailableWidth)
				? std::clamp(fAvailableWidth, 1.0f, kParticlePreviewMaximumExtent)
				: 1.0f;
			const auto makeFinitePreviewColor = [](const glm::vec4& refColor, const glm::vec4& refFallback)
			{
				return glm::vec4{
					std::isfinite(refColor.r) ? std::clamp(refColor.r, 0.0f, 1.0f) : refFallback.r,
					std::isfinite(refColor.g) ? std::clamp(refColor.g, 0.0f, 1.0f) : refFallback.g,
					std::isfinite(refColor.b) ? std::clamp(refColor.b, 0.0f, 1.0f) : refFallback.b,
					std::isfinite(refColor.a) ? std::clamp(refColor.a, 0.0f, 1.0f) : refFallback.a
				};
			};
			const glm::vec4 vecSafeStartColor = makeFinitePreviewColor(
				refStartColor,
				{ 1.0f, 1.0f, 1.0f, 1.0f });
			const glm::vec4 vecSafeEndColor = makeFinitePreviewColor(
				refEndColor,
				{ 1.0f, 1.0f, 1.0f, 0.0f });
			const AshEngine::UIVec2 vecPreviewSize{
				fPreviewWidth,
				kParticleLifetimePreviewHeight
			};
			refUi.dummy(vecPreviewSize);
			const AshEngine::UIRect rectRawPreview = refUi.get_item_rect();
			const AshEngine::UIRect rectPreview{
				std::isfinite(rectRawPreview.x)
					? std::clamp(rectRawPreview.x, -kParticlePreviewMaximumExtent, kParticlePreviewMaximumExtent)
					: 0.0f,
				std::isfinite(rectRawPreview.y)
					? std::clamp(rectRawPreview.y, -kParticlePreviewMaximumExtent, kParticlePreviewMaximumExtent)
					: 0.0f,
				std::isfinite(rectRawPreview.width)
					? std::clamp(rectRawPreview.width, 1.0f, kParticlePreviewMaximumExtent)
					: fPreviewWidth,
				std::isfinite(rectRawPreview.height)
					? std::clamp(rectRawPreview.height, 1.0f, kParticlePreviewMaximumExtent)
					: kParticleLifetimePreviewHeight
			};
			constexpr uint32_t kPreviewColumnCount = 24u;
			constexpr uint32_t kPreviewRowCount = 2u;
			const float fCellWidth = rectPreview.width / static_cast<float>(kPreviewColumnCount);
			const float fCellHeight = rectPreview.height / static_cast<float>(kPreviewRowCount);

			for (uint32_t uColumn = 0; uColumn < kPreviewColumnCount; ++uColumn)
			{
				const float fLifetime = static_cast<float>(uColumn) /
					static_cast<float>(kPreviewColumnCount - 1u);
				const glm::vec4 vecColor = glm::mix(vecSafeStartColor, vecSafeEndColor, fLifetime);
				const float fRed = std::isfinite(vecColor.r) ? std::clamp(vecColor.r, 0.0f, 1.0f) : 0.0f;
				const float fGreen = std::isfinite(vecColor.g) ? std::clamp(vecColor.g, 0.0f, 1.0f) : 0.0f;
				const float fBlue = std::isfinite(vecColor.b) ? std::clamp(vecColor.b, 0.0f, 1.0f) : 0.0f;
				const float fAlpha = std::isfinite(vecColor.a) ? std::clamp(vecColor.a, 0.0f, 1.0f) : 0.0f;
				for (uint32_t uRow = 0; uRow < kPreviewRowCount; ++uRow)
				{
					const float fChecker = ((uColumn + uRow) % 2u) == 0u ? 0.20f : 0.34f;
					const AshEngine::UIColor colorComposited{
						fRed * fAlpha + fChecker * (1.0f - fAlpha),
						fGreen * fAlpha + fChecker * (1.0f - fAlpha),
						fBlue * fAlpha + fChecker * (1.0f - fAlpha),
						1.0f
					};
					const AshEngine::UIRect rectCell{
						rectPreview.x + static_cast<float>(uColumn) * fCellWidth,
						rectPreview.y + static_cast<float>(uRow) * fCellHeight,
						fCellWidth,
						fCellHeight
					};
					refUi.draw_window_rect_filled(rectCell, colorComposited);
				}
			}
			refUi.draw_window_rect(rectPreview, GetEditorOverlayBorderColor(refUi), 3.0f, 1.0f);
		}
	}

	AshEngine::SceneComponentType ParticleComponentEditor::GetComponentType() const
	{
		return AshEngine::SceneComponentType::Particle;
	}

	const char* ParticleComponentEditor::GetDisplayName() const
	{
		return "Particle";
	}

	bool ParticleComponentEditor::CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const
	{
		(void)refHost;
		return AshEngine::can_add_scene_component(entity, GetComponentType());
	}

	bool ParticleComponentEditor::AddDefault(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		(void)refUi;
		refHost.ResetParticleDraftToLive(entity);
		InspectorPanelState& refState = refHost.AccessInspectorState();
		refState.draftParticle.optCurrentValue = AshEngine::ParticleComponent{};
		SanitizeOptionalParticleComponent(refState.draftParticle.optCurrentValue);
		return refHost.CommitParticleDraft(entity);
	}

	bool ParticleComponentEditor::ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity)
	{
		refHost.SyncParticleDraft(entity);
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		return
			refState.draftParticle.uEntityId == entity.get_id() &&
			refState.draftParticle.optCurrentValue.has_value();
	}

	void ParticleComponentEditor::Draw(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		if (!refState.draftParticle.optCurrentValue.has_value())
		{
			return;
		}

		const bool bOpen = refUi.collapsing_header("Particle", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (refHost.DrawComponentHeaderContextMenu(refUi, kInspectorParticleComponentMenuId))
		{
			refState.draftParticle.optCurrentValue.reset();
			refHost.CommitParticleDraft(entity);
			return;
		}
		if (!bOpen)
		{
			return;
		}

		AshEngine::ParticleComponent& particle = *refState.draftParticle.optCurrentValue;
		const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
		bool bCommitRequested = false;
		bool bCommitBlocked = false;

		if (refUi.collapsing_header("Main##ParticleMain", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			bCommitRequested = DrawInspectorSceneBoolField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"emitting",
					"Emitting",
					"Controls whether this emitter creates new particles.",
					"Enabled",
					"On / Off",
					"Existing particles continue simulating when emission is disabled."),
				"Emitting",
				particle.emitting) || bCommitRequested;
			if (DrawInspectorSceneUIntField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"max_particles",
					"Max Particles",
					"Particle pool capacity allocated for this emitter.",
					"4096",
					"[1, 65536]",
					"Changing capacity rebuilds this emitter's GPU particle pools."),
				"Max Particles",
				particle.max_particles,
				1,
				256))
			{
				bCommitRequested = true;
			}
			if (DrawInspectorSceneUIntField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"random_seed",
					"Random Seed",
					"Seed used by deterministic particle spawning and simulation.",
					"0",
					"[0, +inf)",
					"The same seed and fixed timestep produce the same simulation."),
				"Random Seed",
				particle.random_seed))
			{
				bCommitRequested = true;
			}
		}

		if (refUi.collapsing_header("Emission##ParticleEmission", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"spawn_rate",
					"Spawn Rate",
					"Number of particles emitted per second.",
					"200",
					"[0, 20000]",
					"Fractional spawn counts accumulate deterministically between frames."),
				"Spawn Rate",
				particle.spawn_rate,
				1.0f,
				0.0f,
				20000.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"lifetime",
					"Lifetime",
					"Base particle lifetime in seconds.",
					"2",
					"[0.01, 60] seconds",
					"Lifetime variance is applied around this value."),
				"Lifetime",
				particle.lifetime,
				0.05f,
				0.01f,
				60.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"lifetime_variance",
					"Lifetime Variance",
					"Random plus-or-minus spread applied to particle lifetime.",
					"0",
					"[0, 30] seconds",
					"The deterministic random seed controls the sampled value."),
				"Lifetime Variance",
				particle.lifetime_variance,
				0.05f,
				0.0f,
				30.0f) || bCommitRequested;
		}

		if (refUi.collapsing_header("Shape & Motion##ParticleShapeMotion"))
		{
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"initial_speed",
					"Initial Speed",
					"Initial speed along the emitter's local +Y direction.",
					"3",
					"[0, 100]",
					"Spread Angle perturbs the initial direction inside a cone."),
				"Initial Speed",
				particle.initial_speed,
				0.1f,
				0.0f,
				100.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"spread_angle_degrees",
					"Spread Angle",
					"Half-angle of the emission cone around local +Y.",
					"15",
					"[0, 90] degrees",
					"Zero emits all particles in the same direction."),
				"Spread Angle",
				particle.spread_angle_degrees,
				0.25f,
				0.0f,
				90.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorDragVec3Field(
				refUi,
				"Acceleration",
				particle.constant_acceleration,
				0.1f,
				0.0f,
				0.0f,
				MakeInspectorSceneFieldSpec(
					MakeInspectorSceneFieldDesc(
						AshEngine::SceneComponentType::Particle,
						"constant_acceleration",
						"Acceleration",
						"Constant world-space acceleration applied every simulation step.",
						"(0, -9.8, 0)",
						"Finite numbers",
						"Invalid values are restored to defaults before commit."))) || bCommitRequested;
		}

		if (refUi.collapsing_header("Size Over Lifetime##ParticleSizeLifetime"))
		{
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"start_size",
					"Start Size",
					"Billboard size when a particle is born.",
					"0.1",
					"[0, 10]",
					"Size interpolates linearly toward End Size."),
				"Start Size",
				particle.start_size,
				0.01f,
				0.0f,
				10.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"end_size",
					"End Size",
					"Billboard size at the end of a particle's lifetime.",
					"0.02",
					"[0, 10]",
					"Size interpolates linearly from Start Size."),
				"End Size",
				particle.end_size,
				0.01f,
				0.0f,
				10.0f) || bCommitRequested;
			DrawParticleSizeLifetimePreview(refUi, particle.start_size, particle.end_size);
		}

		if (refUi.collapsing_header("Color Over Lifetime##ParticleColorLifetime"))
		{
			bCommitRequested = DrawInspectorSceneColor4Field(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"start_color",
					"Start Color",
					"Linear RGBA color when a particle is born.",
					"(1, 1, 1, 1)",
					"[0, 1] per channel",
					"Color interpolates linearly toward End Color."),
				"Start Color",
				particle.start_color) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneColor4Field(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"end_color",
					"End Color",
					"Linear RGBA color at the end of a particle's lifetime.",
					"(1, 1, 1, 0)",
					"[0, 1] per channel",
					"Color interpolates linearly from Start Color."),
				"End Color",
				particle.end_color) || bCommitRequested;
			DrawParticleColorLifetimePreview(refUi, particle.start_color, particle.end_color);
		}

		const bool bRendererOpen = refUi.collapsing_header(
			"Renderer##ParticleRenderer",
			AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (bRendererOpen)
		{
			InspectorAssetPathWidgetState spriteTexturePathState{};
			spriteTexturePathState.pVecRecentPaths = &refState.vecRecentParticleSpritePaths;
			spriteTexturePathState.pStrSearch = &refState.strParticleSpriteAssetPickerSearch;
			InspectorAssetPathFieldDesc spriteTexturePathDesc = MakeInspectorSceneAssetPathFieldDesc(
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"sprite_texture_path",
					"Sprite Texture",
					"RGBA sprite texture sampled by each particle billboard.",
					"Empty",
					"Texture asset path",
					"Type a relative asset path, browse, or drag-drop from the Asset Browser."),
				"Sprite Texture",
				"ParticleSpriteAssetPickerPopup",
				"Select Particle Sprite");
			spriteTexturePathDesc.pBrowseLabel = "Browse##ParticleSprite";
			bCommitRequested = DrawInspectorAssetPathField(
				refUi,
				particle.sprite_texture_path,
				spriteTexturePathDesc,
				spriteTexturePathState,
				refDeps.pAssetDatabaseService,
				refDeps.pDragDropTransferService) || bCommitRequested;
			if (particle.sprite_texture_path.empty())
			{
				refUi.text_colored(
					GetEditorMutedTextColor(refUi),
					"Using Default Particle Sprite (White)");
			}
		}
		std::string strSpriteTextureValidationMessage{};
		const bool bSpriteTextureBlocksCommit = TryGetParticleSpriteTextureValidationMessage(
			refState.draftParticle,
			refDeps.pAssetDatabaseService,
			strSpriteTextureValidationMessage);
		bCommitBlocked = bSpriteTextureBlocksCommit || bCommitBlocked;
		if (bRendererOpen)
		{
			if (!strSpriteTextureValidationMessage.empty())
			{
				refUi.text_colored(
					GetEditorWarningTextColor(refUi),
					"%s",
					strSpriteTextureValidationMessage.c_str());
			}

			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"radial_falloff",
					"Radial Falloff",
					"Blends the sprite with the analytic radial mask.",
					"1",
					"[0, 1]",
					"Zero preserves the sprite; one applies the full radial mask."),
				"Radial Falloff",
				particle.radial_falloff,
				0.01f,
				0.0f,
				1.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"radial_sharpness",
					"Radial Sharpness",
					"Power exponent for the analytic radial mask.",
					"2",
					"[0.25, 8]",
					"Higher values concentrate coverage toward the sprite center."),
				"Radial Sharpness",
				particle.radial_sharpness,
				0.05f,
				0.25f,
				8.0f) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneBoolField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"soft_particles",
					"Soft Particles",
					"Fades particles near opaque scene depth intersections.",
					"Enabled",
					"On / Off",
					"Disabling soft particles retains the configured fade distance."),
				"Soft Particles",
				particle.soft_particles) || bCommitRequested;
			bCommitRequested = DrawInspectorSceneDragFloatField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"soft_fade_distance",
					"Soft Fade Distance",
					"World-space depth interval used by soft particles.",
					"0.25",
					"[0.001, 10]",
					"The value is retained while Soft Particles is disabled."),
				"Soft Fade Distance",
				particle.soft_fade_distance,
				0.01f,
				0.001f,
				10.0f,
				"%.3f",
				particle.soft_particles) || bCommitRequested;

			int32_t iBlendMode = static_cast<int32_t>(particle.blend_mode);
			if (DrawInspectorSceneEnumField(
				refUi,
				MakeInspectorSceneFieldDesc(
					AshEngine::SceneComponentType::Particle,
					"blend_mode",
					"Blend Mode",
					"Controls how particle color is blended into the HDR scene.",
					"Additive",
					"Additive / AlphaBlend",
					"AlphaBlend is unsorted in this first implementation."),
				"Blend Mode",
				iBlendMode))
			{
				particle.blend_mode = static_cast<AshEngine::ParticleBlendMode>(iBlendMode);
				bCommitRequested = true;
			}
		}

		if (SanitizeParticleComponent(particle))
		{
			LogInspectorDraftSanitized("Particle", entity.get_id());
			bCommitRequested = true;
		}
		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			refHost,
			{
				"Reset Particle",
				"Reset Particle",
				"Write the default particle settings back to the entity and keep the change undoable.",
				"Restore##Particle",
				"Restore Particle",
				"Discard the local particle draft and reload the current scene values without committing.",
				"Particle"
			});
		if (actionRowResult.bResetRequested)
		{
			refHost.ResetParticleDraftToDefaults(entity);
			LogInspectorDraftReset("Particle", "defaults", entity.get_id());
			refHost.CommitParticleDraft(entity);
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			refHost.ResetParticleDraftToLive(entity);
			LogInspectorDraftReset("Particle", "live scene state", entity.get_id());
			return;
		}
		if (actionRowResult.bRemoveRequested)
		{
			refState.draftParticle.optCurrentValue.reset();
			refHost.CommitParticleDraft(entity);
			return;
		}

		if (bCommitRequested && !bCommitBlocked)
		{
			refHost.CommitParticleDraft(entity);
		}
	}
}
