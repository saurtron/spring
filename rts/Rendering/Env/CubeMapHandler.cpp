/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/Game.h"
#include "Map/BaseGroundDrawer.h"
#include "Map/Ground.h"
#include "Map/ReadMap.h"
#include "Map/MapInfo.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/Env/DebugCubeMapTexture.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "System/Config/ConfigHandler.h"

#include "System/Misc/TracyDefs.h"

CONFIG(int, CubeTexSizeSpecular).defaultValue(128).minimumValue(1).description("The square resolution of each face of the specular cubemap.");
CONFIG(int, CubeTexSizeReflection).defaultValue(128).minimumValue(1).description("The square resolution of each face of the environment reflection cubemap.");
CONFIG(bool, CubeTexGenerateMipMaps).defaultValue(false).description("Generate mipmaps for the reflection and specular cubemap textures, useful for efficient subsampling and blurring.");

CubeMapHandler cubeMapHandler;


bool CubeMapHandler::Init() {
	RECOIL_DETAILED_TRACY_ZONE;
	envReflectionTexID = 0;
	skyReflectionTexID = 0;
	specularTexID = 0;

	specTexSize = configHandler->GetInt("CubeTexSizeSpecular");
	reflTexSize = configHandler->GetInt("CubeTexSizeReflection");

	specTexPartBuf.clear();
	specTexPartBuf.resize(specTexSize * 4, 0);
	specTexFaceBuf.clear();
	specTexFaceBuf.resize(specTexSize * specTexSize * 4, 0);

	currReflectionFace = 0;
	specularTexIter = 0;

	mapSkyReflections = (!mapInfo->smf.skyReflectModTexName.empty());
	generateMipMaps = configHandler->GetBool("CubeTexGenerateMipMaps");

	{
		glGenTextures(1, &specularTexID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, specularTexID);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, specTexSize, float3( 1,  1,  1), float3( 0, 0, -2), float3(0, -2,  0));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, specTexSize, float3(-1,  1, -1), float3( 0, 0,  2), float3(0, -2,  0));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, specTexSize, float3(-1,  1, -1), float3( 2, 0,  0), float3(0,  0,  2));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, specTexSize, float3(-1, -1,  1), float3( 2, 0,  0), float3(0,  0, -2));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, specTexSize, float3(-1,  1,  1), float3( 2, 0,  0), float3(0, -2,  0));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, specTexSize, float3( 1,  1, -1), float3(-2, 0,  0), float3(0, -2,  0));
	}

	{
		glGenTextures(1, &envReflectionTexID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, envReflectionTexID);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, generateMipMaps? GL_LINEAR_MIPMAP_LINEAR: GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,                                           GL_LINEAR); // magnification doesn't use mips
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	}

	if (generateMipMaps)
		glGenerateMipmapEXT(GL_TEXTURE_CUBE_MAP);

	if (mapSkyReflections) {
		glGenTextures(1, &skyReflectionTexID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, skyReflectionTexID);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA8, reflTexSize, reflTexSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	}

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	// reflectionCubeFBO is no-op constructed, has to be initialized manually
	reflectionCubeFBO.Init(false);

	if (reflectionCubeFBO.IsValid()) {
		reflectionCubeFBO.Bind();
		reflectionCubeFBO.CreateRenderBuffer(GL_DEPTH_ATTACHMENT_EXT, GL_DEPTH_COMPONENT, reflTexSize, reflTexSize);
		reflectionCubeFBO.Unbind();
		return true;
	}

	Free();
	return false;
}

void CubeMapHandler::Free() {
	RECOIL_DETAILED_TRACY_ZONE;
	if (specularTexID != 0) {
		glDeleteTextures(1, &specularTexID);
		specularTexID = 0;
	}
	if (envReflectionTexID != 0) {
		glDeleteTextures(1, &envReflectionTexID);
		envReflectionTexID = 0;
	}
	if (skyReflectionTexID != 0) {
		glDeleteTextures(1, &skyReflectionTexID);
		skyReflectionTexID = 0;
	}

	reflectionCubeFBO.Kill();
}



void CubeMapHandler::UpdateReflectionTexture()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!unitDrawer->UseAdvShading() && !readMap->GetGroundDrawer()->UseAdvShading())
		return;

	// NOTE:
	//   we unbind later in WorldDrawer::GenerateIBLTextures() to save render
	//   context switches (which are one of the slowest OpenGL operations!)
	//   together with VP restoration
	reflectionCubeFBO.Bind();

	switch (currReflectionFace) {
		case 0: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, false); } break;
		case 1: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, false); } break;
		case 2: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, false); } break;
		case 3: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, false); } break;
		case 4: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, false); } break;
		case 5: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, false); } break;
		default: {} break;
	}

	if (mapSkyReflections) {
		// draw only the sky (into its own cubemap) for SSMF
		// by reusing data from previous frame we could also
		// make terrain reflect itself, not just the sky
		switch (currReflectionFace) {
			case  6: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, true); } break;
			case  7: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, true); } break;
			case  8: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, true); } break;
			case  9: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, true); } break;
			case 10: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, true); } break;
			case 11: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, true); } break;
			default: {} break;
		}

		currReflectionFace +=  1;
		currReflectionFace %= 12;
	} else {
		// touch the FBO at least once per frame
		currReflectionFace += 1;
		currReflectionFace %= 6;
	}

	if (generateMipMaps && currReflectionFace == 0) {
		glBindTexture(GL_TEXTURE_CUBE_MAP, envReflectionTexID);
		glGenerateMipmapEXT(GL_TEXTURE_CUBE_MAP);
		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	}
}

