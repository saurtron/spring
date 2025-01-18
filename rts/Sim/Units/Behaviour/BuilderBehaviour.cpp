#include "BuilderBehaviour.h"
#include "BuilderCmdBehaviour.h"

#include "Sim/Units/CommandAI/Command.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "System/SpringMath.h"
#include "System/Sound/ISoundChannels.h"
#include "Sim/Units/Scripts/CobInstance.h"
#include "Game/GlobalUnsynced.h"

// from Builder.cpp
#include <assert.h>
#include <algorithm>
//#include "Building.h"
#include "Game/GameHelper.h"
#include "Game/GlobalUnsynced.h"
#include "Map/Ground.h"
#include "Map/MapDamage.h"
#include "Map/ReadMap.h"
#include "System/SpringMath.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "Sim/Misc/ModInfo.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/MoveDefHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Units/BehaviourAI/BuilderBehaviourAI.h"
#include "Sim/Units/Scripts/CobInstance.h"
//#include "Sim/Units/CommandAI/BuilderCAI.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/BuilderCaches.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitLoader.h"
#include "System/EventHandler.h"
#include "System/Log/ILog.h"
#include "System/Sound/ISoundChannels.h"

#include "System/Misc/TracyDefs.h"

template CBuilderBehaviour* CUnit::GetBehaviour<CBuilderBehaviour>() const;

CR_BIND_DERIVED(CBuilderBehaviour, CBaseBuilderBehaviour, )

CR_REG_METADATA(CBuilderBehaviour, (
	CR_MEMBER(buildDistance) //,
))

CBuilderBehaviour::CBuilderBehaviour():
	CBaseBuilderBehaviour(),
	buildDistance(16) //,
{
}

CBuilderBehaviour::CBuilderBehaviour(CUnit* owner):
	CBaseBuilderBehaviour(owner),
	buildDistance(16) //,
{
}

/* Builder.cpp */
using std::min;
using std::max;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


void CBuilderBehaviour::PreInit(const UnitLoadParams& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& unitDef = params.unitDef;
	range3D = unitDef->buildRange3D;
	buildDistance = (params.unitDef)->buildDistance;

	buildSpeed     = INV_GAME_SPEED * unitDef->buildSpeed;

	CBaseBuilderBehaviour::PreInit(params);
}


bool CBuilderBehaviour::UpdateBuild(const Command& fCommand)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto inBuildStance = owner->inBuildStance;
	const auto& unitDef = owner->unitDef;
	CUnit* curBuildee = curBuild;
	CBuilderBehaviourAI* cai = owner->commandAI->GetBehaviourAI<CBuilderBehaviourAI>();

	if (curBuildee == nullptr || !cai->IsInBuildRange(curBuildee))
		return false;

	if (fCommand.GetID() == CMD_WAIT) {
		if (curBuildee->buildProgress < 1.0f) {
			// prevent buildee from decaying (we cannot call StopBuild here)
			curBuildee->AddBuildPower(owner, 0.0f);
		} else {
			// stop repairing (FIXME: should be much cleaner to let BuilderCAI
			// call this instead when a wait command is given?)
			StopBuild();
		}

		return true;
	}

	if (curBuildee->soloBuilder != nullptr && (curBuildee->soloBuilder != owner)) {
		StopBuild();
		return true;
	}

	// NOTE:
	//   technically this block of code should be guarded by
	//   "if (inBuildStance)", but doing so can create zombie
	//   guarders because scripts might not set inBuildStance
	//   to true when guard or repair orders are executed and
	//   SetRepairTarget does not check for it
	//
	//   StartBuild *does* ensure construction will not start
	//   until inBuildStance is set to true by the builder's
	//   script, and there are no cases during construction
	//   when inBuildStance can become false yet the buildee
	//   should be kept from decaying, so this is free from
	//   serious side-effects (when repairing, a builder might
	//   start adding build-power before having fully finished
	//   its opening animation)
	if (!(inBuildStance || true))
		return true;

	owner->ScriptDecloak(curBuildee, nullptr);

	// adjusted build-speed: use repair-speed on units with
	// progress >= 1 rather than raw build-speed on buildees
	// with progress < 1
	float adjBuildSpeed = buildSpeed;

	auto* builderCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
	if (builderCmd != nullptr && curBuildee->buildProgress >= 1.0f)
		adjBuildSpeed = std::min(builderCmd->repairSpeed, unitDef->maxRepairSpeed * 0.5f - curBuildee->repairAmount); // repair

	if (adjBuildSpeed > 0.0f && curBuildee->AddBuildPower(owner, adjBuildSpeed)) {
		CreateNanoParticle(curBuildee->midPos, curBuildee->radius * 0.5f, false);
		return true;
	}

	// check if buildee finished construction
	if (curBuildee->beingBuilt || curBuildee->health < curBuildee->maxHealth)
		return true;

	StopBuild();
	return true;
}


