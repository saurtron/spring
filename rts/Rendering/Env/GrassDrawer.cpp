/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cmath>

#include "GrassDrawer.h"
#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Map/Ground.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/FBO.h"
#include "Rendering/Map/InfoTexture/IInfoTextureHandler.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#include "Sim/Misc/Wind.h"
#include "System/EventHandler.h"
#include "System/GlobalRNG.h"
#include "System/SpringMath.h"
#include "System/Config/ConfigHandler.h"
#include "System/Color.h"
#include "System/Exceptions.h"
#include "System/StringUtil.h"
#include "System/Threading/ThreadPool.h"
#include "System/TimeProfiler.h"
#include "System/Log/ILog.h"
#include "System/FileSystem/FileHandler.h"

#include "System/Misc/TracyDefs.h"

CONFIG(int, GrassDetail).defaultValue(7).headlessValue(0).minimumValue(0).description("Sets how detailed the engine rendered grass will be on any given map.");

// uses a 'synced' RNG s.t. grass turfs generated from the same
// seed also share identical sequences, otherwise an unpleasant
// shimmering effect occurs when zooming
#if 0
typedef CGlobalRNG<LCG16, true> GrassRNG;
#else
typedef CGlobalRNG<PCG32, true> GrassRNG;
#endif

static constexpr float turfSize        = 20.0f;            // single turf size
static constexpr float partTurfSize    = turfSize * 1.0f;  // single turf size
static constexpr int   grassSquareSize = 4;                // mapsquares per grass square
static constexpr int   grassBlockSize  = 4;                // grass squares per grass block
static constexpr int   blockMapSize    = grassSquareSize * grassBlockSize;

static constexpr int   GSSSQ = SQUARE_SIZE * grassSquareSize;
static constexpr int   BMSSQ = SQUARE_SIZE * blockMapSize;

static GrassRNG grng;



static float GetGrassBlockCamDist(const int x, const int y, const bool square = false)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float qx = x * GSSSQ;
	const float qz = y * GSSSQ;
	const float3 mid = float3(qx, CGround::GetHeightReal(qx, qz, false), qz);
	const float3 dif = camera->GetPos() - mid;
	return (square) ? dif.SqLength() : dif.Length();
}


static const bool GrassSort(const CGrassDrawer::GrassStruct* a, const CGrassDrawer::GrassStruct* b) {
	RECOIL_DETAILED_TRACY_ZONE;
	const float distA = GetGrassBlockCamDist((a->posX + 0.5f) * grassBlockSize, (a->posZ + 0.5f) * grassBlockSize, true);
	const float distB = GetGrassBlockCamDist((b->posX + 0.5f) * grassBlockSize, (b->posZ + 0.5f) * grassBlockSize, true);
	return (distA > distB);
}

static const bool GrassSortNear(const CGrassDrawer::InviewNearGrass& a, const CGrassDrawer::InviewNearGrass& b) {
	RECOIL_DETAILED_TRACY_ZONE;
	return (a.dist > b.dist);
}



//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/// CGrassBlockDrawer

class CGrassBlockDrawer: public CReadMap::IQuadDrawer
{
public:
	std::vector<CGrassDrawer::InviewNearGrass> inviewGrass;
	std::vector<CGrassDrawer::InviewNearGrass> inviewNearGrass;
	std::vector<CGrassDrawer::GrassStruct*>    inviewFarGrass;
	int cx, cy;
	CGrassDrawer* gd;

	void ResetState() {
		inviewGrass.clear();
		inviewNearGrass.clear();
		inviewFarGrass.clear();

		cx = 0;
		cy = 0;

		gd = nullptr;
	}

	void DrawQuad(int x, int y) {
		const float distSq = GetGrassBlockCamDist((x + 0.5f) * grassBlockSize, (y + 0.5f) * grassBlockSize, true);

		if (distSq > Square(gd->maxGrassDist))
			return;

		if (abs(x - cx) <= gd->detailedBlocks && abs(y - cy) <= gd->detailedBlocks)
			return DrawDetailQuad(x, y);

		DrawFarQuad(x, y);
	}

private:
	void DrawDetailQuad(const int x, const int y) {
		const float maxDetailedDist = gd->maxDetailedDist;

		// blocks close to the camera
		for (int y2 = y * grassBlockSize; y2 < (y + 1) * grassBlockSize; ++y2) {
			for (int x2 = x * grassBlockSize; x2 < (x + 1) * grassBlockSize; ++x2) {
				if (!gd->grassMap[y2 * mapDims.mapx / grassSquareSize + x2])
					continue;

				grng.Seed(y2 * mapDims.mapx / grassSquareSize + x2);

				const float dist  = GetGrassBlockCamDist(x2, y2, false);
				const float rdist = 1.0f + grng.NextFloat() * 0.5f;

				//TODO instead of adding grass turfs depending on their distance to the camera,
				//     there should be a fixed sized pool for mesh & billboard turfs
				//     and then we fill these pools with _preference_ for close distance turfs.
				//     So when a map has only less turfs, render them independent of the cam distance as mesh.
				//     -> see Ravaged_2
				if (dist < (maxDetailedDist + 128.f * rdist)) {
					// close grass (render as mesh)
					CGrassDrawer::InviewNearGrass iv;
					iv.dist = dist;
					iv.x = x2;
					iv.y = y2;
					inviewGrass.push_back(iv);
				}

				if (dist > maxDetailedDist) {
					// near but not close, save for later drawing
					CGrassDrawer::InviewNearGrass iv;
					iv.dist = dist;
					iv.x = x2;
					iv.y = y2;
					inviewNearGrass.push_back(iv);
				}
			}
		}
	}