void CubeMapHandler::CreateReflectionFace(unsigned int glFace, bool skyOnly)
{
	RECOIL_DETAILED_TRACY_ZONE;
	reflectionCubeFBO.AttachTexture((skyOnly? skyReflectionTexID: envReflectionTexID), glFace);

	glPushAttrib(GL_FOG_BIT | GL_DEPTH_BUFFER_BIT);
	const auto& sky = ISky::GetSky();
	glClearColor(sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	if (!skyOnly) {
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
	} else {
		// do not need depth-testing for the sky alone
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
	}

	{
		CCamera* prvCam = CCameraHandler::GetSetActiveCamera(CCamera::CAMTYPE_ENVMAP);
		CCamera* curCam = CCameraHandler::GetActiveCamera();

		const float3* fd = faceDirs[glFace - GL_TEXTURE_CUBE_MAP_POSITIVE_X];

		// env-reflections are only correct when drawn from an inverted
		// perspective (meaning right becomes left and up becomes down)
		curCam->forward  = fd[0];
		curCam->right    = fd[1] * -1.0f;
		curCam->up       = fd[2] * -1.0f;

		// set vertical *and* horizontal FOV to 90 degrees
		curCam->SetVFOV(90.0f);
		curCam->SetAspectRatio(1.0f);
		curCam->SetPos(prvCam->GetPos());

		curCam->UpdateLoadViewport(0, 0, reflTexSize, reflTexSize);
		curCam->UpdateViewRange();
		curCam->UpdateMatrices(globalRendering->viewSizeX, globalRendering->viewSizeY, curCam->GetAspectRatio());
		curCam->UpdateFrustum();
		curCam->LoadMatrices();

		// generate the face
		game->SetDrawMode(CGame::gameReflectionDraw);

		if (!globalRendering->drawDebugCubeMap) {
			sky->Draw();
			if (!skyOnly)
				readMap->GetGroundDrawer()->Draw(DrawPass::TerrainReflection);
		}
		else {
			debugCubeMapTexture.Draw(glFace);
		}

		game->SetDrawMode(CGame::gameNormalDraw);


		CCameraHandler::SetActiveCamera(prvCam->GetCamType());
	}

	glPopAttrib();
}


void CubeMapHandler::UpdateSpecularTexture()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!unitDrawer->UseAdvShading())
		return;

	glBindTexture(GL_TEXTURE_CUBE_MAP, specularTexID);

	int specularTexRow = specularTexIter / 3; //FIXME WTF

	switch (specularTexIter % 3) {
		case 0: {
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, specTexSize, float3( 1,  1,  1), float3( 0, 0, -2), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, specTexSize, float3(-1,  1, -1), float3( 0, 0,  2), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
		} break;
		case 1: {
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, specTexSize, float3(-1,  1, -1), float3( 2, 0,  0), float3(0,  0,  2), specularTexRow, &specTexPartBuf[0]);
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, specTexSize, float3(-1, -1,  1), float3( 2, 0,  0), float3(0,  0, -2), specularTexRow, &specTexPartBuf[0]);
		} break;
		case 2: {
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, specTexSize, float3(-1,  1,  1), float3( 2, 0,  0), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, specTexSize, float3( 1,  1, -1), float3(-2, 0,  0), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
		} break;
	}

	// update one face of one row per frame
	specularTexIter += 1;
	specularTexIter %= (specTexSize * 3);
}

void CubeMapHandler::CreateSpecularFacePart(
	unsigned int texType,
	unsigned int size,
	const float3& cdir,
	const float3& xdif,
	const float3& ydif,
	unsigned int y,
	unsigned char* buf
) {
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& sky = ISky::GetSky();
	// TODO move to a shader
	for (int x = 0; x < size; ++x) {
		const float3 dir = (cdir + (xdif * (x + 0.5f)) / size + (ydif * (y + 0.5f)) / size).Normalize();
		const float dot  = std::max(0.0f, dir.dot(sky->GetLight()->GetLightDir()));
		const float spec = std::min(1.0f, std::pow(dot, sunLighting->specularExponent) + std::pow(dot, 3.0f) * 0.25f);

		buf[x * 4 + 0] = (sunLighting->modelSpecularColor.x * spec * 255);
		buf[x * 4 + 1] = (sunLighting->modelSpecularColor.y * spec * 255);
		buf[x * 4 + 2] = (sunLighting->modelSpecularColor.z * spec * 255);
		buf[x * 4 + 3] = 255;
	}
}

void CubeMapHandler::CreateSpecularFace(
	unsigned int texType,
	unsigned int size,
	const float3& cdir,
	const float3& xdif,
	const float3& ydif
) {
	RECOIL_DETAILED_TRACY_ZONE;
	for (int y = 0; y < size; ++y) {
		CreateSpecularFacePart(texType, size, cdir, xdif, ydif, y, &specTexFaceBuf[y * size * 4]);
	}

	//! note: no mipmaps, cubemap linear filtering is broken
	glTexImage2D(texType, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, &specTexFaceBuf[0]);
}

void CubeMapHandler::UpdateSpecularFace(
	unsigned int texType,
	unsigned int size,
	const float3& cdir,
	const float3& xdif,
	const float3& ydif,
	unsigned int y,
	unsigned char* buf
) {
	RECOIL_DETAILED_TRACY_ZONE;
	CreateSpecularFacePart(texType, size, cdir, xdif, ydif, y, buf);

	glTexSubImage2D(texType, 0, 0, y, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

