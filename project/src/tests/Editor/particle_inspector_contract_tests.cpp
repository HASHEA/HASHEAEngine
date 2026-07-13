#include "doctest.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	std::string ReadSourceContractFile(const char* pPath)
	{
		std::ifstream file(pPath);
		REQUIRE_MESSAGE(file.is_open(), "failed to open source contract file: ", pPath);
		return {
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>()
		};
	}

	std::string SliceFromMarker(
		const std::string& strSource,
		const std::string& strBeginMarker,
		const std::string& strEndMarker)
	{
		const size_t uBegin = strSource.find(strBeginMarker);
		REQUIRE_MESSAGE(uBegin != std::string::npos, "missing source marker: ", strBeginMarker);
		const size_t uEnd = strSource.find(strEndMarker, uBegin + strBeginMarker.size());
		REQUIRE_MESSAGE(uEnd != std::string::npos, "missing source marker: ", strEndMarker);
		return strSource.substr(uBegin, uEnd - uBegin);
	}

	std::string SliceFromMarkerToEnd(
		const std::string& strSource,
		const std::string& strBeginMarker)
	{
		const size_t uBegin = strSource.find(strBeginMarker);
		REQUIRE_MESSAGE(uBegin != std::string::npos, "missing source marker: ", strBeginMarker);
		return strSource.substr(uBegin);
	}

	void CheckDefaultOpenHeader(const std::string& strDrawFunction, const char* pHeader)
	{
		const size_t uHeader = strDrawFunction.find(pHeader);
		REQUIRE_MESSAGE(uHeader != std::string::npos, "missing particle inspector header: ", pHeader);
		const size_t uCallBegin = strDrawFunction.rfind("collapsing_header(", uHeader);
		REQUIRE(uCallBegin != std::string::npos);
		const size_t uCallEnd = strDrawFunction.find(';', uHeader);
		REQUIRE(uCallEnd != std::string::npos);
		const std::string strCall = strDrawFunction.substr(uCallBegin, uCallEnd - uCallBegin);
		CHECK(strCall.find("AshEngine::UITreeNodeFlagBits::DefaultOpen") != std::string::npos);
	}
}

TEST_CASE("Particle Inspector exposes stable modular headers and calls read-only previews")
{
	const std::string strSource = ReadSourceContractFile(
		"project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp");
	const std::string strDrawFunction = SliceFromMarkerToEnd(
		strSource,
		"void ParticleComponentEditor::Draw(");
	const std::array<const char*, 6> arrHeaders{
		"Main##ParticleMain",
		"Emission##ParticleEmission",
		"Shape & Motion##ParticleShapeMotion",
		"Size Over Lifetime##ParticleSizeLifetime",
		"Color Over Lifetime##ParticleColorLifetime",
		"Renderer##ParticleRenderer"
	};

	size_t uPrevious = 0;
	for (const char* pHeader : arrHeaders)
	{
		const size_t uPosition = strDrawFunction.find(pHeader);
		REQUIRE_MESSAGE(uPosition != std::string::npos, "missing particle inspector header: ", pHeader);
		CHECK(uPosition >= uPrevious);
		uPrevious = uPosition;
	}
	CheckDefaultOpenHeader(strDrawFunction, arrHeaders[0]);
	CheckDefaultOpenHeader(strDrawFunction, arrHeaders[1]);
	CheckDefaultOpenHeader(strDrawFunction, arrHeaders[5]);

	const size_t uSizeHeader = strDrawFunction.find(arrHeaders[3]);
	const size_t uSizePreviewCall = strDrawFunction.find("DrawParticleSizeLifetimePreview(", uSizeHeader);
	const size_t uColorHeader = strDrawFunction.find(arrHeaders[4]);
	const size_t uColorPreviewCall = strDrawFunction.find("DrawParticleColorLifetimePreview(", uColorHeader);
	REQUIRE(uSizePreviewCall != std::string::npos);
	REQUIRE(uColorPreviewCall != std::string::npos);
	CHECK(uSizeHeader < uSizePreviewCall);
	CHECK(uSizePreviewCall < uColorHeader);
	CHECK(uColorHeader < uColorPreviewCall);
	CHECK(strSource.find("ImGui::") == std::string::npos);
}