void CBuilderBehaviour::UpdatePre()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto beingBuilt = owner->beingBuilt;
	const CCommandAI* cai = owner->commandAI;
	CBuilderCmdBehaviour* builderCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();

	const CCommandQueue& cQueue = cai->commandQue;
	const Command& fCommand = (!cQueue.empty())? cQueue.front(): Command(CMD_STOP);

	bool updated = false;

	CBaseBuilderBehaviour::UpdatePre(); //nanoPieceCache.Update();

	if (!beingBuilt && !owner->IsStunned()) {
		if (builderCmd != nullptr) {
			updated = updated || builderCmd->UpdateTerraform(fCommand);
			updated = updated || builderCmd->AssistTerraform(fCommand);
		}
		updated = updated || UpdateBuild(fCommand);
		if (builderCmd != nullptr) {
			updated = updated || builderCmd->UpdateReclaim(fCommand);
			updated = updated || builderCmd->UpdateResurrect(fCommand);
			updated = updated || builderCmd->UpdateCapture(fCommand);
		}
	}
}


void CBuilderBehaviour::SlowUpdate()
{
  	RECOIL_DETAILED_TRACY_ZONE;
	CBuilderCmdBehaviour* builderCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
	/* TODO, can put this in a method for builderCmd */
	if (builderCmd != nullptr && builderCmd->terraforming) {
		constexpr int tsr = TERRA_SMOOTHING_RADIUS;
		mapDamage->RecalcArea(builderCmd->tx1 - tsr, builderCmd->tx2 + tsr, builderCmd->tz1 - tsr, builderCmd->tz2 + tsr);
	}

	CBaseBuilderBehaviour::SlowUpdate();
}


void CBuilderBehaviour::StopBuild(bool callScript)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& script = owner->script;
	if (curBuild != nullptr)
		DeleteDeathDependence(curBuild, DEPENDENCE_BUILD);
	CBuilderCmdBehaviour* builderCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
	if (builderCmd != nullptr)
		builderCmd->StopBuild();

	curBuild = nullptr;

	if (callScript)
		script->StopBuilding();

	owner->SetHoldFire(false);
}


