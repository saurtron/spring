#include "UniformConstants.h"

#include <cassert>
#include <stdint.h>

#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/GL/VBO.h"
#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Game/TraceRay.h"
#include "Game/UI/MiniMap.h"
#include "Game/UI/MouseHandler.h"
#include "Sim/Units/Unit.h"
#include "Sim/Features/Feature.h"
#include "Sim/Misc/GlobalConstants.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Misc/Wind.h"
#include "Map/ReadMap.h"
#include "System/Log/ILog.h"
#include "System/SafeUtil.h"
#include "SDL2/SDL_mouse.h"

CR_BIND(UniformMatricesBuffer, )

CR_REG_METADATA(UniformMatricesBuffer, (
	CR_MEMBER_BEGINFLAG(CM_NoSerialize),

		CR_MEMBER(screenView),
		CR_MEMBER(screenProj),
		CR_MEMBER(screenViewProj),

		CR_MEMBER(cameraView),
		CR_MEMBER(cameraProj),
		CR_MEMBER(cameraViewProj),
		CR_MEMBER(cameraBillboardView),

		CR_MEMBER(cameraViewInv),
		CR_MEMBER(cameraProjInv),
		CR_MEMBER(cameraViewProjInv),

		CR_MEMBER(shadowView),
		CR_MEMBER(shadowProj),
		CR_MEMBER(shadowViewProj),

		CR_MEMBER(reflectionView),
		CR_MEMBER(reflectionProj),
		CR_MEMBER(reflectionViewProj),

		CR_MEMBER(orthoProj01),

		CR_MEMBER(mmDrawView),
		CR_MEMBER(mmDrawProj),
		CR_MEMBER(mmDrawViewProj),

		CR_MEMBER(mmDrawIMMView),
		CR_MEMBER(mmDrawIMMProj),
		CR_MEMBER(mmDrawIMMViewProj),

		CR_MEMBER(mmDrawDimView),
		CR_MEMBER(mmDrawDimProj),
		CR_MEMBER(mmDrawDimViewProj),

	CR_MEMBER_ENDFLAG(CM_NoSerialize)
))

CR_BIND(UniformParamsBuffer, )

CR_REG_METADATA(UniformParamsBuffer, (
	CR_MEMBER_BEGINFLAG(CM_NoSerialize),

		CR_MEMBER(rndVec3),
		CR_MEMBER(renderCaps),

		CR_MEMBER(timeInfo),
		CR_MEMBER(viewGeometry),
		CR_MEMBER(mapSize),
		CR_MEMBER(mapHeight),

		CR_MEMBER(fogColor),
		CR_MEMBER(fogParams),

		CR_MEMBER(sunDir),

		CR_MEMBER(sunAmbientModel),
		CR_MEMBER(sunAmbientMap),
		CR_MEMBER(sunDiffuseModel),
		CR_MEMBER(sunDiffuseMap),
		CR_MEMBER(sunSpecularModel),
		CR_MEMBER(sunSpecularMap),

		CR_MEMBER(shadowDensity),

		CR_MEMBER(windInfo),
		CR_MEMBER(mouseScreenPos),
		CR_MEMBER(mouseStatus),
		CR_MEMBER(mouseUnused),
		CR_MEMBER(mouseWorldPos),

		CR_MEMBER(teamColor),

	CR_MEMBER_ENDFLAG(CM_NoSerialize)
))

bool UniformConstants::Supported()
{
	static bool supported = VBO::IsSupported(GL_UNIFORM_BUFFER) && GLEW_ARB_shading_language_420pack; //UBO && UBO layout(binding=x)
	return supported;
}

void UniformConstants::Init()
{
	if (initialized) //don't need to reinit on resolution changes
		return;

	if (!Supported()) {
	#ifndef HEADLESS
		LOG_L(L_ERROR, "[UniformConstants::%s] Important OpenGL extensions are not supported by the system\n  GLEW_ARB_uniform_buffer_object = %d\n  GLEW_ARB_shading_language_420pack = %d", __func__, GLEW_ARB_uniform_buffer_object, GLEW_ARB_shading_language_420pack);
	#endif
		return;
	}

	{
		IStreamBufferConcept::StreamBufferCreationParams p;
		p.target = GL_UNIFORM_BUFFER;
		p.numElems = 1;
		p.type = IStreamBufferConcept::Types::SB_BUFFERSUBDATA;

		p.name = "UniformMatricesBuffer";
		umbSBT = IStreamBuffer<UniformMatricesBuffer>::CreateInstance(p);

		p.name = "UniformParamsBuffer";
		upbSBT = IStreamBuffer<UniformParamsBuffer  >::CreateInstance(p);
	}

	glslDefinitions[0] = SetGLSLDefinition<UniformMatricesBuffer>(UBO_MATRIX_IDX);
	glslDefinitions[1] = SetGLSLDefinition<UniformParamsBuffer  >(UBO_PARAMS_IDX);

	initialized = true;
}