TEST_CASE("Particle Inspector previews sanitize non-finite values before drawing")
{
	const std::string strSource = ReadSourceContractFile(
		"project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp");
	const std::string strSizePreview = SliceFromMarker(
		strSource,
		"void DrawParticleSizeLifetimePreview(",
		"void DrawParticleColorLifetimePreview(");
	const std::string strColorPreview = SliceFromMarker(
		strSource,
		"void DrawParticleColorLifetimePreview(",
		"AshEngine::SceneComponentType ParticleComponentEditor::GetComponentType() const");
	CHECK(strSource.find("#include <cmath>") != std::string::npos);

	const size_t uSizeAvailable = strSizePreview.find("const float fAvailableWidth");
	const size_t uSafeStartSize = strSizePreview.find("const float fSafeStartSize");
	const size_t uSafeEndSize = strSizePreview.find("const float fSafeEndSize");
	const size_t uSizeDummy = strSizePreview.find("refUi.dummy(");
	const size_t uSizeRawRect = strSizePreview.find("const AshEngine::UIRect rectRawPreview");
	const size_t uSizeSafeRect = strSizePreview.find("const AshEngine::UIRect rectPreview");
	const size_t uSizeMaximum = strSizePreview.find("std::max({ fSafeStartSize, fSafeEndSize, 0.001f })");
	const size_t uSizeDraw = strSizePreview.find("draw_window_line(");
	const std::array<const char*, 7> arrFiniteSizeInputs{
		"std::isfinite(fAvailableWidth)",
		"std::isfinite(fStartSize)",
		"std::isfinite(fEndSize)",
		"std::isfinite(rectRawPreview.x)",
		"std::isfinite(rectRawPreview.y)",
		"std::isfinite(rectRawPreview.width)",
		"std::isfinite(rectRawPreview.height)"
	};
	REQUIRE(uSizeAvailable != std::string::npos);
	REQUIRE(uSafeStartSize != std::string::npos);
	REQUIRE(uSafeEndSize != std::string::npos);
	REQUIRE(uSizeDummy != std::string::npos);
	REQUIRE(uSizeRawRect != std::string::npos);
	REQUIRE(uSizeSafeRect != std::string::npos);
	REQUIRE(uSizeMaximum != std::string::npos);
	REQUIRE(uSizeDraw != std::string::npos);
	for (const char* pFiniteInput : arrFiniteSizeInputs)
	{
		CHECK_MESSAGE(strSizePreview.find(pFiniteInput) != std::string::npos, "missing finite size guard: ", pFiniteInput);
	}
	CHECK(uSizeAvailable < uSizeDummy);
	CHECK(uSafeStartSize < uSizeMaximum);
	CHECK(uSafeEndSize < uSizeMaximum);
	CHECK(uSizeDummy < uSizeRawRect);
	CHECK(uSizeRawRect < uSizeSafeRect);
	CHECK(uSizeSafeRect < uSizeDraw);

	const size_t uColorAvailable = strColorPreview.find("const float fAvailableWidth");
	const size_t uSafeStartColor = strColorPreview.find("const glm::vec4 vecSafeStartColor");
	const size_t uSafeEndColor = strColorPreview.find("const glm::vec4 vecSafeEndColor");
	const size_t uColorDummy = strColorPreview.find("refUi.dummy(");
	const size_t uColorRawRect = strColorPreview.find("const AshEngine::UIRect rectRawPreview");
	const size_t uColorSafeRect = strColorPreview.find("const AshEngine::UIRect rectPreview");
	const size_t uColorMix = strColorPreview.find("glm::mix(vecSafeStartColor, vecSafeEndColor");
	const size_t uSafeAlpha = strColorPreview.find("std::isfinite(vecColor.a)");
	const size_t uColorDraw = strColorPreview.find("draw_window_rect_filled(rectCell");
	const std::array<const char*, 13> arrFiniteColorInputs{
		"std::isfinite(fAvailableWidth)",
		"std::isfinite(refColor.r)",
		"std::isfinite(refColor.g)",
		"std::isfinite(refColor.b)",
		"std::isfinite(refColor.a)",
		"std::isfinite(rectRawPreview.x)",
		"std::isfinite(rectRawPreview.y)",
		"std::isfinite(rectRawPreview.width)",
		"std::isfinite(rectRawPreview.height)",
		"std::isfinite(vecColor.r)",
		"std::isfinite(vecColor.g)",
		"std::isfinite(vecColor.b)",
		"std::isfinite(vecColor.a)"
	};
	REQUIRE(uColorAvailable != std::string::npos);
	REQUIRE(uSafeStartColor != std::string::npos);
	REQUIRE(uSafeEndColor != std::string::npos);
	REQUIRE(uColorDummy != std::string::npos);
	REQUIRE(uColorRawRect != std::string::npos);
	REQUIRE(uColorSafeRect != std::string::npos);
	REQUIRE(uColorMix != std::string::npos);
	REQUIRE(uSafeAlpha != std::string::npos);
	REQUIRE(uColorDraw != std::string::npos);
	for (const char* pFiniteInput : arrFiniteColorInputs)
	{
		CHECK_MESSAGE(strColorPreview.find(pFiniteInput) != std::string::npos, "missing finite color guard: ", pFiniteInput);
	}
	CHECK(uColorAvailable < uColorDummy);
	CHECK(uSafeStartColor < uColorMix);
	CHECK(uSafeEndColor < uColorMix);
	CHECK(uColorDummy < uColorRawRect);
	CHECK(uColorRawRect < uColorSafeRect);
	CHECK(uColorSafeRect < uColorMix);
	CHECK(uColorMix < uSafeAlpha);
	CHECK(uSafeAlpha < uColorDraw);
}

