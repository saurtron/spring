/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "ModelDrawerState.hpp"
#include "ModelDrawer.h"
#include "Game/Camera.h"
#include "Game/Game.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/IWater.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/Env/SkyLight.h"
#include "Rendering/GL/GeometryBuffer.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/Common/ModelDrawer.h"
#include "Rendering/Common/ModelDrawerHelpers.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Sim/Misc/TeamHandler.h"
#include "System/Matrix44f.h"
#include "System/Config/ConfigHandler.h"
#include "System/SpringMath.h"
#include "System/StringUtil.h"

#include "System/Misc/TracyDefs.h"



bool IModelDrawerState::SetTeamColor(int team, float alpha) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// need this because we can be called by no-team projectiles
	if (!teamHandler.IsValidTeam(team))
		return false;

	// should be an assert, but projectiles (+FlyingPiece) would trigger it
	if (shadowHandler.InShadowPass())
		return false;

	return true;
}

void IModelDrawerState::SetupOpaqueDrawing(bool deferredPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE * CModelDrawerConcept::WireFrameModeRef() + GL_FILL * (1 - CModelDrawerConcept::WireFrameModeRef()));

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	if (IsLegacy()) {
		glAlphaFunc(GL_GREATER, 0.5f);
		glEnable(GL_ALPHA_TEST);
	}

	Enable(deferredPass, false);
}

void IModelDrawerState::ResetOpaqueDrawing(bool deferredPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	Disable(deferredPass);

	if (IsLegacy())
		glDisable(GL_ALPHA_TEST);

	glPopAttrib();
}

void IModelDrawerState::SetupAlphaDrawing(bool deferredPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | (GL_COLOR_BUFFER_BIT * IsLegacy()));
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE * CModelDrawerConcept::WireFrameModeRef() + GL_FILL * (1 - CModelDrawerConcept::WireFrameModeRef()));

	Enable(/*deferredPass always false*/ false, true);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (IsLegacy()) {
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.1f);
	}

	glDepthMask(GL_FALSE);
}

void IModelDrawerState::ResetAlphaDrawing(bool deferredPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	Disable(/*deferredPass*/ false);
	glPopAttrib();
}


////////////// GLSL ////////////////

CModelDrawerStateGLSL::CModelDrawerStateGLSL()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// if (!CanEnable())
	// 	return;

	#define sh shaderHandler

	const GL::LightHandler* lightHandler = CModelDrawerConcept::GetLightHandler();
	static const std::string shaderNames[MODEL_SHADER_COUNT] = {
		"ModelShaderGLSL-NoShadowStandard",
		"ModelShaderGLSL-ShadowedStandard",
		"ModelShaderGLSL-NoShadowDeferred",
		"ModelShaderGLSL-ShadowedDeferred",
	};
	const std::string extraDefs =
		("#define BASE_DYNAMIC_MODEL_LIGHT " + IntToString(lightHandler->GetBaseLight()) + "\n") +
		("#define MAX_DYNAMIC_MODEL_LIGHTS " + IntToString(lightHandler->GetMaxLights()) + "\n");

	for (uint32_t n = MODEL_SHADER_NOSHADOW_STANDARD; n <= MODEL_SHADER_SHADOWED_DEFERRED; n++) {
		modelShaders[n] = sh->CreateProgramObject(PO_CLASS, shaderNames[n]);
		modelShaders[n]->AttachShaderObject(sh->CreateShaderObject("GLSL/ModelVertProg.glsl", extraDefs, GL_VERTEX_SHADER));
		modelShaders[n]->AttachShaderObject(sh->CreateShaderObject("GLSL/ModelFragProg.glsl", extraDefs, GL_FRAGMENT_SHADER));

		modelShaders[n]->SetFlag("USE_SHADOWS", int((n & 1) == 1));
		modelShaders[n]->SetFlag("DEFERRED_MODE", int(n >= MODEL_SHADER_NOSHADOW_DEFERRED));
		modelShaders[n]->SetFlag("GBUFFER_NORMTEX_IDX", GL::GeometryBuffer::ATTACHMENT_NORMTEX);
		modelShaders[n]->SetFlag("GBUFFER_DIFFTEX_IDX", GL::GeometryBuffer::ATTACHMENT_DIFFTEX);
		modelShaders[n]->SetFlag("GBUFFER_SPECTEX_IDX", GL::GeometryBuffer::ATTACHMENT_SPECTEX);
		modelShaders[n]->SetFlag("GBUFFER_EMITTEX_IDX", GL::GeometryBuffer::ATTACHMENT_EMITTEX);
		modelShaders[n]->SetFlag("GBUFFER_MISCTEX_IDX", GL::GeometryBuffer::ATTACHMENT_MISCTEX);
		modelShaders[n]->SetFlag("GBUFFER_ZVALTEX_IDX", GL::GeometryBuffer::ATTACHMENT_ZVALTEX);

		modelShaders[n]->Link();

		modelShaders[n]->Enable();

		modelShaders[n]->SetUniform("diffuseTex"    , 0);
		modelShaders[n]->SetUniform("shadingTex"    , 1);
		modelShaders[n]->SetUniform("shadowTex"     , 2);
		modelShaders[n]->SetUniform("shadowColorTex", 3);
		modelShaders[n]->SetUniform("reflectTex"    , 4);
		modelShaders[n]->SetUniform("specularTex"   , 5);

		modelShaders[n]->SetUniform3v("sunDir", &ISky::GetSky()->GetLight()->GetLightDir().x);
		modelShaders[n]->SetUniform3v("cameraPos", &camera->GetPos()[0]);
		modelShaders[n]->SetUniform("teamColor", 0.0f, 0.0f, 0.0f, 0.0f);
		modelShaders[n]->SetUniform("nanoColor", 0.0f, 0.0f, 0.0f, 0.0f);
		modelShaders[n]->SetUniform3v("sunAmbient", &sunLighting->modelAmbientColor[0]);
		modelShaders[n]->SetUniform3v("sunDiffuse", &sunLighting->modelDiffuseColor[0]);
		modelShaders[n]->SetUniform3v("sunSpecular", &sunLighting->modelSpecularColor[0]);
		modelShaders[n]->SetUniform("shadowDensity", sunLighting->modelShadowDensity);
		modelShaders[n]->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());

		modelShaders[n]->Disable();
		modelShaders[n]->Validate();
	}

	// make the active shader non-NULL
	SetActiveShader(shadowHandler.ShadowsLoaded(), false);

	#undef sh
}