	void DrawFarQuad(const int x, const int y) {
		const int curSquare = y * gd->blocksX + x;
		CGrassDrawer::GrassStruct* grass = &gd->grass[curSquare];
		grass->lastSeen = globalRendering->drawFrame;
		grass->posX = x;
		grass->posZ = y;
		inviewFarGrass.push_back(grass);
	}
};



static CGrassBlockDrawer blockDrawer;

// managed by WorldDrawer
CGrassDrawer* grassDrawer = nullptr;



CGrassDrawer::CGrassDrawer()
: CEventClient("[GrassDrawer]", 199992, false)
, blocksX(mapDims.mapx / grassSquareSize / grassBlockSize)
, blocksY(mapDims.mapy / grassSquareSize / grassBlockSize)
, grassDL(0)
, grassBladeTex(0)
, farTex(0)
, farnearVA(2048)
, grassOff(false)
, updateBillboards(false)
, updateVisibility(false)
{
	blockDrawer.ResetState();
	grng.Seed(15);

	const int detail = configHandler->GetInt("GrassDetail");

	// load grass density from map
	{
		MapBitmapInfo grassbm;
		unsigned char* grassdata = readMap->GetInfoMap("grass", &grassbm);
		if (grassdata == nullptr) {
			grassOff = true;
			return;
		}

		if (grassbm.width != mapDims.mapx / grassSquareSize || grassbm.height != mapDims.mapy / grassSquareSize) {
			char b[128];
			SNPRINTF(b, sizeof(b), "grass-map has wrong size (%dx%d, should be %dx%d)\n",
				grassbm.width, grassbm.height, mapDims.mapx / grassSquareSize, mapDims.mapy / grassSquareSize);
			throw std::runtime_error(b);
		}

		grassMap.resize(mapDims.mapx * mapDims.mapy / (grassSquareSize * grassSquareSize));
		memcpy(grassMap.data(), grassdata, grassMap.size());
		readMap->FreeInfoMap("grass", grassdata);

		// some ATI drivers crash with grass enabled, default to disabled
		if ((detail == 0) || ((detail == 7) && globalRendering->haveAMD)) {
			grassOff = true;
			return;
		}

		// needed to create the far tex
		if (!GLEW_EXT_framebuffer_blit) {
			grassOff = true;
			return;
		}

	}

	// create/load blade texture
	{
		CBitmap grassBladeTexBM;
		if (!grassBladeTexBM.Load(mapInfo->grass.bladeTexName)) {
			// map didn't define a grasstex, so generate one
			grassBladeTexBM.Alloc(256, 64, 4);

			for (int a = 0; a < 16; ++a) {
				CreateGrassBladeTex(&grassBladeTexBM.GetRawMem()[a * 16 * 4]);
			}
		}
		//grassBladeTexBM.Save("blade.png", false);
		grassBladeTex = grassBladeTexBM.CreateMipMapTexture();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	// create shaders and finalize
	grass.resize(blocksX * blocksY);
	farnearVA.Initialize();
	grassDL = glGenLists(1);

	ChangeDetail(detail);
	LoadGrassShaders();
	configHandler->NotifyOnChange(this, {"GrassDetail"});

	// eventclient
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
}

CGrassDrawer::~CGrassDrawer()
{
	eventHandler.RemoveClient(this);
	configHandler->RemoveObserver(this);

	glDeleteLists(grassDL, 1);
	glDeleteTextures(1, &grassBladeTex);
	glDeleteTextures(1, &farTex);
	shaderHandler->ReleaseProgramObjects("[GrassDrawer]");
}



void CGrassDrawer::ChangeDetail(int detail) {
	RECOIL_DETAILED_TRACY_ZONE;
	// TODO: get rid of the magic constants
	const int detail_lim = std::min(3, detail);
	maxGrassDist = 800 + std::sqrt((float) detail) * 240;
	maxDetailedDist = 146 + detail * 24;
	detailedBlocks = int((maxDetailedDist + 128.f * 1.5f) / BMSSQ) + 1;
	numTurfs = 3 + int(detail_lim * 0.5f);
	strawPerTurf = std::min(50 + int(std::sqrt((float)detail_lim) * 10), mapInfo->grass.maxStrawsPerTurf);

	// recreate textures & XBOs
	CreateGrassDispList(grassDL);
	CreateFarTex();

	// reset  all cached blocks
	for (GrassStruct& pGS: grass) {
		ResetPos(pGS.posX, pGS.posZ);
	}
}


void CGrassDrawer::ConfigNotify(const std::string& key, const std::string& value) {
	RECOIL_DETAILED_TRACY_ZONE;
	ChangeDetail(configHandler->GetInt("GrassDetail"));
}


void CGrassDrawer::LoadGrassShaders() {
	RECOIL_DETAILED_TRACY_ZONE;
	#define sh shaderHandler
	grassShaders.resize(GRASS_PROGRAM_LAST, nullptr);

	static const std::string shaderNames[GRASS_PROGRAM_LAST] = {
		"grassNearAdvShader",
		"grassDistAdvShader",
		"grassShadGenShader"
	};
	static const std::string shaderDefines[GRASS_PROGRAM_LAST] = {
		"#define DISTANCE_NEAR\n",
		"#define DISTANCE_FAR\n",
		"#define SHADOW_GEN\n"
	};

	for (int i = 0; i < GRASS_PROGRAM_LAST; i++) {
		grassShaders[i] = sh->CreateProgramObject("[GrassDrawer]", shaderNames[i] + "GLSL");
		grassShaders[i]->AttachShaderObject(sh->CreateShaderObject("GLSL/GrassVertProg.glsl", shaderDefines[i], GL_VERTEX_SHADER));
		grassShaders[i]->AttachShaderObject(sh->CreateShaderObject("GLSL/GrassFragProg.glsl", shaderDefines[i], GL_FRAGMENT_SHADER));
		grassShaders[i]->Link();

		grassShaders[i]->Enable();
		grassShaders[i]->SetUniform("mapSizePO2", 1.0f / (mapDims.pwr2mapx * SQUARE_SIZE), 1.0f / (mapDims.pwr2mapy * SQUARE_SIZE));
		grassShaders[i]->SetUniform("mapSize",    1.0f / (mapDims.mapx     * SQUARE_SIZE), 1.0f / (mapDims.mapy     * SQUARE_SIZE));
		grassShaders[i]->SetUniform("bladeTex",        0);
		grassShaders[i]->SetUniform("grassShadingTex", 1);
		grassShaders[i]->SetUniform("shadingTex",      2);
		grassShaders[i]->SetUniform("infoMap",         3);
		grassShaders[i]->SetUniform("shadowMap",       4);
		grassShaders[i]->SetUniform("specularTex",     5);
		grassShaders[i]->SetUniform("shadowColorTex",  6);
		grassShaders[i]->SetUniform("infoTexIntensityMul", 1.0f);
		grassShaders[i]->SetUniform("groundShadowDensity", sunLighting->groundShadowDensity);
		grassShaders[i]->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());
		grassShaders[i]->SetUniform4v("shadowParams", &shadowHandler.GetShadowParams().x);
		grassShaders[i]->Disable();
		grassShaders[i]->Validate();

		if ((grassOff = !grassShaders[i]->IsValid()))
			break;
	}

	#undef sh
}