void UniformConstants::Kill()
{
	if (!Supported() || !initialized)
		return;

	umbSBT->UnbindBufferRange(UBO_MATRIX_IDX);
	upbSBT->UnbindBufferRange(UBO_PARAMS_IDX);

	umbSBT = nullptr;
	upbSBT = nullptr;

	initialized = false;
}

void UniformConstants::UpdateMatricesImpl(UniformMatricesBuffer* updateBuffer)
{
	updateBuffer->screenView = globalRendering->screenViewMatrix;
	updateBuffer->screenProj = globalRendering->screenProjMatrix;
	updateBuffer->screenViewProj = updateBuffer->screenProj * updateBuffer->screenView;

	const auto* camPlayer = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);

	updateBuffer->cameraView = camPlayer->GetViewMatrix();
	updateBuffer->cameraProj = camPlayer->GetProjectionMatrix();
	updateBuffer->cameraViewProj = camPlayer->GetViewProjectionMatrix();

	// pretty useless as billboarding should be applied to modelView matrix and not viewMatrix
	// much easier way is to assign identity matrix to top-left 3x3 submatrix in the shader
	updateBuffer->cameraBillboardView = updateBuffer->cameraView * camPlayer->GetBillBoardMatrix(); //GetBillBoardMatrix() is supposed to be multiplied by the view Matrix

	updateBuffer->cameraViewInv = camPlayer->GetViewMatrixInverse();
	updateBuffer->cameraProjInv = camPlayer->GetProjectionMatrixInverse();
	updateBuffer->cameraViewProjInv = camPlayer->GetViewProjectionMatrixInverse();

	updateBuffer->shadowView = shadowHandler.GetShadowViewMatrix(CShadowHandler::SHADOWMAT_TYPE_DRAWING);
	updateBuffer->shadowProj = shadowHandler.GetShadowProjMatrix(CShadowHandler::SHADOWMAT_TYPE_DRAWING);
	updateBuffer->shadowViewProj = updateBuffer->shadowProj * updateBuffer->shadowView;

	{
		const auto* prvCam = CCameraHandler::GetSetActiveCamera(CCamera::CAMTYPE_UWREFL);

		auto* reflCam = CCameraHandler::GetActiveCamera();
		reflCam->CopyStateReflect(prvCam);

		updateBuffer->reflectionView = reflCam->GetViewMatrix();
		updateBuffer->reflectionProj = reflCam->GetProjectionMatrix();
		updateBuffer->reflectionViewProj = reflCam->GetViewProjectionMatrix();

		CCameraHandler::SetActiveCamera(CCamera::CAMTYPE_PLAYER);
	}

	updateBuffer->orthoProj01 = CMatrix44f::ClipOrthoProj01();

	updateBuffer->mmDrawView = minimap->GetViewMat(0);
	updateBuffer->mmDrawIMMView = minimap->GetViewMat(1);
	updateBuffer->mmDrawDimView = minimap->GetViewMat(2);

	updateBuffer->mmDrawProj = minimap->GetProjMat(0);
	updateBuffer->mmDrawIMMProj = minimap->GetProjMat(1);
	updateBuffer->mmDrawDimProj = minimap->GetProjMat(2);

	updateBuffer->mmDrawViewProj = updateBuffer->mmDrawProj * updateBuffer->mmDrawView;
	updateBuffer->mmDrawIMMViewProj = updateBuffer->mmDrawIMMProj * updateBuffer->mmDrawIMMView;
	updateBuffer->mmDrawDimViewProj = updateBuffer->mmDrawDimProj * updateBuffer->mmDrawDimView;
}