CModelDrawerStateGLSL::~CModelDrawerStateGLSL()
{
	RECOIL_DETAILED_TRACY_ZONE;
	modelShaders.fill(nullptr);
	modelShader = nullptr;
	shaderHandler->ReleaseProgramObjects(PO_CLASS);
}

bool CModelDrawerStateGLSL::CanEnable() const { return CModelDrawerConcept::UseAdvShading(); }
bool CModelDrawerStateGLSL::CanDrawDeferred() const { return CModelDrawerConcept::DeferredAllowed(); }

bool CModelDrawerStateGLSL::SetTeamColor(int team, float alpha) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IModelDrawerState::SetTeamColor(team, alpha))
		return false;

	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	float4 teamColor = CModelDrawerHelper::GetTeamColor(team, alpha);
	modelShader->SetUniform4v("teamColor", &teamColor.r);

	return true;
}

void CModelDrawerStateGLSL::Enable(bool deferredPass, bool alphaPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// body of former EnableCommon();
	CModelDrawerHelper::PushTransform(camera);
	CModelDrawerHelper::EnableTexturesCommon();

	SetActiveShader(shadowHandler.ShadowsLoaded(), deferredPass);
	assert(modelShader != nullptr);
	modelShader->Enable();
	// end of EnableCommon();

	modelShader->SetUniform3v("sunDir", &ISky::GetSky()->GetLight()->GetLightDir().x);
	modelShader->SetUniform3v("sunAmbient", &sunLighting->modelAmbientColor[0]);
	modelShader->SetUniform3v("sunDiffuse", &sunLighting->modelDiffuseColor[0]);
	modelShader->SetUniform3v("sunSpecular", &sunLighting->modelSpecularColor[0]);
	modelShader->SetUniform("shadowDensity", sunLighting->modelShadowDensity);
	modelShader->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());

	CModelDrawerConcept::GetLightHandler()->Update(modelShader);
}

void CModelDrawerStateGLSL::Disable(bool deferredPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);

	modelShader->Disable();
	SetActiveShader(shadowHandler.ShadowsLoaded(), deferredPass);

	CModelDrawerHelper::DisableTexturesCommon();
	CModelDrawerHelper::PopTransform();
}

void CModelDrawerStateGLSL::SetNanoColor(const float4& color) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader->IsBound());
	modelShader->SetUniform4v("nanoColor", &color.x);
}