void CGrassDrawer::EnableShader(const GrassShaderProgram type) {
	RECOIL_DETAILED_TRACY_ZONE;
	const float3 windSpeed = envResHandler.GetCurrentWindVec() * mapInfo->grass.bladeWaveScale;

	grassShader = grassShaders[type];
	grassShader->SetFlag("HAVE_INFOTEX", infoTextureHandler->IsEnabled());
	grassShader->SetFlag("HAVE_SHADOWS", shadowHandler.ShadowsLoaded());
	grassShader->Enable();

	grassShader->SetUniform("frame", gs->frameNum + globalRendering->timeOffset);
	grassShader->SetUniform3v("windSpeed", &windSpeed.x);
	grassShader->SetUniform3v("camPos",    &camera->GetPos().x);
	grassShader->SetUniform3v("camDir",    &camera->GetDir().x);
	grassShader->SetUniform3v("camUp",     &camera->GetUp().x);
	grassShader->SetUniform3v("camRight",  &camera->GetRight().x);

	grassShader->SetUniform("infoTexIntensityMul", float(infoTextureHandler->InMetalMode()) + 1.0f);
	grassShader->SetUniform("groundShadowDensity", sunLighting->groundShadowDensity);
	grassShader->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());
	grassShader->SetUniform4v("shadowParams", &shadowHandler.GetShadowParams().x);

	grassShader->SetUniform3v("ambientLightColor",  &sunLighting->modelAmbientColor.x);
	grassShader->SetUniform3v("diffuseLightColor",  &sunLighting->modelDiffuseColor.x);
	grassShader->SetUniform3v("specularLightColor", &sunLighting->modelSpecularColor.x);
	grassShader->SetUniform3v("sunDir",             &mapInfo->light.sunDir.x);
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

