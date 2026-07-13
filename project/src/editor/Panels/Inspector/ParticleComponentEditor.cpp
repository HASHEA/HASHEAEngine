#include "Panels/Inspector/ParticleComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <cstdint>

namespace AshEditor
{
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
		bool bCommitRequested = false;
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

		if (bCommitRequested)
		{
			refHost.CommitParticleDraft(entity);
		}
	}
}