bool CBuilderBehaviour::StartBuild(BuildInfo& buildInfo, CFeature*& feature, bool& inWaitStance, bool& limitReached)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto allyteam = owner->allyteam;
	const auto team = owner->team;
	const CUnit* prvBuild = curBuild;

	StopBuild(false);
	owner->TempHoldFire(-1);

	buildInfo.pos = CGameHelper::Pos2BuildPos(buildInfo, true);

	auto isBuildeeFloating = [](const BuildInfo& buildInfo) {
		if (buildInfo.def->RequireMoveDef()) {
			MoveDef* md = moveDefHandler.GetMoveDefByPathType(buildInfo.def->pathType);
			return (md->FloatOnWater());
		} else {
			return (buildInfo.def->floatOnWater);
		}
	};

	// Units that cannot be underwater need their build checks kept above water or else collision detections will
	// produce the wrong results.
	if (isBuildeeFloating(buildInfo))
		buildInfo.pos.y = (buildInfo.pos.y < 0.f) ? 0.f : buildInfo.pos.y;

	// Pass -1 as allyteam to behave like we have maphack.
	// This is needed to prevent building on top of cloaked stuff.
	const CGameHelper::BuildSquareStatus tbs = CGameHelper::TestUnitBuildSquare(buildInfo, feature, -1, true);

	switch (tbs) {
		case CGameHelper::BUILDSQUARE_OPEN:
			break;

		case CGameHelper::BUILDSQUARE_BLOCKED:
		case CGameHelper::BUILDSQUARE_OCCUPIED: {
			const CUnit* u = nullptr;

			const int2 mins = CSolidObject::GetMapPosStatic(buildInfo.pos, buildInfo.GetXSize(), buildInfo.GetZSize());
			const int2 maxs = mins + int2(buildInfo.GetXSize(), buildInfo.GetZSize());

			for (int z = mins.y; z < maxs.y; ++z) {
				for (int x = mins.x; x < maxs.x; ++x) {
					const CGroundBlockingObjectMap::BlockingMapCell& cell = groundBlockingObjectMap.GetCellUnsafeConst(float3{
						static_cast<float>(x * SQUARE_SIZE),
						0.0f,
						static_cast<float>(z * SQUARE_SIZE) }
					);

					// look for any blocking assistable buildee at build.pos
					for (size_t i = 0, n = cell.size(); i < n; i++) {
						const CUnit* cu = dynamic_cast<const CUnit*>(cell[i]);

						if (cu == nullptr)
							continue;
						if (allyteam != cu->allyteam)
							return false; // Enemy units that block always block the cell
						/* TODO WHAT IS THIS
						if (!CanAssistUnit(cu, buildInfo.def))
							continue;
						*/

						u = cu;
						goto out; //lol
					}
				}
			}

			out:
			// <pos> might map to a non-blocking portion
			// of the buildee's yardmap, fallback check
			if (u == nullptr)
				u = CGameHelper::GetClosestFriendlyUnit(nullptr, buildInfo.pos, buildDistance, allyteam);

			if (u != nullptr) {
				// TODO REFACTOR: maybe make a method inside BuilderCmd for this
				auto* builderCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
				if (builderCmd != nullptr && builderCmd->CanAssistUnit(u, buildInfo.def)) {
					// StopBuild sets this to false, fix it here if picking up the same buildee again
					builderCmd->terraforming = (u == prvBuild && u->terraformLeft > 0.0f);

					AddDeathDependence(curBuild = const_cast<CUnit*>(u), DEPENDENCE_BUILD);
					ScriptStartBuilding(u->pos, false);
					return true;
				}

				// let BuggerOff handle this case (TODO: non-landed aircraft should not count)
				if (buildInfo.FootPrintOverlap(u->pos, u->GetFootPrint(SQUARE_SIZE * 0.5f)))
					return false;
			}
		} return false;

		case CGameHelper::BUILDSQUARE_RECLAIMABLE:
			// caller should handle this
			return false;
	}

	// at this point we know the builder is going to create a new unit, bail if at the limit
	if ((limitReached = (unitHandler.NumUnitsByTeamAndDef(team, buildInfo.def->id) >= buildInfo.def->maxThisUnit)))
		return false;

	if ((inWaitStance = !ScriptStartBuilding(buildInfo.pos, true)))
		return false;

	const UnitDef* buildeeDef = buildInfo.def;
	const UnitLoadParams buildeeParams = {buildeeDef, owner, buildInfo.pos, ZeroVector, -1, team, buildInfo.buildFacing, true, false};

	CUnit* buildee = unitLoader->LoadUnit(buildeeParams);

	// floating structures don't terraform the seabed
	const bool buildeeOnWater = (buildee->FloatOnWater() && buildee->IsInWater());
	const bool allowTerraform = (!mapDamage->Disabled() && buildeeDef->levelGround);
	const bool  skipTerraform = (buildeeOnWater || buildeeDef->IsAirUnit() || !buildeeDef->IsImmobileUnit());

	CBuilderCmdBehaviour* builderCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
	if (!allowTerraform || skipTerraform) {
		// skip the terraforming job
		buildee->terraformLeft = 0.0f;
		buildee->groundLevelled = true;
	} else if(builderCmd != nullptr) {
		// TODO: REFACTOR, maybe move this inside CmdBehaviour
		builderCmd->tx1 = (int)std::max(0.0f, (buildee->pos.x - (buildee->xsize * 0.5f * SQUARE_SIZE)) / SQUARE_SIZE);
		builderCmd->tz1 = (int)std::max(0.0f, (buildee->pos.z - (buildee->zsize * 0.5f * SQUARE_SIZE)) / SQUARE_SIZE);
		builderCmd->tx2 = std::min(mapDims.mapx, builderCmd->tx1 + buildee->xsize);
		builderCmd->tz2 = std::min(mapDims.mapy, builderCmd->tz1 + buildee->zsize);

		buildee->terraformLeft = builderCmd->CalculateBuildTerraformCost(buildInfo);
		buildee->groundLevelled = false;

		builderCmd->terraforming    = true;
		builderCmd->terraformType   = CBuilderCmdBehaviour::Terraform_Building;
		builderCmd->terraformRadius = (builderCmd->tx2 - builderCmd->tx1) * SQUARE_SIZE;
		builderCmd->terraformCenter = buildee->pos;
	}

	// pass the *builder*'s udef for checking canBeAssisted; if buildee
	// happens to be a non-assistable factory then it would also become
	// impossible to *construct* with multiple builders
	buildee->SetSoloBuilder(owner, owner->unitDef);
	AddDeathDependence(curBuild = buildee, DEPENDENCE_BUILD);

	// if the ground is not going to be terraformed the buildee would
	// 'pop' to the correct height over the (un-flattened) terrain on
	// completion, so put it there to begin with
	curBuild->moveType->SlowUpdate();
	return true;
}


void CBuilderBehaviour::DependentDied(CObject* o)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (o == curBuild) {
		curBuild = nullptr;
		StopBuild();
	}
	CBaseBuilderBehaviour::DependentDied(o);
}


bool CBuilderBehaviour::ScriptStartBuilding(float3 pos, bool silent)
{
	auto& script = owner->script;
	const auto inBuildStance = owner->inBuildStance;
	const auto& unitDef = owner->unitDef;
	if (script->HasStartBuilding()) {
		const auto& midPos = owner->midPos;
		const auto& frontdir = owner->frontdir;
		const auto& updir = owner->updir;
		const auto& heading = owner->heading;

		const float3 wantedDir = (pos - midPos).Normalize();
		const float h = GetHeadingFromVectorF(wantedDir.x, wantedDir.z);
		const float p = math::asin(wantedDir.dot(updir));
		const float pitch = math::asin(frontdir.dot(updir));

		// clamping p - pitch not needed, range of asin is -PI/2..PI/2,
		// so max difference between two asin calls is PI.
		// FIXME: convert CSolidObject::heading to radians too.
		script->StartBuilding(ClampRad(h - heading * TAANG2RAD), p - pitch);
	}

	if ((!silent || inBuildStance) && owner->IsInLosForAllyTeam(gu->myAllyTeam))
		Channels::General->PlayRandomSample(unitDef->sounds.build, pos);

	return inBuildStance;
}