static float3 GetTurfParams(GrassRNG& rng, const int x, const int y)
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 result;
	result.x = (x + rng.NextFloat()) * GSSSQ;
	result.y = (y + rng.NextFloat()) * GSSSQ;
	result.z = rng.NextFloat() * 360.0f; // rotation
	return result;
}



void CGrassDrawer::DrawNear(const std::vector<InviewNearGrass>& inviewGrass)
{
	RECOIL_DETAILED_TRACY_ZONE;
	for (const InviewNearGrass& g: inviewGrass) {
		grng.Seed(g.y * mapDims.mapx / grassSquareSize + g.x);

//		const float distSq = GetGrassBlockCamDist(g.x, g.y, true);
		const float rdist  = 1.0f + grng.NextFloat() * 0.5f;
		const float alpha  = linearstep(maxDetailedDist, maxDetailedDist + 128.0f * rdist, g.dist);

		for (int a = 0; a < numTurfs; a++) {
			const float3& p = GetTurfParams(grng, g.x, g.y);
			float3 pos(p.x, CGround::GetHeightReal(p.x, p.y, false), p.y);

			pos.y -= CGround::GetSlope(p.x, p.y, false) * 30.0f;
			pos.y -= 2.0f * mapInfo->grass.bladeHeight * alpha;

			glPushMatrix();
			glTranslatef3(pos);
			glRotatef(p.z, 0.0f, 1.0f, 0.0f);
			glCallList(grassDL);
			glPopMatrix();
		}
	}
}


void CGrassDrawer::DrawBillboard(const int x, const int y, const float dist, VA_TYPE_TN* va_tn)
{
	RECOIL_DETAILED_TRACY_ZONE;
	GrassRNG trng; // need our own, this function may run threaded
	trng.Seed(y * mapDims.mapx / grassSquareSize + x);

	const float rDist = 1.0f + trng.NextFloat() * 0.5f;
	const float gStep = linearstep(maxGrassDist,  maxGrassDist + 127.0f, dist + 128.0f);
	const float dStep = linearstep(maxDetailedDist, maxDetailedDist + 128.0f * rDist, dist);
	const float alpha = std::min(1.0f - gStep, dStep);

	for (int a = 0; a < numTurfs; a++) {
		const float3& p = GetTurfParams(trng, x, y);
		const float3 pos(p.x, CGround::GetHeightReal(p.x, p.y, false) - CGround::GetSlope(p.x, p.y, false) * 30.0f, p.y);

		va_tn[a * 4 + 0] = { pos,         0.0f, 1.0f, float3(-partTurfSize, -partTurfSize, alpha) };
		va_tn[a * 4 + 1] = { pos, 1.0f / 16.0f, 1.0f, float3( partTurfSize, -partTurfSize, alpha) };
		va_tn[a * 4 + 2] = { pos, 1.0f / 16.0f, 0.0f, float3( partTurfSize,  partTurfSize, alpha) };
		va_tn[a * 4 + 3] = { pos,         0.0f, 0.0f, float3(-partTurfSize,  partTurfSize, alpha) };
	}
}


void CGrassDrawer::DrawFarBillboards(const std::vector<GrassStruct*>& inviewFarGrass)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// update far grass blocks
	if (updateBillboards) {
		updateBillboards = false;

		for_mt(0, inviewFarGrass.size(), [&](const int i) {
			GrassStruct& g = *inviewFarGrass[i];

			if (g.lastFar == 0) {
				// TODO: VA's need to be uploaded each frame, switch to VBO's
				// force the patch-quads to be recreated
				g.lastFar = globalRendering->drawFrame;
				g.lastDist = -1.0f;
			}

			const float distSq = GetGrassBlockCamDist((g.posX + 0.5f) * grassBlockSize, (g.posZ + 0.5f) * grassBlockSize, true);

			if (distSq == g.lastDist)
				return;

			const bool inAlphaRange1 = (    distSq < Square(maxDetailedDist + 128.0f * 1.5f)) || (    distSq > Square(maxGrassDist - 128.0f));
			const bool inAlphaRange2 = (g.lastDist < Square(maxDetailedDist + 128.0f * 1.5f)) || (g.lastDist > Square(maxGrassDist - 128.0f));

			if (!inAlphaRange1 && (inAlphaRange1 == inAlphaRange2))
				return;

			g.lastDist = distSq;
			CVertexArray* va = &g.va;
			va->Initialize();

			// (4*4)*numTurfs quads
			for (int y2 = g.posZ * grassBlockSize; y2 < (g.posZ + 1) * grassBlockSize; ++y2) {
				for (int x2 = g.posX * grassBlockSize; x2 < (g.posX  + 1) * grassBlockSize; ++x2) {
					if (!grassMap[y2 * mapDims.mapx / grassSquareSize + x2])
						continue;

					const float dist = GetGrassBlockCamDist(x2, y2);
					auto* va_tn = va->GetTypedVertexArray<VA_TYPE_TN>(numTurfs * 4);
					DrawBillboard(x2, y2, dist, va_tn);
				}
			}
		});
	}

	// render far grass blocks
	for (GrassStruct* g: inviewFarGrass) {
		g->va.DrawArrayTN(GL_QUADS);
	}
}