TEST_CASE("Particle Inspector recent asset selection applies after iteration")
{
	const std::string strSource = ReadSourceContractFile(
		"project/src/editor/Widgets/InspectorAssetPathWidgets.cpp");
	const std::string strPickerFunction = SliceFromMarker(
		strSource,
		"bool DrawInspectorAssetPathPicker(",
		"bool DrawInspectorAssetPathField(");

	const size_t uRecentLoop = strPickerFunction.find(
		"for (const std::string& strRecent : *refState.pVecRecentPaths)");
	const size_t uCapture = strPickerFunction.find("optSelectedRecentPath = strRecent", uRecentLoop);
	const size_t uBreak = strPickerFunction.find("break;", uCapture);
	const size_t uApplyGuard = strPickerFunction.find("if (optSelectedRecentPath.has_value())", uBreak);
	const size_t uApplyCopy = strPickerFunction.find("*optSelectedRecentPath", uApplyGuard);
	REQUIRE(uRecentLoop != std::string::npos);
	REQUIRE(uCapture != std::string::npos);
	REQUIRE(uBreak != std::string::npos);
	REQUIRE(uApplyGuard != std::string::npos);
	REQUIRE(uApplyCopy != std::string::npos);
	CHECK(strSource.find("#include <optional>") != std::string::npos);
	CHECK(uRecentLoop < uCapture);
	CHECK(uCapture < uBreak);
	CHECK(uBreak < uApplyGuard);
	CHECK(uApplyGuard < uApplyCopy);
	const std::string strLiveIteration = strPickerFunction.substr(uRecentLoop, uApplyGuard - uRecentLoop);
	CHECK(strLiveIteration.find("ApplyInspectorAssetPath(") == std::string::npos);
	CHECK(strPickerFunction.find(
		"\n\t\t\t\t}\n\t\t\t\tif (optSelectedRecentPath.has_value())",
		uBreak) != std::string::npos);
	CHECK(strPickerFunction.find("ApplyInspectorAssetPath(strPath, strRecent") == std::string::npos);
}

