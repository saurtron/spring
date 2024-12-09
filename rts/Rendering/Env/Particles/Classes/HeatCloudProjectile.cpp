/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "HeatCloudProjectile.h"

#include "Game/Camera.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "Sim/Projectiles/ExpGenSpawnableMemberInfo.h"

#include "System/Misc/TracyDefs.h"


CR_BIND_DERIVED(CHeatCloudProjectile, CProjectile, )

CR_REG_METADATA(CHeatCloudProjectile,
(
	CR_MEMBER_BEGINFLAG(CM_Config),
		CR_MEMBER(heat),
		CR_MEMBER(maxheat),
		CR_MEMBER(heatFalloff),
		CR_MEMBER(size),
		CR_MEMBER(sizeGrowth),
		CR_MEMBER(sizemod),
		CR_MEMBER(sizemodmod),
		CR_IGNORED(texture),
	CR_MEMBER_ENDFLAG(CM_Config),
	CR_SERIALIZER(Serialize)
))


CHeatCloudProjectile::CHeatCloudProjectile()
	: heat(0.0f)
	, maxheat(0.0f)
	, heatFalloff(0.0f)
	, size(0.0f)
	, sizeGrowth(0.0f)
	, sizemod(0.0f)
	, sizemodmod(0.0f)
{
	checkCol = false;
	useAirLos = true;
	texture = projectileDrawer->heatcloudtex;
}

CHeatCloudProjectile::CHeatCloudProjectile(
	CUnit* owner,
	const float3& pos,
	const float3& speed,
	const float temperature,
	const float size
)
	: CProjectile(pos, speed, owner, false, false, false)

	, heat(temperature)
	, maxheat(temperature)
	, heatFalloff(1.0f)
	, size(0.0f)
	, sizemod(0.0f)
	, sizemodmod(0.0f)
{
	sizeGrowth = size / temperature;
	checkCol = false;
	useAirLos = true;
	texture = projectileDrawer->heatcloudtex;

	SetRadiusAndHeight(size + sizeGrowth * heat / heatFalloff, 0.0f);
}

void CHeatCloudProjectile::Serialize(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::string name;
	if (s->IsWriting())
		name = projectileDrawer->textureAtlas->GetTextureName(texture);
	creg::GetType(name)->Serialize(s, &name);
	if (!s->IsWriting())
		texture = name.empty() ? projectileDrawer->heatcloudtex
				: projectileDrawer->textureAtlas->GetTexturePtr(name);
}

void CHeatCloudProjectile::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	pos += speed;
	heat = std::max(heat - heatFalloff, 0.0f);

	deleteMe |= (heat <= 0.0f);

	size += sizeGrowth;
	sizemod *= sizemodmod;
}

void CHeatCloudProjectile::Init(const CUnit* owner, const float3& offset)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CProjectile::Init(owner, offset);
}

void CHeatCloudProjectile::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	UpdateRotation();

	unsigned char col[4];
	const float dheat = std::max(0.0f, heat-globalRendering->timeOffset);
	const float alpha = (dheat / maxheat) * 255.0f;

	col[0] = (unsigned char) alpha;
	col[1] = (unsigned char) alpha;
	col[2] = (unsigned char) alpha;
	col[3] = 1;//(dheat/maxheat)*255.0f;

	const float drawsize = (size + sizeGrowth * globalRendering->timeOffset) * (1.0f - sizemod);

	const float3 ri = camera->GetRight();
	const float3 up = camera->GetUp();

	std::array<float3, 4> bounds = {
		-ri * drawsize - up * drawsize,
		 ri * drawsize - up * drawsize,
		 ri * drawsize + up * drawsize,
		-ri * drawsize + up * drawsize
	};

	if (math::fabs(rotVal) > 0.01f) {
		float3::rotate<false>(rotVal, camera->GetForward(), bounds);
	}
	AddEffectsQuad(
		{ drawPos + bounds[0], texture->xstart, texture->ystart, col },
		{ drawPos + bounds[1], texture->xend,   texture->ystart, col },
		{ drawPos + bounds[2], texture->xend,   texture->yend,   col },
		{ drawPos + bounds[3], texture->xstart, texture->yend,   col }
	);
}

int CHeatCloudProjectile::GetProjectilesCount() const
{
	return 1;
}


bool CHeatCloudProjectile::GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (CProjectile::GetMemberInfo(memberInfo))
		return true;

	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, heat       )
	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, maxheat    )
	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, heatFalloff)
	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, size       )
	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, sizeGrowth )
	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, sizemod    )
	CHECK_MEMBER_INFO_FLOAT (CHeatCloudProjectile, sizemodmod )
	CHECK_MEMBER_INFO_PTR   (CHeatCloudProjectile, texture, projectileDrawer->textureAtlas->GetTexturePtr)

	return false;
}