void CGrassDrawer::DrawNearBillboards(const std::vector<InviewNearGrass>& inviewNearGrass)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (farnearVA.drawIndex() == 0) {
		auto* va_tn = farnearVA.GetTypedVertexArray<VA_TYPE_TN>(inviewNearGrass.size() * numTurfs * 4);

		for_mt(0, inviewNearGrass.size(), [&](const int i) {
			const InviewNearGrass& gi = inviewNearGrass[i];
			DrawBillboard(gi.x, gi.y, gi.dist, &va_tn[i * numTurfs * 4]);
		});
	}

	farnearVA.DrawArrayTN(GL_QUADS);
}


void CGrassDrawer::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// grass is never drawn in any special (non-opaque) pass
	const CCamera* cam = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);

	// update visible turfs
	updateVisibility |= (oldCamPos != cam->GetPos());
	updateVisibility |= (oldCamDir != cam->GetDir());

	if (updateVisibility) {
		oldCamPos = cam->GetPos();
		oldCamDir = cam->GetDir();
		lastVisibilityUpdate = globalRendering->drawFrame;

		blockDrawer.ResetState();
		blockDrawer.cx = int(cam->GetPos().x / BMSSQ);
		blockDrawer.cy = int(cam->GetPos().z / BMSSQ);
		blockDrawer.gd = this;
		readMap->GridVisibility(nullptr, &blockDrawer, maxGrassDist, blockMapSize);

		// ATI crashes w/o an error when shadows are enabled!?
       const bool shadows = (shadowHandler.ShadowsLoaded() && globalRendering->amdHacks);

		if (!shadows) {
			std::sort(blockDrawer.inviewFarGrass.begin(), blockDrawer.inviewFarGrass.end(), GrassSort);
			std::sort(blockDrawer.inviewNearGrass.begin(), blockDrawer.inviewNearGrass.end(), GrassSortNear);
			farnearVA.Initialize();
			updateBillboards = true;
		}

		updateVisibility = false;
	}

	// collect garbage
	//   originally, this deleted the billboard VA of any patch that was not drawn for 50 frames
	//   now it only resets lastFar s.t. patches are forcibly recreated when they become visible
	//   again (reusing memory)
	//   pass negative coordinates since we do not want to set updateVisibility during this step
	for (const GrassStruct& gs: grass) {
		if ((gs.lastSeen != lastVisibilityUpdate) && (gs.lastSeen < globalRendering->drawFrame - 50) && gs.lastFar != 0) {
			ResetPos(-gs.posX, -gs.posZ);
		}
	}
}


void CGrassDrawer::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassOff || !readMap->GetGrassShadingTexture())
		return;

	glPushAttrib(GL_CURRENT_BIT);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	if (!blockDrawer.inviewGrass.empty()) {
		SetupGlStateNear();
			DrawNear(blockDrawer.inviewGrass);
		ResetGlStateNear();
	}

	// ATI crashes w/o an error when shadows are enabled!?
    const bool shadows = (shadowHandler.ShadowsLoaded() && globalRendering->amdHacks);

	if (!shadows && (!blockDrawer.inviewFarGrass.empty() || !blockDrawer.inviewNearGrass.empty())) {
		SetupGlStateFar();
			DrawFarBillboards(blockDrawer.inviewFarGrass);
			DrawNearBillboards(blockDrawer.inviewNearGrass);
		ResetGlStateFar();
	}

	glPopAttrib();
}


void CGrassDrawer::DrawShadow()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Grass self-shadowing doesn't look that good atm
/*	if (grassOff || !readMap->GetGrassShadingTexture())
		return;

	// looks ad with low density grass
	//TODO either enable it on high density only, or wait for alpha transparent shadows and use those then
	EnableShader(GRASS_PROGRAM_SHADOW_GEN);

	glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, activeFarTex);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);

	glPolygonOffset(5, 15);
	glEnable(GL_POLYGON_OFFSET_FILL);

	// we pass it as uniform and want to have pos & rot
	// of the turfs to be saved alone in the modelview matrix
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	CCamera* cam = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);

	static CGrassBlockDrawer blockDrawer;
	blockDrawer.ResetState();
	blockDrawer.cx = int(cam->GetPos().x / BMSSQ);
	blockDrawer.cy = int(cam->GetPos().z / BMSSQ);
	blockDrawer.gd = this;

	readMap->GridVisibility(nullptr, &drawer, maxGrassDist, blockMapSize);
	DrawNear(blockDrawer.inviewGrass);

	//FIXME needs own shader!
	//DrawNearBillboards(blockDrawer.inviewNearGrass);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glEnable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);

	grassShader->Disable();*/
}