void CModelDrawerStateGLSL::EnableTextures() const { CModelDrawerHelper::EnableTexturesCommon(); }
void CModelDrawerStateGLSL::DisableTextures() const { CModelDrawerHelper::DisableTexturesCommon(); }

////////////// GL4 ////////////////

CModelDrawerStateGL4::CModelDrawerStateGL4()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!CanEnable())
	return;

	#define sh shaderHandler

	static const std::string shaderNames[MODEL_SHADER_COUNT] = {
		"ModelShaderGL4-NoShadowStandard",
		"ModelShaderGL4-ShadowedStandard",
		"ModelShaderGL4-NoShadowDeferred",
		"ModelShaderGL4-ShadowedDeferred",
	};

	for (uint32_t n = MODEL_SHADER_NOSHADOW_STANDARD; n <= MODEL_SHADER_SHADOWED_DEFERRED; n++) {
		modelShaders[n] = sh->CreateProgramObject(PO_CLASS, shaderNames[n]);
		modelShaders[n]->AttachShaderObject(sh->CreateShaderObject("GLSL/ModelVertProgGL4.glsl", "", GL_VERTEX_SHADER));
		modelShaders[n]->AttachShaderObject(sh->CreateShaderObject("GLSL/ModelFragProgGL4.glsl", "", GL_FRAGMENT_SHADER));

		modelShaders[n]->SetFlag("USE_SHADOWS", int((n & 1) == 1));
		modelShaders[n]->SetFlag("DEFERRED_MODE", int(n >= MODEL_SHADER_NOSHADOW_DEFERRED));
		modelShaders[n]->SetFlag("GBUFFER_NORMTEX_IDX", GL::GeometryBuffer::ATTACHMENT_NORMTEX);
		modelShaders[n]->SetFlag("GBUFFER_DIFFTEX_IDX", GL::GeometryBuffer::ATTACHMENT_DIFFTEX);
		modelShaders[n]->SetFlag("GBUFFER_SPECTEX_IDX", GL::GeometryBuffer::ATTACHMENT_SPECTEX);
		modelShaders[n]->SetFlag("GBUFFER_EMITTEX_IDX", GL::GeometryBuffer::ATTACHMENT_EMITTEX);
		modelShaders[n]->SetFlag("GBUFFER_MISCTEX_IDX", GL::GeometryBuffer::ATTACHMENT_MISCTEX);
		modelShaders[n]->SetFlag("GBUFFER_ZVALTEX_IDX", GL::GeometryBuffer::ATTACHMENT_ZVALTEX);

		modelShaders[n]->Link();
		modelShaders[n]->Enable();
		modelShaders[n]->Disable();
		modelShaders[n]->Validate();
	}

	// make the active shader non-NULL
	SetActiveShader(shadowHandler.ShadowsLoaded(), false);

	#undef sh
}

CModelDrawerStateGL4::~CModelDrawerStateGL4()
{
	RECOIL_DETAILED_TRACY_ZONE;
	modelShaders.fill(nullptr);
	modelShader = nullptr;
	shaderHandler->ReleaseProgramObjects(PO_CLASS);
}

bool CModelDrawerStateGL4::CanEnable() const { return globalRendering->haveGL4 && CModelDrawerConcept::UseAdvShading(); }
bool CModelDrawerStateGL4::CanDrawDeferred() const { return CModelDrawerConcept::DeferredAllowed(); }

bool CModelDrawerStateGL4::SetTeamColor(int team, float alpha) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IModelDrawerState::SetTeamColor(team, alpha))
		return false;

	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	modelShader->SetUniform("teamColorAlpha", alpha);

	return true;
}

void CModelDrawerStateGL4::Enable(bool deferredPass, bool alphaPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// body of former EnableCommon();
	CModelDrawerHelper::EnableTexturesCommon();

	SetActiveShader(shadowHandler.ShadowsLoaded(), deferredPass);
	assert(modelShader != nullptr);
	modelShader->Enable();

	switch (game->GetDrawMode())
	{
	case CGame::GameDrawMode::gameReflectionDraw: {
		glEnable(GL_CLIP_DISTANCE2);
		SetCameraMode(ShaderCameraModes::REFLCT_CAMERA);
	} break;
	case CGame::GameDrawMode::gameRefractionDraw: {
		glEnable(GL_CLIP_DISTANCE2);
		SetCameraMode(ShaderCameraModes::REFRAC_CAMERA);
	} break;
	default: SetCameraMode(ShaderCameraModes::NORMAL_CAMERA); break;
	}

	float gtThreshold = mix(0.5, 0.1, static_cast<float>(alphaPass));
	modelShader->SetUniform("alphaCtrl", gtThreshold, 1.0f, 0.0f, 0.0f); // test > 0.1 | 0.5

	// end of EnableCommon();
}

