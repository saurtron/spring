#include "BaseBuilderBehaviour.h"

#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Units/CommandAI/Command.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitLoader.h"

template CBaseBuilderBehaviour* CUnit::GetBehaviour<CBaseBuilderBehaviour>() const;

CR_BIND_DERIVED(CBaseBuilderBehaviour, CBehaviour, )

CR_REG_METADATA(CBaseBuilderBehaviour, (
	CR_MEMBER(buildSpeed),
	CR_MEMBER(range3D),

	CR_MEMBER(curBuild),
	CR_MEMBER(nanoPieceCache)
))

CBaseBuilderBehaviour::CBaseBuilderBehaviour():
	CBehaviour(),
	buildSpeed(100),
	curBuild(nullptr),
	range3D(true)
{
}

CBaseBuilderBehaviour::CBaseBuilderBehaviour(CUnit* owner):
	CBehaviour(owner),
	buildSpeed(100),
	curBuild(nullptr),
	range3D(owner->unitDef->buildRange3D)
{
}

void CBaseBuilderBehaviour::PreInit(const UnitLoadParams& params)
{
	auto& unitDef = params.unitDef;
	range3D = unitDef->buildRange3D;

	buildSpeed     = INV_GAME_SPEED * unitDef->buildSpeed;
	CBehaviour::PreInit(params);
}

void CBaseBuilderBehaviour::UpdatePre()
{
	nanoPieceCache.Update();
}

void CBaseBuilderBehaviour::CreateNanoParticle(const float3& goal, float radius, bool inverse, bool highPriority)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int modelNanoPiece = nanoPieceCache.GetNanoPiece(owner->script);

	if (!owner->localModel.Initialized() || !owner->localModel.HasPiece(modelNanoPiece))
		return;

	const float3 relNanoFirePos = owner->localModel.GetRawPiecePos(modelNanoPiece);
	const float3 nanoPos = owner->GetObjectSpacePos(relNanoFirePos);

	// unsynced
	projectileHandler.AddNanoParticle(nanoPos, goal, owner->unitDef, owner->team, radius, inverse, highPriority);
}