void CGrassDrawer::SetupGlStateNear()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// bind textures
	{
		glActiveTextureARB(GL_TEXTURE0_ARB);
			glBindTexture(GL_TEXTURE_2D, grassBladeTex);
		glActiveTextureARB(GL_TEXTURE1_ARB);
			glBindTexture(GL_TEXTURE_2D, readMap->GetGrassShadingTexture());
		glActiveTextureARB(GL_TEXTURE2_ARB);
			glBindTexture(GL_TEXTURE_2D, readMap->GetShadingTexture());
		glActiveTextureARB(GL_TEXTURE3_ARB);
			glBindTexture(GL_TEXTURE_2D, infoTextureHandler->GetCurrentInfoTexture());
		glActiveTextureARB(GL_TEXTURE5_ARB);
			glBindTexture(GL_TEXTURE_CUBE_MAP_ARB, cubeMapHandler.GetSpecularTextureID());
	}

	// bind shader
	EnableShader(GRASS_PROGRAM_NEAR);

	if (shadowHandler.ShadowsLoaded()) {
		shadowHandler.SetupShadowTexSampler(GL_TEXTURE4);
		glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, shadowHandler.GetColorTextureID());
	}

	glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glMultMatrixf(camera->GetViewMatrix());
	glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDepthMask(GL_TRUE);
	const auto& sky = ISky::GetSky();
	sky->SetupFog();
}


void CGrassDrawer::ResetGlStateNear()
{
	RECOIL_DETAILED_TRACY_ZONE;
	//CBaseGroundDrawer* gd = readMap->GetGroundDrawer();

	grassShader->Disable();

	if (shadowHandler.ShadowsLoaded()) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE_ARB, GL_NONE);
			glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
}


void CGrassDrawer::SetupGlStateFar()
{
	RECOIL_DETAILED_TRACY_ZONE;
	//glEnable(GL_ALPHA_TEST);
	//glAlphaFunc(GL_GREATER, 0.01f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glMultMatrixf(camera->GetViewMatrix());
	glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

	EnableShader(GRASS_PROGRAM_DIST);

	glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D, farTex);
	glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_2D, readMap->GetGrassShadingTexture());
	glActiveTextureARB(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_2D, readMap->GetShadingTexture());
	glActiveTextureARB(GL_TEXTURE3_ARB);
		glBindTexture(GL_TEXTURE_2D, infoTextureHandler->GetCurrentInfoTexture());

	if (shadowHandler.ShadowsLoaded()) {
		shadowHandler.SetupShadowTexSampler(GL_TEXTURE4);
		glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, shadowHandler.GetColorTextureID());
	}

	glActiveTextureARB(GL_TEXTURE0_ARB);
}


void CGrassDrawer::ResetGlStateFar()
{
	RECOIL_DETAILED_TRACY_ZONE;
	grassShader->Disable();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	if (shadowHandler.ShadowsLoaded()) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE_ARB, GL_NONE);
			glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_ALPHA_TEST);

}