TEST_CASE("Particle Inspector integrates the texture picker and gates commit")
{
	const std::string strSource = ReadSourceContractFile(
		"project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp");
	const std::string strDrawFunction = SliceFromMarkerToEnd(
		strSource,
		"void ParticleComponentEditor::Draw(");
	const std::string strRenderer = SliceFromMarker(
		strDrawFunction,
		"const bool bRendererOpen",
		"if (SanitizeParticleComponent(particle))");

	const std::array<const char*, 9> arrPickerMarkers{
		"vecRecentParticleSpritePaths",
		"strParticleSpriteAssetPickerSearch",
		"sprite_texture_path",
		"ParticleSpriteAssetPickerPopup",
		"Select Particle Sprite",
		"spriteTexturePathDesc.pBrowseLabel = \"Browse##ParticleSprite\"",
		"DrawInspectorAssetPathField(",
		"Using Default Particle Sprite (White)",
		"TryGetParticleSpriteTextureValidationMessage("
	};
	size_t uPrevious = 0;
	for (const char* pMarker : arrPickerMarkers)
	{
		const size_t uPosition = strRenderer.find(pMarker);
		REQUIRE_MESSAGE(uPosition != std::string::npos, "missing renderer picker marker: ", pMarker);
		CHECK(uPosition >= uPrevious);
		uPrevious = uPosition;
	}

	CHECK(strRenderer.find("\n\t\tstd::string strSpriteTextureValidationMessage") != std::string::npos);
	const size_t uValidation = strDrawFunction.find("TryGetParticleSpriteTextureValidationMessage(");
	const size_t uBlock = strDrawFunction.find(
		"bCommitBlocked = bSpriteTextureBlocksCommit || bCommitBlocked",
		uValidation);
	const size_t uCommitCondition = strDrawFunction.find("bCommitRequested && !bCommitBlocked", uBlock);
	const size_t uCommit = strDrawFunction.find("refHost.CommitParticleDraft(entity);", uCommitCondition);
	REQUIRE(uBlock != std::string::npos);
	REQUIRE(uCommitCondition != std::string::npos);
	REQUIRE(uCommit != std::string::npos);
	CHECK(uValidation < uBlock);
	CHECK(uBlock < uCommitCondition);
	CHECK(uCommitCondition < uCommit);
}

TEST_CASE("Particle Inspector sprite validation keeps fallback paths committable")
{
	const std::string strSource = ReadSourceContractFile(
		"project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.cpp");
	const std::string strValidation = SliceFromMarker(
		strSource,
		"bool TryGetParticleSpriteTextureValidationMessage(",
		"bool TryGetInspectorAssetPathValidationMessage(");

	CHECK(strValidation.find("AshEngine::AssetType::Texture") != std::string::npos);
	CHECK(strValidation.find("The particle sprite path is missing; runtime will use Default Particle Sprite.") != std::string::npos);
	CHECK(strValidation.find("The particle sprite must be a texture resource.") != std::string::npos);
	CHECK(strValidation.find("The particle sprite is currently ") != std::string::npos);
	CHECK(strValidation.find("desc.bValidateOnlyWhenChanged = false") != std::string::npos);
	CHECK(strValidation.find("desc.bBlockWhenEmpty = false") != std::string::npos);
	CHECK(strValidation.find("desc.bBlockWhenMissingAsset = false") != std::string::npos);
	CHECK(strValidation.find("desc.bBlockWhenUnsupportedAssetType = true") != std::string::npos);
	CHECK(strValidation.find("desc.bBlockWhenLoadStateProblem = false") != std::string::npos);
}

TEST_CASE("Particle Inspector equality and soft fade use the complete visual contract")
{
	const std::string strComparisonSource = ReadSourceContractFile(
		"project/src/editor/Core/EditorComponentComparison.cpp");
	const std::string strComparison = SliceFromMarkerToEnd(
		strComparisonSource,
		"bool ParticleComponentsEqual(");
	const std::string strParticleSource = ReadSourceContractFile(
		"project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp");
	const std::string strRenderer = SliceFromMarker(
		strParticleSource,
		"const bool bRendererOpen",
		"if (SanitizeParticleComponent(particle))");
	const std::array<const char*, 5> arrEqualityExpressions{
		"refLeft.sprite_texture_path == refRight.sprite_texture_path",
		"refLeft.radial_falloff == refRight.radial_falloff",
		"refLeft.radial_sharpness == refRight.radial_sharpness",
		"refLeft.soft_particles == refRight.soft_particles",
		"refLeft.soft_fade_distance == refRight.soft_fade_distance"
	};

	size_t uPrevious = 0;
	for (const char* pExpression : arrEqualityExpressions)
	{
		const size_t uPosition = strComparison.find(pExpression);
		REQUIRE_MESSAGE(uPosition != std::string::npos, "missing particle equality expression: ", pExpression);
		CHECK(uPosition >= uPrevious);
		uPrevious = uPosition;
	}

	const size_t uFadeField = strRenderer.find("\"soft_fade_distance\"");
	REQUIRE(uFadeField != std::string::npos);
	const size_t uFadeCallEnd = strRenderer.find(';', uFadeField);
	REQUIRE(uFadeCallEnd != std::string::npos);
	const std::string strFadeCall = strRenderer.substr(uFadeField, uFadeCallEnd - uFadeField);
	CHECK(strFadeCall.find("particle.soft_particles") != std::string::npos);
}