void CModelDrawerStateGL4::Disable(bool deferredPass) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);

	modelShader->Disable();

	SetActiveShader(shadowHandler.ShadowsLoaded(), deferredPass);

	switch (game->GetDrawMode())
	{
	case CGame::GameDrawMode::gameReflectionDraw: {
		glDisable(GL_CLIP_DISTANCE2);
	} break;
	case CGame::GameDrawMode::gameRefractionDraw: {
		glDisable(GL_CLIP_DISTANCE2);
	} break;
	default: {} break;
	}

	CModelDrawerHelper::DisableTexturesCommon();
}

void CModelDrawerStateGL4::SetNanoColor(const float4& color) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	modelShader->SetUniform("nanoColor", color.r, color.g, color.b, color.a);
}

void CModelDrawerStateGL4::EnableTextures() const { CModelDrawerHelper::EnableTexturesCommon(); }
void CModelDrawerStateGL4::DisableTextures() const { CModelDrawerHelper::DisableTexturesCommon(); }

void CModelDrawerStateGL4::SetColorMultiplier(float r, float g, float b, float a) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);
	assert(modelShader->IsBound());
	modelShader->SetUniform("colorMult", r, g, b, a);
}

ShaderCameraModes CModelDrawerStateGL4::SetCameraMode(ShaderCameraModes scm_) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	std::swap(scm, scm_);
	modelShader->SetUniform("cameraMode", static_cast<int>(scm));

	switch (scm)
	{
	case ShaderCameraModes::REFLCT_CAMERA:
		SetClipPlane(2, { 0.0f,  1.0f, 0.0f, 0.0f });
		break;
	case ShaderCameraModes::REFRAC_CAMERA:
		SetClipPlane(2, { 0.0f, -1.0f, 0.0f, 0.0f });
		break;
	default:
		SetClipPlane(2  /* default, no clipping  */);
		break;
	}

	return scm_; //old state
}

ShaderMatrixModes CModelDrawerStateGL4::SetMatrixMode(ShaderMatrixModes smm_) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	std::swap(smm, smm_);
	modelShader->SetUniform("matrixMode", static_cast<int>(smm));

	return smm_; //old state
}

ShaderShadingModes CModelDrawerStateGL4::SetShadingMode(ShaderShadingModes ssm_) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	std::swap(ssm, ssm_);
	modelShader->SetUniform("shadingMode", static_cast<int>(ssm));

	return ssm_; //old state
}

void CModelDrawerStateGL4::SetStaticModelMatrix(const CMatrix44f& mat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelShader != nullptr);
	assert(modelShader->IsBound());

	modelShader->SetUniformMatrix4x4("staticModelMatrix", false, &mat.m[0]);
}

void CModelDrawerStateGL4::SetClipPlane(uint8_t idx, const float4& cp) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (idx)
	{
	case 0: //upper construction clip plane
		modelShader->SetUniform("clipPlane0", cp.x, cp.y, cp.z, cp.w);
		break;
	case 1: //lower construction clip plane
		modelShader->SetUniform("clipPlane1", cp.x, cp.y, cp.z, cp.w);
		break;
	case 2: //water clip plane
		modelShader->SetUniform("clipPlane2", cp.x, cp.y, cp.z, cp.w);
		break;
	default:
		assert(false);
		break;
	}
}

IModelDrawerState::IModelDrawerState()
{
	RECOIL_DETAILED_TRACY_ZONE;
	modelShaders.fill(nullptr);

	//dup with every instance, but ok
	alphaValues.x = std::max(0.11f, std::min(1.0f, 1.0f - configHandler->GetFloat("UnitTransparency")));
	alphaValues.y = std::min(1.0f, alphaValues.x + 0.1f);
	alphaValues.z = std::min(1.0f, alphaValues.x + 0.2f);
	alphaValues.w = std::min(1.0f, alphaValues.x + 0.4f);
}

bool IModelDrawerState::IsValid() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	bool valid = true;
	for (auto ms : modelShaders) {
		if (!ms)
			continue;

		valid &= ms->IsValid();
	}

	return valid;
}