void CGrassDrawer::CreateGrassDispList(int listNum)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CVertexArray* va = GetVertexArray();
	va->Initialize();
	grng.Seed(15);

	for (int a = 0; a < strawPerTurf; ++a) {
		// draw a single blade
		const float lngRnd = grng.NextFloat();
		const float length = mapInfo->grass.bladeHeight * (1.0f + lngRnd);
		const float maxAng = mapInfo->grass.bladeAngle * std::max(grng.NextFloat(), 1.0f - smoothstep(0.0f, 1.0f, lngRnd));

		float3 sideVect;
		sideVect.x = grng.NextFloat() - 0.5f;
		sideVect.z = grng.NextFloat() - 0.5f;
		sideVect.ANormalize();
		float3 bendVect = sideVect.cross(UpVector); // direction to bend into
		sideVect *= mapInfo->grass.bladeWidth * (-0.15f * lngRnd + 1.0f);

		const float3 basePos = grng.NextVector2D() * (turfSize - (bendVect * std::sin(maxAng) * length).Length2D());

		// select one of the 16 color shadings
		const float xtexCoord = grng.NextInt(16) / 16.0f;
		const int numSections = 2 + int(maxAng * 1.2f + length * 0.2f);

		float3 normalBend = -bendVect;

		// start btm
		va->AddVertexTN(basePos + sideVect - float3(0.0f, 3.0f, 0.0f), xtexCoord              , 0.f, normalBend);
		va->AddVertexTN(basePos - sideVect - float3(0.0f, 3.0f, 0.0f), xtexCoord + (1.0f / 16), 0.f, normalBend);

		for (float h = 0.0f; h < 1.0f; h += (1.0f / numSections)) {
			const float ang = maxAng * h;
			const float3 n = (normalBend * std::cos(ang) + UpVector * std::sin(ang)).ANormalize();
			const float3 edgePos  = (UpVector * std::cos(ang) + bendVect * std::sin(ang)) * length * h;
			const float3 edgePosL = edgePos - sideVect * (1.0f - h);
			const float3 edgePosR = edgePos + sideVect * (1.0f - h);

			va->AddVertexTN(basePos + edgePosR, xtexCoord + (1.0f / 32) * h              , h, (n + sideVect * 0.04f).ANormalize());
			va->AddVertexTN(basePos + edgePosL, xtexCoord - (1.0f / 32) * h + (1.0f / 16), h, (n - sideVect * 0.04f).ANormalize());
		}

		// end top tip (single triangle)
		const float3 edgePos = (UpVector * std::cos(maxAng) + bendVect * std::sin(maxAng)) * length;
		const float3 n = (normalBend * std::cos(maxAng) + UpVector * std::sin(maxAng)).ANormalize();
		va->AddVertexTN(basePos + edgePos, xtexCoord + (1.0f / 32), 1.0f, n);

		// next blade
		va->EndStrip();
	}

	glNewList(listNum, GL_COMPILE);
	va->DrawArrayTN(GL_TRIANGLE_STRIP);
	glEndList();
}

void CGrassDrawer::CreateGrassBladeTex(unsigned char* buf)
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 redish = float3(0.95f, 0.70f, 0.4f);
	float3 col = mix(mapInfo->grass.color, redish, 0.1f * grng.NextFloat());
	col.x = std::clamp(col.x, 0.f, 1.f);
	col.y = std::clamp(col.y, 0.f, 1.f);
	col.z = std::clamp(col.z, 0.f, 1.f);

	SColor* img = reinterpret_cast<SColor*>(buf);
	for (int y=0; y<64; ++y) {
		for (int x=0; x<16; ++x) {
			const float brightness = smoothstep(-0.8f, 0.5f, y/63.0f) + ((x%2) == 0 ? 0.035f : 0.0f);
			const float3 c = col * brightness;
			img[y*256+x] = SColor(c.r, c.g, c.b, 1.0f);
		}
	}
}