void UniformConstants::UpdateParamsImpl(UniformParamsBuffer* updateBuffer)
{
	updateBuffer->rndVec3 = guRNG.NextVector();
	//TODO add something else
	updateBuffer->renderCaps =
		globalRendering->supportClipSpaceControl << 0;

	updateBuffer->timeInfo = float4{(float)gs->frameNum, spring_tomsecs(globalRendering->grTime) * 0.001f, (gs->GetLuaSimFrame() + globalRendering->timeOffset) / GAME_SPEED, globalRendering->timeOffset}; //gameFrame, drawSeconds, interpolated(unsynced)GameSeconds(synced), frameTimeOffset
	updateBuffer->viewGeometry = float4{(float)globalRendering->viewSizeX, (float)globalRendering->viewSizeY, (float)globalRendering->viewPosX, (float)globalRendering->viewPosY}; //vsx, vsy, vpx, vpy
	updateBuffer->mapSize = float4{(float)mapDims.mapx, (float)mapDims.mapy, (float)mapDims.pwr2mapx, (float)mapDims.pwr2mapy} *(float)SQUARE_SIZE; //xz, xzPO2
	updateBuffer->mapHeight = float4{readMap->GetCurrMinHeight(), readMap->GetCurrMaxHeight(), readMap->GetInitMinHeight(), readMap->GetInitMaxHeight()};

	const auto& sky = ISky::GetSky();
	float4 fogColor = (sky != nullptr) ? float4{sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 1.0f} : float4{0.7f, 0.7f, 0.8f, 1.0f};
	updateBuffer->fogColor = fogColor;

	const auto camPlayer = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);
	float4 fogParams = (sky != nullptr) ? float4{sky->fogStart * camPlayer->GetFarPlaneDist(), sky->fogEnd * camPlayer->GetFarPlaneDist(), 0.0f, 0.0f} : float4{0.1f * CGlobalRendering::MAX_VIEW_RANGE, 1.0f * CGlobalRendering::MAX_VIEW_RANGE, 0.0f, 0.0f};
	fogParams.w = 1.0f / (fogParams.y - fogParams.x);
	updateBuffer->fogParams = fogParams;

	updateBuffer->sunDir = (sky != nullptr) ? sky->GetLight()->GetLightDir() : float4(/*map default*/ 0.0f, 0.447214f, 0.894427f, 1.0f);

	updateBuffer->sunAmbientModel = sunLighting->modelAmbientColor;
	updateBuffer->sunAmbientMap = sunLighting->groundAmbientColor;

	updateBuffer->sunDiffuseModel = sunLighting->modelDiffuseColor;
	updateBuffer->sunDiffuseMap = sunLighting->groundDiffuseColor;

	updateBuffer->sunSpecularModel = float4{ sunLighting->modelSpecularColor.xyz, sunLighting->specularExponent };
	updateBuffer->sunSpecularMap = float4{ sunLighting->groundSpecularColor.xyz, sunLighting->specularExponent };

	updateBuffer->shadowDensity = float4{ sunLighting->groundShadowDensity, sunLighting->modelShadowDensity, 0.0, 0.0 };

	updateBuffer->windInfo = float4{ envResHandler.GetCurrentWindVec(), envResHandler.GetCurrentWindStrength() };

	updateBuffer->mouseScreenPos = float2{
		static_cast<float>(mouse->lastx),
		static_cast<float>(globalRendering->viewPosY + globalRendering->viewSizeY - mouse->lasty - 1)
	};

	updateBuffer->mouseStatus = (
		mouse->buttons[SDL_BUTTON_LEFT  ].pressed << 0 |
		mouse->buttons[SDL_BUTTON_MIDDLE].pressed << 1 |
		mouse->buttons[SDL_BUTTON_RIGHT ].pressed << 2 |
		mouse->offscreen                          << 3 |
		mouse->mmbScroll                          << 4 |
		mouse->locked                             << 5
	);
	updateBuffer->mouseUnused = 0u;

	{
		const int wx = mouse->lastx;
		const int wy = mouse->lasty;

		const CUnit* unit = nullptr;
		const CFeature* feature = nullptr;

		const float rawRange = camPlayer->GetFarPlaneDist() * 1.4f;
		const float badRange = rawRange - 300.0f;

		const float3 camPos = camPlayer->GetPos();
		const float3 pxlDir = camPlayer->CalcPixelDir(wx, wy);

		// trace for player's allyteam
		const float traceDist = TraceRay::GuiTraceRay(camPos, pxlDir, rawRange, nullptr, unit, feature, true, false, true);

		const float3 tracePos = camPos + (pxlDir * traceDist);

		if (unit)
			updateBuffer->mouseWorldPos = float4{ unit->drawPos, 1.0f };
		else if (feature)
			updateBuffer->mouseWorldPos = float4{ feature->drawPos, 1.0f };
		else
			updateBuffer->mouseWorldPos = float4{ tracePos, 1.0f };

		if ((traceDist < 0.0f || traceDist > badRange) && unit == nullptr && feature == nullptr) {
			updateBuffer->mouseWorldPos.w = 0.0f;
		}
	}

	for (int teamID = 0; teamID < teamHandler.ActiveTeams(); ++teamID) {
		const CTeam* team = teamHandler.Team(teamID);
		if (team == nullptr || !teamHandler.IsActiveTeam(teamID))
			continue;

		updateBuffer->teamColor[teamID] = float4{team->color[0] / 255.0f, team->color[1] / 255.0f, team->color[2] / 255.0f, team->color[3] / 255.0f};
	}
}

void UniformConstants::UpdateMatrices()
{
	if (!Supported())
		return;

	auto umbMap = umbSBT->Map();
	UniformConstants::UpdateMatricesImpl(umbMap);
	umbSBT->Unmap();
}

void UniformConstants::UpdateParams()
{
	if (!Supported())
		return;

	auto upbMap = upbSBT->Map();
	UniformConstants::UpdateParamsImpl(upbMap);
	upbSBT->Unmap();
}

void UniformConstants::Bind()
{
	if (!Supported())
		return;

	assert(umbSBT->GetID() && upbSBT->GetID());

	umbSBT->BindBufferRange(UBO_MATRIX_IDX);
	upbSBT->BindBufferRange(UBO_PARAMS_IDX);
}
