/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <SDL_keycode.h>

#include "OverheadController.h"

#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/UI/MouseHandler.h"
#include "Map/Ground.h"
#include "Map/ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "System/SpringMath.h"
#include "System/Log/ILog.h"
#include "System/Config/ConfigHandler.h"
#include "System/Input/KeyInput.h"

#include "System/Misc/TracyDefs.h"

CONFIG(float, MiddleClickScrollSpeed).defaultValue(0.01f);
CONFIG(int, OverheadScrollSpeed).defaultValue(10);
CONFIG(float, OverheadTiltSpeed).defaultValue(1.0f);
CONFIG(bool, OverheadEnabled).defaultValue(true).headlessValue(false);
CONFIG(float, OverheadFOV).defaultValue(45.0f);
CONFIG(float, OverheadMinZoomDistance).defaultValue(60.0f).description("Minimum camera zoom distance");
CONFIG(float, OverheadMaxHeightFactor).defaultValue(1.0f).description("Float multiplier for maximum overhead camera zoom distance");
CONFIG(float, CamOverheadFastScale).defaultValue(3.0f / 10.0f).description("Scaling for CameraMoveFastMult.");

static const float angleStep = math::HALFPI / 14.0f;


COverheadController::COverheadController()
	: flipped(false)
	, changeAltHeight(true)

	// make whole map visible in overhead mode
	, height(CGround::GetHeightAboveWater(pos.x, pos.z, false) + (2.5f * std::max(pos.x / globalRendering->aspectRatio, pos.z)))
	, oldAltHeight(height)

	, maxHeight(10000.0f)
	, minHeight(60.0f)
	, angle(DEFAULT_ANGLE)
{
	configHandler->NotifyOnChange(this, {"MiddleClickScrollSpeed", "OverheadScrollSpeed", "OverheadTiltSpeed", "OverheadEnabled", "OverheadFOV", "OverheadMinZoomDistance", "OverheadMaxHeightFactor", "CamOverheadFastScale"});
	ConfigUpdate();
}

COverheadController::~COverheadController()
{
	RECOIL_DETAILED_TRACY_ZONE;
	configHandler->RemoveObserver(this);
}

void COverheadController::ConfigUpdate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	middleClickScrollSpeed = configHandler->GetFloat("MiddleClickScrollSpeed");
	scrollSpeed = configHandler->GetInt("OverheadScrollSpeed") * 0.1f;
	tiltSpeed = configHandler->GetFloat("OverheadTiltSpeed");
	enabled = configHandler->GetBool("OverheadEnabled");
	fov = configHandler->GetFloat("OverheadFOV");
	minHeight = configHandler->GetFloat("OverheadMinZoomDistance");
	maxHeight = 9.5f * std::max(mapDims.mapx, mapDims.mapy) * configHandler->GetFloat("OverheadMaxHeightFactor");
	fastScale = configHandler->GetFloat("CamOverheadFastScale");
}

void COverheadController::ConfigNotify(const std::string & key, const std::string & value)
{
	RECOIL_DETAILED_TRACY_ZONE;
	ConfigUpdate();
}

void COverheadController::KeyMove(float3 move)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (flipped) {
		move.x = -move.x;
		move.y = -move.y;
	}

	move *= (math::sqrt(move.z) * 200.0f);
	pos.x += (move.x * pixelSize * 2.0f * scrollSpeed);
	pos.z -= (move.y * pixelSize * 2.0f * scrollSpeed);

	Update();
}

void COverheadController::MouseMove(float3 move)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// z is the speed modifier, in practice invertMouse{0,1} => move.z{-1,1}
	move.x *= move.z;
	move.y *= move.z;

	if (flipped) {
		move.x = -move.x;
		move.y = -move.y;
	}

	const bool moveFast = camHandler->GetActiveCamera()->GetMovState()[CCamera::MOVE_STATE_FST];

	// ignore middleClickScrollSpeed sign in locked MMB-scroll mode
	move = mix(move, move * Sign(middleClickScrollSpeed), mouse->locked) * middleClickScrollSpeed * 100.0f;
	pos.x += (move.x * pixelSize * (1 + moveFast * camera->moveFastMult * fastScale) * scrollSpeed);
	pos.z += (move.y * pixelSize * (1 + moveFast * camera->moveFastMult * fastScale) * scrollSpeed);

	Update();
}

void COverheadController::ScreenEdgeMove(float3 move)
{
	RECOIL_DETAILED_TRACY_ZONE;
	KeyMove(move);
}