void CGrassDrawer::CreateFarTex()
{
	RECOIL_DETAILED_TRACY_ZONE;
	//TODO create normalmap, too?
	const int sizeMod = 2;
	const int billboardSize = 256;
	const int numAngles = 16;
	const int texSizeX = billboardSize * numAngles;
	const int texSizeY = billboardSize;

	if (farTex == 0) {
		glGenTextures(1, &farTex);
		glBindTexture(GL_TEXTURE_2D, farTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		RecoilTexStorage2D(GL_TEXTURE_2D, -1, GL_RGBA8, texSizeX, texSizeY);
	}

	FBO fboTex;
	fboTex.Bind();
	fboTex.AttachTexture(farTex);
	fboTex.CheckStatus("GRASSDRAWER1");

	GLenum depthFormat = static_cast<GLenum>(CGlobalRendering::DepthBitsToFormat(globalRendering->supportDepthBufferBitDepth));

	FBO fbo;
	fbo.Bind();
	fbo.CreateRenderBuffer(GL_DEPTH_ATTACHMENT_EXT, depthFormat, texSizeX * sizeMod, texSizeY * sizeMod);
	fbo.CreateRenderBuffer(GL_COLOR_ATTACHMENT0_EXT, GL_RGBA8, texSizeX * sizeMod, texSizeY * sizeMod);
	fbo.CheckStatus("GRASSDRAWER2");

	if (!fboTex.IsValid() || !fbo.IsValid()) {
		grassOff = true;
		return;
	}

	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();

	glDisable(GL_FOG);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glBindTexture(GL_TEXTURE_2D, grassBladeTex);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CLIP_PLANE0);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glColor4f(1,1,1,1);

	glViewport(0,0,texSizeX*sizeMod, texSizeY*sizeMod);
	glClearColor(mapInfo->grass.color.r,mapInfo->grass.color.g,mapInfo->grass.color.b,0.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.f,0.f,0.f,0.f);

	static const GLdouble eq[4] = {0.f, 1.f, 0.f, 0.f};

	// render turf from different vertical angles
	for (int a=0;a<numAngles;++a) {
		glViewport(a*billboardSize*sizeMod, 0, billboardSize*sizeMod, billboardSize*sizeMod);
		glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glRotatef(a*90.f/(numAngles-1),1,0,0);
			//glTranslatef(0,-0.5f,0);
		glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(-partTurfSize, partTurfSize, partTurfSize, -partTurfSize, -turfSize, turfSize);

		// has to be applied after the matrix transformations,
		// cause it uses those an `compiles` them into the clip plane
		glClipPlane(GL_CLIP_PLANE0, &eq[0]);

		glCallList(grassDL);
	}

	glDisable(GL_CLIP_PLANE0);

	// scale down the rendered fartextures (MSAA) and write to the final texture
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER, fbo.fboId);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, fboTex.fboId);
	glBlitFramebufferEXT(0, 0, texSizeX*sizeMod, texSizeY*sizeMod,
		0, 0, texSizeX, texSizeY,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);

	// compute mipmaps
	glBindTexture(GL_TEXTURE_2D, farTex);
	glGenerateMipmap(GL_TEXTURE_2D);

	// blur non-rendered areas, so in mipmaps color data isn't blurred with background color
	{
		const int mipLevels = std::ceil(std::log((float)(std::max(texSizeX, texSizeY) + 1)));

		glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
			glLoadIdentity();

		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, GL_ZERO, GL_DST_ALPHA);

		// copy each mipmap to its predecessor background
		// -> fill background with blurred color data
		fboTex.Bind();
		for (int mipLevel = mipLevels - 2; mipLevel >= 0; --mipLevel) {
			fboTex.AttachTexture(farTex, GL_TEXTURE_2D, GL_COLOR_ATTACHMENT0_EXT, mipLevel);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, mipLevel + 1.f);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, mipLevel + 1.f);
			glViewport(0, 0, texSizeX>>mipLevel, texSizeY>>mipLevel);

			CVertexArray* va = GetVertexArray();
			va->Initialize();
				va->AddVertexT(float3(-1.0f,  1.0f, 0.0f), 0.0f, 1.0f);
				va->AddVertexT(float3( 1.0f,  1.0f, 0.0f), 1.0f, 1.0f);
				va->AddVertexT(float3( 1.0f, -1.0f, 0.0f), 1.0f, 0.0f);
				va->AddVertexT(float3(-1.0f, -1.0f, 0.0f), 0.0f, 0.0f);
			va->DrawArrayT(GL_QUADS);
		}

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// recreate mipmaps from now blurred base level
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, -1000.f);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD,  1000.f);
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	globalRendering->LoadViewport();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	FBO::Unbind();
	//glSaveTexture(farTex, "grassfar.png");
}


void CGrassDrawer::ResetPos(const int grassBlockX, const int grassBlockZ)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassOff)
		return;

	// negative coors are passed during "garbage-collection" resets
	const int gbx = std::abs(grassBlockX);
	const int gbz = std::abs(grassBlockZ);

	assert(gbx < blocksX);
	assert(gbz < blocksY);

	grass[gbz * blocksX + gbx].lastFar = 0;

	updateBillboards = true;
	updateVisibility = (grassBlockX >= 0 && grassBlockZ >= 0);
}


void CGrassDrawer::ResetPos(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	ResetPos(pos.x / BMSSQ, pos.z / BMSSQ);
}


void CGrassDrawer::AddGrass(const float3& pos, const uint8_t grassValue)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassMap.empty())
		return;

	const int x = int(pos.x) / GSSSQ;
	const int z = int(pos.z) / GSSSQ;
	assert(x >= 0 && x < (mapDims.mapx / grassSquareSize));
	assert(z >= 0 && z < (mapDims.mapy / grassSquareSize));

	grassMap[z * mapDims.mapx / grassSquareSize + x] = grassValue;
	ResetPos(pos);
}

void CGrassDrawer::RemoveGrass(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassMap.empty())
		return;

	const int x = int(pos.x) / GSSSQ;
	const int z = int(pos.z) / GSSSQ;
	assert(x >= 0 && x < (mapDims.mapx / grassSquareSize));
	assert(z >= 0 && z < (mapDims.mapy / grassSquareSize));

	grassMap[z * mapDims.mapx / grassSquareSize + x] = 0;
	ResetPos(pos);
}

unsigned char CGrassDrawer::GetGrass(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassMap.empty())
		return -1;

	const int x = int(pos.x) / GSSSQ;
	const int z = int(pos.z) / GSSSQ;
	assert(x >= 0 && x < (mapDims.mapx / grassSquareSize));
	assert(z >= 0 && z < (mapDims.mapy / grassSquareSize));

	return grassMap[z * mapDims.mapx / grassSquareSize + x];
}


void CGrassDrawer::UnsyncedHeightMapUpdate(const SRectangle& rect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	for (int z = rect.z1; z <= rect.z2; ++z) {
		for (int x = rect.x1; x <= rect.x2; ++x) {
			ResetPos(float3(x, 0.f, z));
		}
	}
}


