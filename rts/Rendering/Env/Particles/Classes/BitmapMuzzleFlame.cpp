/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "BitmapMuzzleFlame.h"

#include "Sim/Misc/GlobalSynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/ColorMap.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "Sim/Projectiles/ExpGenSpawnableMemberInfo.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "System/creg/DefTypes.h"
#include "System/SpringMath.h"

#include "System/Misc/TracyDefs.h"

CR_BIND_DERIVED(CBitmapMuzzleFlame, CProjectile, )

CR_REG_METADATA(CBitmapMuzzleFlame,
(
	CR_MEMBER(invttl),
	CR_MEMBER_BEGINFLAG(CM_Config),
		CR_IGNORED(sideTexture),
		CR_IGNORED(frontTexture),
		CR_MEMBER(colorMap),
		CR_MEMBER(size),
		CR_MEMBER(length),
		CR_MEMBER(sizeGrowth),
		CR_MEMBER(ttl),
		CR_MEMBER(frontOffset),
	CR_MEMBER_ENDFLAG(CM_Config),
	CR_SERIALIZER(Serialize)
))

CBitmapMuzzleFlame::CBitmapMuzzleFlame()
	: sideTexture(nullptr)
	, frontTexture(nullptr)
	, colorMap(nullptr)
	, size(0.0f)
	, length(0.0f)
	, sizeGrowth(0.0f)
	, frontOffset(0.0f)
	, ttl(0)
	, invttl(0.0f)
{
	// set fields from super-classes
	useAirLos = true;
	checkCol  = false;
	deleteMe  = false;
}

void CBitmapMuzzleFlame::Serialize(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::string sideName, frontName;
	if (s->IsWriting()) {
		sideName = projectileDrawer->textureAtlas->GetTextureName(sideTexture);
		frontName = projectileDrawer->textureAtlas->GetTextureName(frontTexture);
	}
	creg::GetType(sideName)->Serialize(s, &sideName);
	creg::GetType(frontName)->Serialize(s, &frontName);
	if (!s->IsWriting()) {
		sideTexture = projectileDrawer->textureAtlas->GetTexturePtr(sideName);
		frontTexture = projectileDrawer->textureAtlas->GetTexturePtr(frontName);
	}
}

void CBitmapMuzzleFlame::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	UpdateRotation();
	UpdateAnimParams();

	const float life = (gs->frameNum - createFrame + globalRendering->timeOffset) * invttl;
	const float igrowth = sizeGrowth * (1.0f - Square(1.0f - life));

	const float isize = size * (igrowth + 1.0f);
	const float ilength = length * (igrowth + 1.0f);

	SetDrawRadius(std::max(isize, ilength));

	unsigned char col[4];
	colorMap->GetColor(col, life);

	float3 fpos = pos + dir * frontOffset * ilength;

	const float3 zdir = (std::fabs(dir.dot(UpVector)) >= 0.99f)? FwdVector: UpVector;
	const float3 xdir = (dir.cross(zdir)).SafeANormalize();
	const float3 ydir = (dir.cross(xdir)).SafeANormalize();

	std::array<float3, 12> bounds = {
		  ydir * isize                ,
		  ydir * isize + dir * ilength,
		 -ydir * isize + dir * ilength,
		 -ydir * isize                ,

		  xdir * isize                ,
		  xdir * isize + dir * ilength,
		 -xdir * isize + dir * ilength,
		 -xdir * isize                ,

		 -xdir * isize + ydir * isize,
		  xdir * isize + ydir * isize,
		  xdir * isize - ydir * isize,
		 -xdir * isize - ydir * isize
	};

	if (math::fabs(rotVal) > 0.01f) {
		float3::rotate<false>(rotVal, dir, bounds);
	}

	if (IsValidTexture(sideTexture)) {
		AddEffectsQuad(
			{ pos + bounds[0], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[1], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[2], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[3], sideTexture->xstart, sideTexture->yend  , col }
		);
		AddEffectsQuad(
			{ pos + bounds[4], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[5], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[6], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[7], sideTexture->xstart, sideTexture->yend  , col }
		);
	}

	if (IsValidTexture(frontTexture)) {
		AddEffectsQuad(
			{ fpos + bounds[8 ], frontTexture->xstart, frontTexture->ystart, col },
			{ fpos + bounds[9 ], frontTexture->xend  , frontTexture->ystart, col },
			{ fpos + bounds[10], frontTexture->xend  , frontTexture->yend , col },
			{ fpos + bounds[11], frontTexture->xstart, frontTexture->yend , col }
		);
	}
}

void CBitmapMuzzleFlame::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	deleteMe |= ((ttl--) == 0);
}

void CBitmapMuzzleFlame::Init(const CUnit* owner, const float3& offset)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CProjectile::Init(owner, offset);

	invttl = 1.0f / ttl;

	SetDrawRadius(std::max(size, length));
}

int CBitmapMuzzleFlame::GetProjectilesCount() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return 2 * IsValidTexture(sideTexture) + 1 * IsValidTexture(frontTexture);
}


bool CBitmapMuzzleFlame::GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (CProjectile::GetMemberInfo(memberInfo))
		return true;

	CHECK_MEMBER_INFO_PTR  (CBitmapMuzzleFlame, sideTexture,  projectileDrawer->textureAtlas->GetTexturePtr)
	CHECK_MEMBER_INFO_PTR  (CBitmapMuzzleFlame, frontTexture, projectileDrawer->textureAtlas->GetTexturePtr)
	CHECK_MEMBER_INFO_PTR  (CBitmapMuzzleFlame, colorMap, CColorMap::LoadFromDefString)
	CHECK_MEMBER_INFO_FLOAT(CBitmapMuzzleFlame, size       )
	CHECK_MEMBER_INFO_FLOAT(CBitmapMuzzleFlame, length     )
	CHECK_MEMBER_INFO_FLOAT(CBitmapMuzzleFlame, sizeGrowth )
	CHECK_MEMBER_INFO_FLOAT(CBitmapMuzzleFlame, frontOffset)
	CHECK_MEMBER_INFO_INT  (CBitmapMuzzleFlame, ttl        )

	return false;
}