void COverheadController::MouseWheelMove(float move, const float3& newDir)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (move == 0.0f)
		return;

	camHandler->CameraTransition(0.05f);

	const bool moveFast     = camHandler->GetActiveCamera()->GetMovState()[CCamera::MOVE_STATE_FST];
	const bool moveTilt     = camHandler->GetActiveCamera()->GetMovState()[CCamera::MOVE_STATE_TLT];
	const float shiftSpeed  = (moveFast ? camera->moveFastMult * fastScale: 1.0f);
	const float altZoomDist = height * move * 0.007f * shiftSpeed;

	// tilt the camera if LCTRL is pressed
	//
	// otherwise holding down LALT uses 'instant-zoom'
	// from here to the end of the function (smoothed)
	if (moveTilt) {
		angle += (move * tiltSpeed * shiftSpeed * 0.025f) * angleStep;
		angle = std::clamp(angle, 0.01f, math::HALFPI);
		camHandler->CameraTransition(0.125f);
	} else {
		const bool moveReset = camHandler->GetActiveCamera()->GetMovState()[CCamera::MOVE_STATE_RST];

		if (move < 0.0f) {
			// ZOOM IN to mouse cursor instead of mid screen
			float3 cpos = pos - dir * height;
			float dif = -altZoomDist;

			if ((height - dif) < 60.0f)
				dif = height - 60.0f;

			// instazoom in to standard view
			if (moveReset)
				dif = (height - oldAltHeight) / newDir.y * dir.y;

			float3 wantedPos = cpos + newDir * dif;

			float newHeight = CGround::LineGroundCol(wantedPos, wantedPos + dir * 15000.0f, false);
			float yDirClamp = std::max(std::fabs(dir.y), 0.0001f) * std::copysign(1.0f, dir.y);

			if (newHeight < 0.0f)
				newHeight = height * (1.0f + move * 0.007f * shiftSpeed);

			if ((wantedPos.y + (dir.y * newHeight)) < 0.0f)
				newHeight = -wantedPos.y / yDirClamp;

			if(newHeight < minHeight) {
				wantedPos = cpos + newDir * (height - minHeight);
				newHeight = minHeight;
			}

			if(height > minHeight) {
				height = newHeight;
				pos = wantedPos + dir * height;
			}
		} else {
			// ZOOM OUT from mid screen
			if (moveReset) {
				// instazoom out to maximum height
				if (height < maxHeight * 0.5f && changeAltHeight) {
					oldAltHeight = height;
					changeAltHeight = false;
				}

				height = maxHeight;
				pos.x  = mapDims.mapx * SQUARE_SIZE * 0.5f;
				pos.z  = mapDims.mapy * SQUARE_SIZE * 0.55f; // somewhat longer toward bottom
			} else {
				height *= (1.0f + (altZoomDist / height));
			}
		}

		// instant-zoom: turn on the smooth transition and reset the camera tilt
		if (moveReset) {
			angle = DEFAULT_ANGLE;
			camHandler->CameraTransition(1.0f);
		} else {
			changeAltHeight = true;
		}
	}

	Update();
}

void COverheadController::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	pos.x = std::clamp(pos.x, 0.01f, mapDims.mapx * SQUARE_SIZE - 0.01f);
	pos.z = std::clamp(pos.z, 0.01f, mapDims.mapy * SQUARE_SIZE - 0.01f);
	pos.y = CGround::GetHeightAboveWater(pos.x, pos.z, false);

	height = std::clamp(height, minHeight, maxHeight);
	angle = std::clamp(angle, 0.01f, math::HALFPI);

	dir = float3(0.0f, -fastmath::cos(angle), flipped ? fastmath::sin(angle) : -fastmath::sin(angle));
	pixelSize = (camera->GetTanHalfFov() * 2.0f) / globalRendering->viewSizeY * height * 2.0f;
}


void COverheadController::SetPos(const float3& newPos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	pos = newPos;
	Update();
}


void COverheadController::SwitchTo(const CCameraController* oldCam, const bool showText)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (showText)
		LOG("Switching to Overhead (TA) style camera");

	float3 oldPos = oldCam->SwitchFrom();
	if (oldCam->GetName() == "ov"){
		pos = oldPos + dir * height;
		Update();
		return;
	}

	dir = oldCam->GetDir();
	if (dir.y > 0) {
		dir.y = -.5;
		dir.Normalize();
	}
	height = DistanceToGround(oldPos, dir, 0);
	pos = oldPos + dir * height;

	angle = math::PI - CCamera::GetRotFromDir(dir).x;
	Update();
}

void COverheadController::GetState(StateMap& sm) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	CCameraController::GetState(sm);

	sm["height"]  = height;
	sm["angle"]   = angle;
	sm["flipped"] = flipped ? +1.0f : -1.0f;
}

bool COverheadController::SetState(const StateMap& sm)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CCameraController::SetState(sm);

	SetStateFloat(sm, "height", height);
	SetStateFloat(sm, "angle", angle);
	SetStateBool (sm, "flipped", flipped);

	return true;
}
