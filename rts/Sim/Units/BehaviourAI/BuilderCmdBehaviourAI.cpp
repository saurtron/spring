/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cassert>

#include "BuilderCmdBehaviourAI.h"
#include "ExternalAI/EngineOutHandler.h"
#include "Game/GameHelper.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Map/Ground.h"
#include "Map/MapDamage.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Misc/Team.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/Units/Behaviour/BaseBuilderBehaviour.h"
#include "Sim/Units/Behaviour/BuilderCmdBehaviour.h"
#include "Sim/Units/Behaviour/FactoryBehaviour.h"
#include "Sim/Units/Behaviour/BuilderBehaviour.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/CommandAI/BuilderCaches.h"
#include "System/SpringMath.h"
#include "System/StringUtil.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/Log/ILog.h"
#include "System/creg/STL_Map.h"

#include "System/Misc/TracyDefs.h"

template CBuilderCmdBehaviourAI* CCommandAI::GetBehaviourAI<CBuilderCmdBehaviourAI>() const;

CR_BIND_DERIVED(CBuilderCmdBehaviourAI, CBehaviourAI , )

CR_REG_METADATA(CBuilderCmdBehaviourAI , (
	CR_MEMBER(ownerBuilder),
	CR_MEMBER(baseBuilder),
	CR_MEMBER(range3D),
	CR_MEMBER(randomCounter),

	CR_MEMBER(lastPC1),
	CR_MEMBER(lastPC2),
	CR_MEMBER(lastPC3),
	/*CR_POSTLOAD(PostLoad),*/
	CR_PREALLOC(GetPreallocContainer)
))

CBuilderCmdBehaviourAI::CBuilderCmdBehaviourAI():
	CBehaviourAI(),
	ownerBuilder(nullptr),
	baseBuilder(nullptr),
	randomCounter(0),
	lastPC1(-1),
	lastPC2(-1),
	lastPC3(-1),
	range3D(true)
{
}

CBuilderCmdBehaviourAI::CBuilderCmdBehaviourAI(CUnit* owner):
	CBehaviourAI(owner),
	randomCounter(0),
	lastPC1(-1),
	lastPC2(-1),
	lastPC3(-1),
	range3D(owner->unitDef->buildRange3D)
{
	// TODO REFACTOR: need to get builder when other constructor is used
	ownerBuilder = owner->GetBehaviour<CBuilderCmdBehaviour>();
	// TODO REFACTOR: why GetBehaviour can't resolve CBaseBuilder directly??
	CFactoryBehaviour* factory = owner->GetBehaviour<CFactoryBehaviour>();
	if (factory)
		baseBuilder = factory;
	else {
		CBuilderBehaviour* builder = owner->GetBehaviour<CBuilderBehaviour>();
		baseBuilder = builder;
	}
	auto& possibleCommands = owner->commandAI->possibleCommands;

	if (owner->unitDef->canRepair) {
		SCommandDescription c;

		c.id   = CMD_REPAIR;
		c.type = CMDTYPE_ICON_UNIT_OR_AREA;

		c.action    = "repair";
		c.name      = "Repair";
		c.tooltip   = c.name + ": Repairs another unit";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}
	else if (owner->unitDef->canAssist) {
		SCommandDescription c;

		c.id   = CMD_REPAIR;
		c.type = CMDTYPE_ICON_UNIT_OR_AREA;

		c.action    = "assist";
		c.name      = "Assist";
		c.tooltip   = c.name + ": Help build something";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

	if (owner->unitDef->canReclaim) {
		SCommandDescription c;

		c.id   = CMD_RECLAIM;
		c.type = CMDTYPE_ICON_UNIT_FEATURE_OR_AREA;

		c.action    = "reclaim";
		c.name      = "Reclaim";
		c.tooltip   = c.name + ": Sucks in the metal/energy content of a unit/feature\nand adds it to your storage";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

	if (owner->unitDef->canRestore && !mapDamage->Disabled()) {
		SCommandDescription c;

		c.id   = CMD_RESTORE;
		c.type = CMDTYPE_ICON_AREA;

		c.action    = "restore";
		c.name      = "Restore";
		c.tooltip   = c.name + ": Restores an area of the map to its original height";
		c.mouseicon = c.name;

		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

	if (owner->unitDef->canResurrect) {
		SCommandDescription c;

		c.id   = CMD_RESURRECT;
		c.type = CMDTYPE_ICON_UNIT_FEATURE_OR_AREA;

		c.action    = "resurrect";
		c.name      = "Resurrect";
		c.tooltip   = c.name + ": Resurrects a unit from a feature";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}
	if (owner->unitDef->canCapture) {
		SCommandDescription c;

		c.id   = CMD_CAPTURE;
		c.type = CMDTYPE_ICON_UNIT_OR_AREA;

		c.action    = "capture";
		c.name      = "Capture";
		c.tooltip   = c.name + ": Captures a unit from the enemy";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}
	//unitHandler.AddBuilderCAI(this);
}


CBuilderCmdBehaviourAI::~CBuilderCmdBehaviourAI()
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBuilderCaches::RemoveUnitFromReclaimers(owner);
	CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
	CBuilderCaches::RemoveUnitFromResurrecters(owner);
	//unitHandler.RemoveBuilderCAI(this);
}
/*
void CBuilderCmdBehaviourAI::PostLoad()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& possibleCommands = owner->commandAI->possibleCommands;
	const auto& commandQue = owner->commandAI->commandQue;
	for (const SCommandDescription* cd: possibleCommands) {
		if (cd->id < 0)
			buildOptions.insert(cd->id);
	}
	if (commandQue.empty())
		return;

	ownerBuilder = owner->GetBehaviour<CBuilderCmdBehaviour>();

	const Command& c = commandQue.front();

	if (buildOptions.find(c.GetID()) != buildOptions.end()) {
		build.Parse(c);
		build.pos = CGameHelper::Pos2BuildPos(build, true);
	}
}*/

float CBuilderCmdBehaviourAI::GetBuildRange(const float targetRadius) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (ownerBuilder->buildDistance + targetRadius);
}

bool CBuilderCmdBehaviourAI::IsInBuildRange(const CWorldObject* obj) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return IsInBuildRange(obj->pos, obj->buildeeRadius);
}

bool CBuilderCmdBehaviourAI::IsInBuildRange(const float3& objPos, const float objRadius) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float immDistSqr = f3SqDist(owner->pos, objPos);
	const float buildDist = GetBuildRange(objRadius);

	return (immDistSqr <= (buildDist * buildDist));
}



inline bool CBuilderCmdBehaviourAI::MoveInBuildRange(const CWorldObject* obj, const bool checkMoveTypeForFailed)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return MoveInBuildRange(obj->pos, obj->buildeeRadius, checkMoveTypeForFailed);
}

bool CBuilderCmdBehaviourAI::MoveInBuildRange(const float3& objPos, float objRadius, const bool checkMoveTypeForFailed)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsInBuildRange(objPos, objRadius)) {
		// NOTE:
		//   ignore the fail-check if we are an aircraft, movetype code
		//   is unreliable wrt. setting it correctly and causes (landed)
		//   aircraft to discard orders
		const bool checkFailed = (checkMoveTypeForFailed && !owner->unitDef->IsAirUnit());
		// check if the AMoveType::Failed belongs to the same goal position
		const bool haveFailed = (owner->moveType->progressState == AMoveType::Failed && f3SqDist(owner->moveType->goalPos, objPos) > 1.0f);

		if (checkFailed && haveFailed) {
			// don't call SetGoal() it would reset moveType->progressState
			// and so later code couldn't check if the move command failed
			return false;
		}

		// too far away, start a move command
		SetGoal(objPos, owner->pos, GetBuildRange(objRadius) * 0.9f);
		return false;
	}

	if (owner->unitDef->IsAirUnit()) {
		StopMoveAndKeepPointing(objPos, GetBuildRange(objRadius) * 0.9f, false);
	} else {
		StopMoveAndKeepPointing(owner->moveType->goalPos, GetBuildRange(objRadius) * 0.9f, false);
	}

	return true;
}

// TODO: removed inline -> maybe can be moved to CBuilderBehaviourAI??
bool CBuilderCmdBehaviourAI::OutOfImmobileRange(const Command& cmd) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// builder can move
	if (owner->unitDef->canmove)
		return false;

	// not an internal object-targeted command
	if (!cmd.IsInternalOrder() || (cmd.GetNumParams() != 1))
		return false;

	const int objID = cmd.GetParam(0);
	const CWorldObject* obj = unitHandler.GetUnit(objID);

	if (obj == nullptr) {
		// features don't move, but maybe the unit was transported?
		obj = featureHandler.GetFeature(objID - unitHandler.MaxUnits());

		if (obj == nullptr)
			return false;
	}

	switch (cmd.GetID()) {
		case CMD_REPAIR:
		case CMD_RECLAIM:
		case CMD_RESURRECT:
		case CMD_CAPTURE: {
			if (!IsInBuildRange(obj))
				return true;

			break;
		}
	}

	return false;
}

void CBuilderCmdBehaviourAI::ReclaimFeature(CFeature* f)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!owner->unitDef->canReclaim || !f->def->reclaimable) {
		// FIXME user shouldn't be able to queue buildings on top of features
		// in the first place (in this case).
		StopMoveAndFinishCommand();
	} else {
		auto& commandQue = owner->commandAI->commandQue;
		commandQue.push_front(Command(CMD_RECLAIM, 0, f->id + unitHandler.MaxUnits()));
		// this assumes that the reclaim command can never return directly
		// without having reclaimed the target
		owner->commandAI->SlowUpdate();
	}
}

bool CBuilderCmdBehaviourAI::TargetInterceptable(const CUnit* unit, float targetSpeed) {
	RECOIL_DETAILED_TRACY_ZONE;
	// if the target is moving away at a higher speed than we can manage, there is little point in chasing it
	const float maxSpeed = owner->moveType->GetMaxSpeed();
	if (targetSpeed <= maxSpeed)
		return true;
	const float3 unitToPos = unit->pos - owner->pos;
	return (unitToPos.dot2D(unit->speed) <= unitToPos.Length2D() * maxSpeed);
}


void CBuilderCmdBehaviourAI::ExecuteRepair(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	auto& inCommand = owner->commandAI->inCommand;
	// not all builders are repair-capable by default
	if (!owner->unitDef->canRepair)
		return;

	if (c.GetNumParams() == 1 || c.GetNumParams() == 5) {
		// repair unit
		CUnit* unit = unitHandler.GetUnit(c.GetParam(0));

		if (unit == nullptr) {
			StopMoveAndFinishCommand();
			return;
		}

		if (cai != nullptr && cai->tempOrder && owner->moveState <= MOVESTATE_MANEUVER) {
			// limit how far away we go when not roaming
			if (LinePointDist(cai->commandPos1, cai->commandPos2, unit->pos) > std::max(500.0f, GetBuildRange(unit->buildeeRadius))) {
				StopMoveAndFinishCommand();
				return;
			}
		}

		if (c.GetNumParams() == 5) {
			const float3& pos = c.GetPos(1);
			const float radius = c.GetParam(4) + 100.0f; // do not walk too far outside repair area

			if ((pos - unit->pos).SqLength2D() > radius * radius ||
				(unit->IsMoving() && ((c.IsInternalOrder() && !TargetInterceptable(unit, unit->speed.Length2D())) || baseBuilder->curBuild == unit)
				&& !IsInBuildRange(unit))) {
				StopMoveAndFinishCommand();
				return;
			}
		}

		// do not consider units under construction irreparable
		// even if they can be repaired
		bool canRepairUnit = true;
		canRepairUnit &= ((unit->beingBuilt) || (unit->unitDef->repairable && (unit->health < unit->maxHealth)));
		canRepairUnit &= ((unit != owner) || owner->unitDef->canSelfRepair);
		canRepairUnit &= (!unit->soloBuilder || (unit->soloBuilder == owner));
		canRepairUnit &= (!c.IsInternalOrder() || (c.GetOpts() & CONTROL_KEY) || !CBuilderCaches::IsUnitBeingReclaimed(unit, owner));
		canRepairUnit &= (UpdateTargetLostTimer(unit->id) != 0);

		if (canRepairUnit) {
			if (MoveInBuildRange(unit))
				ownerBuilder->SetRepairTarget(unit);
		} else {
			StopMoveAndFinishCommand();
		}
	} else if (c.GetNumParams() == 4) {
		// area repair
		const float3 pos = c.GetPos(0);
		const float radius = c.GetParam(3);

		ownerBuilder->StopBuild();
		if (FindRepairTargetAndRepair(pos, radius, c.GetOpts(), false, (c.GetOpts() & META_KEY))) {
			inCommand = false;
			owner->commandAI->SlowUpdate();
			return;
		}

		if (!(c.GetOpts() & ALT_KEY))
			StopMoveAndFinishCommand();

	} else {
		StopMoveAndFinishCommand();
	}
}


void CBuilderCmdBehaviourAI::ExecuteCapture(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	// not all builders are capture-capable by default
	if (!owner->unitDef->canCapture)
		return;

	if (c.GetNumParams() == 1 || c.GetNumParams() == 5) {
		// capture unit
		CUnit* unit = unitHandler.GetUnit(c.GetParam(0));

		if (unit == nullptr) {
			StopMoveAndFinishCommand();
			return;
		}

		if (c.GetNumParams() == 5) {
			const float3& pos = c.GetPos(1);
			const float radius = c.GetParam(4) + 100.0f; // do not walk too far outside capture area

			if (((pos - unit->pos).SqLength2D() > (radius * radius) ||
				(ownerBuilder->curCapture == unit && unit->IsMoving() && !IsInBuildRange(unit)))) {
				StopMoveAndFinishCommand();
				return;
			}
		}

		if (unit->unitDef->capturable && unit->team != owner->team && UpdateTargetLostTimer(unit->id)) {
			if (MoveInBuildRange(unit)) {
				ownerBuilder->SetCaptureTarget(unit);
			}
		} else {
			StopMoveAndFinishCommand();
		}
	} else if (c.GetNumParams() == 4) {
		// area capture
		const float3 pos = c.GetPos(0);
		const float radius = c.GetParam(3);

		ownerBuilder->StopBuild();

		if (FindCaptureTargetAndCapture(pos, radius, c.GetOpts(), (c.GetOpts() & META_KEY))) {
			inCommand = false;
			owner->commandAI->SlowUpdate();
			return;
		}

		if (!(c.GetOpts() & ALT_KEY))
			StopMoveAndFinishCommand();

	} else {
		StopMoveAndFinishCommand();
	}
}


void CBuilderCmdBehaviourAI::ExecuteGuard(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	if (!owner->unitDef->canGuard)
		return;

	CUnit* guardee = unitHandler.GetUnit(c.GetParam(0));

	if (guardee == nullptr) {
		StopMoveAndFinishCommand();
		return;
	}
	if (guardee == owner) {
		StopMoveAndFinishCommand();
		return;
	}
	if (UpdateTargetLostTimer(guardee->id) == 0) {
		StopMoveAndFinishCommand();
		return;
	}
	if (guardee->outOfMapTime > (GAME_SPEED * 5)) {
		StopMoveAndFinishCommand();
		return;
	}

	auto& commandQue = owner->commandAI->commandQue;
	CBaseBuilderBehaviour* base;
	CFactoryBehaviour* factory = guardee->GetBehaviour<CFactoryBehaviour>();
	if (factory)
		base = factory;
	else {
		CBuilderBehaviour* builder = guardee->GetBehaviour<CBuilderBehaviour>();
		base = builder;
	}
	auto* b = guardee->GetBehaviour<CBuilderCmdBehaviour>();

	//if (CBuilder* b = dynamic_cast<CBuilder*>(guardee)) {
	if (b) {
		if (b->terraforming) {
			if (MoveInBuildRange(b->terraformCenter, b->terraformRadius * 0.7f)) {
				ownerBuilder->HelpTerraform(b);
			} else {
				StopSlowGuard();
			}
			return;
		} else if (b->curReclaim && owner->unitDef->canReclaim) {
			StopSlowGuard();
			if (!ReclaimObject(b->curReclaim)) {
				StopMove();
			}
			return;
		} else if (b->curResurrect && owner->unitDef->canResurrect) {
			StopSlowGuard();
			if (!ResurrectObject(b->curResurrect)) {
				StopMove();
			}
			return;
		} else {
			ownerBuilder->StopBuild();
		}

		const bool pushRepairCommand =
			(  base->curBuild != nullptr) &&
			(  base->curBuild->soloBuilder == nullptr || base->curBuild->soloBuilder == owner) &&
			(( base->curBuild->beingBuilt && owner->unitDef->canAssist) ||
			( !base->curBuild->beingBuilt && owner->unitDef->canRepair));

		if (pushRepairCommand) {
			StopSlowGuard();

			Command nc(CMD_REPAIR, c.GetOpts(), base->curBuild->id);

			commandQue.push_front(nc);
			inCommand = false;
			owner->commandAI->SlowUpdate();
			return;
		}
	}

	if (CFactoryBehaviour* fac = guardee->GetBehaviour<CFactoryBehaviour>()) {
		const bool pushRepairCommand =
			(  fac->curBuild != nullptr) &&
			(  fac->curBuild->soloBuilder == nullptr || fac->curBuild->soloBuilder == owner) &&
			(( fac->curBuild->beingBuilt && owner->unitDef->canAssist) ||
			 (!fac->curBuild->beingBuilt && owner->unitDef->canRepair));

		if (pushRepairCommand) {
			StopSlowGuard();

			commandQue.push_front(Command(CMD_REPAIR, c.GetOpts(), fac->curBuild->id));
			inCommand = false;
			owner->commandAI->SlowUpdate();
			return;
		}
	}

	if (!(c.GetOpts() & CONTROL_KEY) && CBuilderCaches::IsUnitBeingReclaimed(guardee, owner))
		return;

	const float3 pos    = guardee->pos;
	const float  radius = (guardee->immobile) ? guardee->buildeeRadius : guardee->buildeeRadius * 0.8f; // in case of mobile units reduce radius a bit

	if (MoveInBuildRange(pos, radius)) {
		StartSlowGuard(guardee->moveType->GetMaxSpeed());

		const bool pushRepairCommand =
			(  guardee->health < guardee->maxHealth) &&
			(  guardee->soloBuilder == nullptr || guardee->soloBuilder == owner) &&
			(( guardee->beingBuilt && owner->unitDef->canAssist) ||
			 (!guardee->beingBuilt && owner->unitDef->canRepair));

		if (pushRepairCommand) {
			StopSlowGuard();
			commandQue.push_front(Command(CMD_REPAIR, c.GetOpts(), guardee->id));
			inCommand = false;
			return;
		}

		NonMoving();
	} else {
		StopSlowGuard();
	}
}


void CBuilderCmdBehaviourAI::ExecuteReclaim(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	const auto& commandQue = owner->commandAI->commandQue;
	// not all builders are reclaim-capable by default
	if (!owner->unitDef->canReclaim)
		return;

	if (c.GetNumParams() == 1 || c.GetNumParams() == 5) {
		const int signedId = (int) c.GetParam(0);

		if (signedId < 0) {
			LOG_L(L_WARNING, "Trying to reclaim unit or feature with id < 0 (%i), aborting.", signedId);
			return;
		}

		const unsigned int uid = signedId;

		const bool checkForBetterTarget = ((++randomCounter % 5) == 0);
		if (checkForBetterTarget && c.IsInternalOrder() && (c.GetNumParams() >= 5)) {
			// regular check if there is a closer reclaim target
			CSolidObject* obj;

			if (uid >= unitHandler.MaxUnits()) {
				obj = featureHandler.GetFeature(uid - unitHandler.MaxUnits());
			} else {
				obj = unitHandler.GetUnit(uid);
			}

			if (obj) {
				const float3& pos = c.GetPos(1);
				const float radius = c.GetParam(4);
				const float curdist = pos.SqDistance2D(obj->pos);

				const bool recUnits = !!(c.GetOpts() & META_KEY);
				const bool recEnemyOnly = (c.GetOpts() & META_KEY) && (c.GetOpts() & CONTROL_KEY);
				const bool recSpecial = !!(c.GetOpts() & CONTROL_KEY);

				ReclaimOption recopt = REC_NORESCHECK;
				if (recUnits)     recopt |= REC_UNITS;
				if (recEnemyOnly) recopt |= REC_ENEMYONLY;
				if (recSpecial)   recopt |= REC_SPECIAL;

				const int rid = FindReclaimTarget(pos, radius, c.GetOpts(), recopt, curdist);
				if ((rid > 0) && (rid != uid)) {
					StopMoveAndFinishCommand();
					CBuilderCaches::RemoveUnitFromReclaimers(owner);
					CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
					return;
				}
			}
		}

		if (uid >= unitHandler.MaxUnits()) { // reclaim feature
			CFeature* feature = featureHandler.GetFeature(uid - unitHandler.MaxUnits());

			if (feature != nullptr) {
				bool featureBeingResurrected = CBuilderCaches::IsFeatureBeingResurrected(feature->id, owner);
				featureBeingResurrected &= c.IsInternalOrder();

				if (featureBeingResurrected || !ReclaimObject(feature)) {
					StopMoveAndFinishCommand();
					CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
				} else {
					CBuilderCaches::AddUnitToFeatureReclaimers(owner);
				}
			} else {
				StopMoveAndFinishCommand();
				CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
			}

			CBuilderCaches::RemoveUnitFromReclaimers(owner);
		} else { // reclaim unit
			CUnit* unit = unitHandler.GetUnit(uid);

			if (unit != nullptr && c.GetNumParams() == 5) {
				const float3& pos = c.GetPos(1);
				const float radius = c.GetParam(4) + 100.0f; // do not walk too far outside reclaim area

				const bool outOfReclaimRange =
					(pos.SqDistance2D(unit->pos) > radius * radius) ||
					(ownerBuilder->curReclaim == unit && unit->IsMoving() && !IsInBuildRange(unit));
				const bool busyAlliedBuilder =
					unit->unitDef->builder &&
					!unit->commandAI->commandQue.empty() &&
					teamHandler.Ally(owner->allyteam, unit->allyteam);

				if (outOfReclaimRange || busyAlliedBuilder) {
					StopMoveAndFinishCommand();
					CBuilderCaches::RemoveUnitFromReclaimers(owner);
					CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
					return;
				}
			}

			if (unit != nullptr && unit != owner && unit->unitDef->reclaimable && UpdateTargetLostTimer(unit->id) && unit->AllowedReclaim(owner)) {
				if (!ReclaimObject(unit)) {
					StopMoveAndFinishCommand();
				} else {
					CBuilderCaches::AddUnitToReclaimers(owner);
				}
			} else {
				CBuilderCaches::RemoveUnitFromReclaimers(owner);
				StopMoveAndFinishCommand();
			}

			CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
		}
	} else if (c.GetNumParams() == 4) {
		// area reclaim
		const float3 pos = c.GetPos(0);
		const float radius = c.GetParam(3);
		const bool recUnits = !!(c.GetOpts() & META_KEY);
		const bool recEnemyOnly = (c.GetOpts() & META_KEY) && (c.GetOpts() & CONTROL_KEY);
		const bool recSpecial = !!(c.GetOpts() & CONTROL_KEY);

		CBuilderCaches::RemoveUnitFromReclaimers(owner);
		CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
		ownerBuilder->StopBuild();

		ReclaimOption recopt = REC_NORESCHECK;
		if (recUnits)     recopt |= REC_UNITS;
		if (recEnemyOnly) recopt |= REC_ENEMYONLY;
		if (recSpecial)   recopt |= REC_SPECIAL;

		if (FindReclaimTargetAndReclaim(pos, radius, c.GetOpts(), recopt)) {
			inCommand = false;
			owner->commandAI->SlowUpdate();
			return;
		}

		if (!(c.GetOpts() & ALT_KEY))
			StopMoveAndFinishCommand();

	} else {
		// wrong number of parameters
		CBuilderCaches::RemoveUnitFromReclaimers(owner);
		CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
		StopMoveAndFinishCommand();
	}
}


bool CBuilderCmdBehaviourAI::ResurrectObject(CFeature *feature) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (MoveInBuildRange(feature, true)) {
		ownerBuilder->SetResurrectTarget(feature);
		return true;
	}

	return (owner->moveType->progressState != AMoveType::Failed);
}


void CBuilderCmdBehaviourAI::ExecuteResurrect(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	// not all builders are resurrect-capable by default
	if (!owner->unitDef->canResurrect)
		return;

	if (c.GetNumParams() == 1) {
		const unsigned int id = (unsigned int) c.GetParam(0);

		if (id >= unitHandler.MaxUnits()) { // resurrect feature
			CFeature* feature = featureHandler.GetFeature(id - unitHandler.MaxUnits());

			if (feature && feature->udef != nullptr) {
				if ((c.IsInternalOrder() && !(c.GetOpts() & CONTROL_KEY) && CBuilderCaches::IsFeatureBeingReclaimed(feature->id, owner)) ||
					!ResurrectObject(feature)) {
					CBuilderCaches::RemoveUnitFromResurrecters(owner);
					StopMoveAndFinishCommand();
				}
				else {
					CBuilderCaches::AddUnitToResurrecters(owner);
				}
			} else {
				CBuilderCaches::RemoveUnitFromResurrecters(owner);

				if (ownerBuilder->lastResurrected && unitHandler.GetUnitUnsafe(ownerBuilder->lastResurrected) != nullptr && owner->unitDef->canRepair) {
					// resurrection finished, start repair (by overwriting the current order)
					c = Command(CMD_REPAIR, c.GetOpts() | INTERNAL_ORDER, ownerBuilder->lastResurrected);

					ownerBuilder->lastResurrected = 0;
					inCommand = false;
					owner->commandAI->SlowUpdate();
					return;
				}

				StopMoveAndFinishCommand();
			}
		} else { // resurrect unit
			CBuilderCaches::RemoveUnitFromResurrecters(owner);
			StopMoveAndFinishCommand();
		}
	} else if (c.GetNumParams() == 4) {
		// area resurrect
		const float3 pos = c.GetPos(0);
		const float radius = c.GetParam(3);

		if (FindResurrectableFeatureAndResurrect(pos, radius, c.GetOpts(), (c.GetOpts() & META_KEY))) {
			inCommand = false;
			owner->commandAI->SlowUpdate();
			return;
		}

		if (!(c.GetOpts() & ALT_KEY))
			StopMoveAndFinishCommand();

	} else {
		// wrong number of parameters
		CBuilderCaches::RemoveUnitFromResurrecters(owner);
		StopMoveAndFinishCommand();
	}
}


void CBuilderCmdBehaviourAI::ExecutePatrol(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!owner->unitDef->canPatrol)
		return;

	if (c.GetNumParams() < 3)
		return;

	auto& commandQue = owner->commandAI->commandQue;

	Command temp(CMD_FIGHT, c.GetOpts() | INTERNAL_ORDER, c.GetPos(0));

	commandQue.push_back(c);
	commandQue.pop_front();
	commandQue.push_front(temp);
	Command tmpC(CMD_PATROL);
	eoh->CommandFinished(*owner, tmpC);
	owner->commandAI->SlowUpdate();
}


void CBuilderCmdBehaviourAI::ExecuteFight(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);

	float3 cPos1;
	float3 cPos2;
	if (cai) {
		cPos1 = cai->commandPos1;
		cPos2 = cai->commandPos1;
	}
	assert(c.IsInternalOrder() || owner->unitDef->canFight);

	if (cai != nullptr && cai->tempOrder) {
		cai->tempOrder = false;
		inCommand = true;
	}
	if (c.GetNumParams() < 3) {
		LOG_L(L_ERROR, "[BuilderCAI::%s][f=%d][id=%d][#c.params=%d min=3]", __func__, gs->frameNum, owner->id, c.GetNumParams());
		return;
	}

	if (c.GetNumParams() >= 6) {
		if (cai != nullptr && !inCommand) {
			cai->commandPos1 = c.GetPos(3);
			cPos1 = c.GetPos(3);
		} else if (cai == nullptr) {
			cPos1 = c.GetPos(3);
		}

	} else if (cai != nullptr) {
		// Some hackery to make sure the line (commandPos1,commandPos2) is NOT
		// rotated (only shortened) if we reach this because the previous return
		// fight command finished by the 'if((curPos-pos).SqLength2D()<(64*64)){'
		// condition, but is actually updated correctly if you click somewhere
		// outside the area close to the line (for a new command).
		if (f3SqDist(owner->pos, cai->commandPos1 = ClosestPointOnLine(cai->commandPos1, cai->commandPos2, owner->pos)) > Square(96.0f)) {
			cai->commandPos1 = owner->pos;
			cPos1 = cai->commandPos1;
		}
	}

	float3 pos = c.GetPos(0);
	if (cai != nullptr && !inCommand) {
		inCommand = true;
		cai->commandPos2 = pos;
		cPos2 = c.GetPos(3);
	} else if (cai == nullptr) {
		cPos2 = c.GetPos(3);
	}

	float3 curPosOnLine = ClosestPointOnLine(cPos1, cPos2, owner->pos);

	if (c.GetNumParams() >= 6)
		pos = curPosOnLine;

	if (cai != nullptr) {
		if (pos != owner->moveType->goalPos)
			SetGoal(pos, owner->pos);
	}

	const UnitDef* ownerDef = owner->unitDef;

	const bool resurrectMode = !!(c.GetOpts() & ALT_KEY);
	const bool reclaimEnemyMode = !!(c.GetOpts() & META_KEY);
	const bool reclaimEnemyOnlyMode = (c.GetOpts() & CONTROL_KEY) && (c.GetOpts() & META_KEY);

	ReclaimOption recopt;
	if (resurrectMode       ) recopt |= REC_NONREZ;
	if (reclaimEnemyMode    ) recopt |= REC_ENEMY;
	if (reclaimEnemyOnlyMode) recopt |= REC_ENEMYONLY;

	const float searchRadius = (owner->immobile ? 0.0f : (300.0f * owner->moveState)) + ownerBuilder->buildDistance;

	// Priority 1: Repair
	if (!reclaimEnemyOnlyMode && (ownerDef->canRepair || ownerDef->canAssist) && FindRepairTargetAndRepair(curPosOnLine, searchRadius, c.GetOpts(), true, resurrectMode)){
		if (cai != nullptr)
			cai->tempOrder = true;
		inCommand = false;

		if (lastPC1 != gs->frameNum) {  //avoid infinite loops
			lastPC1 = gs->frameNum;
			owner->commandAI->SlowUpdate();
		}

		return;
	}

	// Priority 2: Resurrect (optional)
	if (!reclaimEnemyOnlyMode && resurrectMode && ownerDef->canResurrect && FindResurrectableFeatureAndResurrect(curPosOnLine, searchRadius, c.GetOpts(), false)) {
		if (cai != nullptr)
			cai->tempOrder = true;
		inCommand = false;

		if (lastPC2 != gs->frameNum) {  //avoid infinite loops
			lastPC2 = gs->frameNum;
			owner->commandAI->SlowUpdate();
		}

		return;
	}

	// Priority 3: Reclaim / reclaim non resurrectable (optional) / reclaim enemy units (optional)
	if (ownerDef->canReclaim && FindReclaimTargetAndReclaim(curPosOnLine, searchRadius, c.GetOpts(), recopt)) {
		if (cai != nullptr)
			cai->tempOrder = true;
		inCommand = false;

		if (lastPC3 != gs->frameNum) {  //avoid infinite loops
			lastPC3 = gs->frameNum;
			owner->commandAI->SlowUpdate();
		}

		return;
	}

	if (cai != nullptr && f3SqDist(owner->pos, pos) < Square(64.0f)) {
		StopMoveAndFinishCommand();
		return;
	}

	if (owner->HaveTarget() && owner->moveType->progressState != AMoveType::Done) {
		StopMove();
	} else {
		SetGoal(owner->moveType->goalPos, owner->pos);
	}
}


void CBuilderCmdBehaviourAI::ExecuteRestore(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	if (!owner->unitDef->canRestore)
		return;

	if (inCommand) {
		if (!ownerBuilder->terraforming)
			StopMoveAndFinishCommand();

		return;
	}

	if (owner->unitDef->canRestore) {
		const float3 pos(c.GetParam(0), CGround::GetHeightReal(c.GetParam(0), c.GetParam(2)), c.GetParam(2));
		const float radius = std::min(c.GetParam(3), 200.0f);

		if (MoveInBuildRange(pos, radius * 0.7f)) {
			ownerBuilder->StartRestore(pos, radius);
			inCommand = true;
		}
	}
}


int CBuilderCmdBehaviourAI::GetDefaultCmd(const CUnit* pointed, const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (pointed != nullptr) {
		if (!teamHandler.Ally(gu->myAllyTeam, pointed->allyteam)) {
			if (owner->unitDef->canAttack && (owner->maxRange > 0.0f))
				return CMD_ATTACK;

			if (owner->unitDef->canReclaim && pointed->unitDef->reclaimable)
				return CMD_RECLAIM;
		} else {
			const bool canAssistPointed = ownerBuilder->CanAssistUnit(pointed);
			const bool canRepairPointed = ownerBuilder->CanRepairUnit(pointed);

			if (canAssistPointed)
				return CMD_REPAIR;
			if (canRepairPointed)
				return CMD_REPAIR;

			if (pointed->CanTransport(owner))
				return CMD_LOAD_ONTO;
			if (owner->unitDef->canGuard)
				return CMD_GUARD;
		}
	}

	if (feature != nullptr) {
		if (owner->unitDef->canResurrect && feature->udef != nullptr)
			return CMD_RESURRECT;

		if (owner->unitDef->canReclaim && feature->def->reclaimable)
			return CMD_RECLAIM;
	}

	return CMD_MOVE;
}


bool CBuilderCmdBehaviourAI::ReclaimObject(CSolidObject* object) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (MoveInBuildRange(object)) {
		ownerBuilder->SetReclaimTarget(object);
		return true;
	}

	return (owner->moveType->progressState != AMoveType::Failed);
}


int CBuilderCmdBehaviourAI::FindReclaimTarget(const float3& pos, float radius, unsigned char cmdopt, ReclaimOption recoptions, float bestStartDist) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& commandQue = owner->commandAI->commandQue;
	const bool noResCheck   = recoptions & REC_NORESCHECK;
	const bool recUnits     = recoptions & REC_UNITS;
	const bool recNonRez    = recoptions & REC_NONREZ;
	const bool recEnemy     = recoptions & REC_ENEMY;
	const bool recEnemyOnly = recoptions & REC_ENEMYONLY;
	const bool recSpecial   = recoptions & REC_SPECIAL;

	const CSolidObject* best = nullptr;
	float bestDist = bestStartDist;
	bool stationary = false;
	int rid = -1;

	if (recUnits || recEnemy || recEnemyOnly) {
		QuadFieldQuery qfQuery;
		quadField.GetUnitsExact(qfQuery, pos, radius, false);

		for (const CUnit* u: *qfQuery.units) {
			if (u == owner)
				continue;
			if (!u->unitDef->reclaimable)
				continue;
			if (!((!recEnemy && !recEnemyOnly) || !teamHandler.Ally(owner->allyteam, u->allyteam)))
				continue;
			if (!(u->losStatus[owner->allyteam] & (LOS_INRADAR|LOS_INLOS)))
				continue;

			// reclaim stationary targets first
			if (u->IsMoving() && stationary)
				continue;

			// do not reclaim friendly builders that are busy
			if (u->unitDef->builder && teamHandler.Ally(owner->allyteam, u->allyteam) && !u->commandAI->commandQue.empty())
				continue;

			const float dist = f3SqDist(u->pos, owner->pos);
			if (dist < bestDist || (!stationary && !u->IsMoving())) {
				if (owner->immobile && !IsInBuildRange(u))
					continue;

				if (!stationary && !u->IsMoving())
					stationary = true;

				bestDist = dist;
				best = u;
			}
		}
		if (best != nullptr)
			rid = best->id;
	}

	if ((!best || !stationary) && !recEnemyOnly) {
		best = nullptr;
		const CTeam* team = teamHandler.Team(owner->team);
		QuadFieldQuery qfQuery;
		quadField.GetFeaturesExact(qfQuery, pos, radius, false);
		bool metal = false;

		for (const CFeature* f: *qfQuery.features) {
			if (!f->def->reclaimable)
				continue;
			if (!recSpecial && !f->def->autoreclaim)
				continue;

			if (recNonRez && f->udef != nullptr)
				continue;

			if (recSpecial && metal && f->defResources.metal <= 0.0)
				continue;

			const float dist = f3SqDist(f->pos, owner->pos);

			if ((dist < bestDist || (recSpecial && !metal && f->defResources.metal > 0.0)) &&
				(noResCheck ||
				((f->defResources.metal  > 0.0f) && (team->res.metal  < team->resStorage.metal)) ||
				((f->defResources.energy > 0.0f) && (team->res.energy < team->resStorage.energy)))
			) {
				if (!f->IsInLosForAllyTeam(owner->allyteam))
					continue;

				if (!owner->unitDef->canmove && !IsInBuildRange(f))
					continue;

				if (CBuilderCaches::IsFeatureBeingResurrected(f->id, owner))
					continue;

				metal |= (recSpecial && !metal && f->defResources.metal > 0.0f);

				bestDist = dist;
				best = f;
			}
		}

		if (best != nullptr)
			rid = unitHandler.MaxUnits() + best->id;
	}

	return rid;
}


/******************************************************************************/
//
//  Area searches
//

bool CBuilderCmdBehaviourAI::FindReclaimTargetAndReclaim(const float3& pos, float radius, unsigned char cmdopt, ReclaimOption recoptions)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& commandQue = owner->commandAI->commandQue;
	const int rid = FindReclaimTarget(pos, radius, cmdopt, recoptions);

	if (rid < 0)
		return false;

	// FIGHT commands always resource check
	if (!(recoptions & REC_NORESCHECK))
		PushOrUpdateReturnFight();

	Command c(CMD_RECLAIM, cmdopt | INTERNAL_ORDER, rid, pos);
	c.PushParam(radius);
	commandQue.push_front(c);
	return true;
}


bool CBuilderCmdBehaviourAI::FindResurrectableFeatureAndResurrect(
	const float3& pos,
	float radius,
	unsigned char options,
	bool freshOnly
) {
	RECOIL_DETAILED_TRACY_ZONE;
	auto& commandQue = owner->commandAI->commandQue;
	QuadFieldQuery qfQuery;
	quadField.GetFeaturesExact(qfQuery, pos, radius, false);

	const CFeature* best = nullptr;
	float bestDist = 1.0e30f;

	for (const CFeature* f: *qfQuery.features) {
		if (f->udef == nullptr)
			continue;

		if (!f->IsInLosForAllyTeam(owner->allyteam))
			continue;

		if (freshOnly && f->reclaimLeft < 1.0f)
			continue;

		const float dist = f3SqDist(f->pos, owner->pos);
		if (dist < bestDist) {
			// dont lock-on to units outside of our reach (for immobile builders)
			if (owner->immobile && !IsInBuildRange(f))
				continue;

			if (!(options & CONTROL_KEY) && CBuilderCaches::IsFeatureBeingReclaimed(f->id, owner))
				continue;

			bestDist = dist;
			best = f;
		}
	}

	if (best != nullptr) {
		commandQue.push_front(Command(CMD_RESURRECT, options | INTERNAL_ORDER, unitHandler.MaxUnits() + best->id));
		return true;
	}

	return false;
}


bool CBuilderCmdBehaviourAI::FindCaptureTargetAndCapture(
	const float3& pos,
	float radius,
	unsigned char options,
	bool healthyOnly
) {
	RECOIL_DETAILED_TRACY_ZONE;
	auto& commandQue = owner->commandAI->commandQue;
	QuadFieldQuery qfQuery;
	quadField.GetUnitsExact(qfQuery, pos, radius, false);

	const CUnit* best = nullptr;
	float bestDist = 1.0e30f;
	bool stationary = false;

	const bool ctrlOpt = (options & CONTROL_KEY);

	for (const CUnit* unit: *qfQuery.units) {
		const bool isAlliedUnit = teamHandler.Ally(owner->allyteam, unit->allyteam);
		const bool isVisibleUnit = (unit->losStatus[owner->allyteam] & (LOS_INRADAR | LOS_INLOS));
		const bool isCapturableUnit = !unit->beingBuilt && unit->unitDef->capturable;

		const bool isEnemyTarget = ((ctrlOpt && owner->team != unit->team) || !isAlliedUnit);
		const bool isValidTarget = ((unit != owner) && isVisibleUnit && isCapturableUnit);

		if (isEnemyTarget && isValidTarget) {
			// capture stationary targets first
			if (unit->IsMoving() && stationary)
				continue;

			if (healthyOnly && unit->health < unit->maxHealth && unit->captureProgress <= 0.0f)
				continue;

			const float dist = f3SqDist(unit->pos, owner->pos);

			if (dist < bestDist || (!stationary && !unit->IsMoving())) {
				if (!owner->unitDef->canmove && !IsInBuildRange(unit))
					continue;

				stationary |= (!stationary && !unit->IsMoving());

				bestDist = dist;
				best = unit;
			}
		}
	}

	if (best != nullptr) {
		commandQue.push_front(Command(CMD_CAPTURE, options | INTERNAL_ORDER, best->id));
		return true;
	}

	return false;
}


bool CBuilderCmdBehaviourAI::FindRepairTargetAndRepair(
	const float3& pos,
	float radius,
	unsigned char options,
	bool attackEnemy,
	bool builtOnly
) {
	RECOIL_DETAILED_TRACY_ZONE;
	auto& commandQue = owner->commandAI->commandQue;
	QuadFieldQuery qfQuery;
	quadField.GetUnitsExact(qfQuery, pos, radius, false);
	const CUnit* bestUnit = nullptr;

	const float maxSpeed = owner->moveType->GetMaxSpeed();
	float unitSpeed = 0.0f;
	float bestDist = 1.0e30f;

	bool haveEnemy = false;
	bool trySelfRepair = false;
	bool stationary = false;

	for (const CUnit* unit: *qfQuery.units) {
		if (teamHandler.Ally(owner->allyteam, unit->allyteam)) {
			if (!haveEnemy && (unit->health < unit->maxHealth)) {
				// don't help allies build unless set on roam
				if (unit->beingBuilt && owner->team != unit->team && (owner->moveState != MOVESTATE_ROAM))
					continue;

				// don't help factories produce units when set on hold pos
				if (unit->beingBuilt && unit->moveDef != nullptr && (owner->moveState == MOVESTATE_HOLDPOS))
					continue;

				// don't assist or repair if can't assist or repair
				if (!ownerBuilder->CanAssistUnit(unit) && !ownerBuilder->CanRepairUnit(unit))
					continue;

				if (unit == owner) {
					trySelfRepair = true;
					continue;
				}
				// repair stationary targets first
				if (unit->IsMoving() && stationary)
					continue;

				if (builtOnly && unit->beingBuilt)
					continue;

				float dist = f3SqDist(unit->pos, owner->pos);

				// avoid targets that are faster than our max speed
				if (unit->IsMoving()) {
					unitSpeed = unit->speed.Length2D();
					dist *= (1.0f + std::max(unitSpeed - maxSpeed, 0.0f));
				}
				if (dist < bestDist || (!stationary && !unit->IsMoving())) {
					// dont lock-on to units outside of our reach (for immobile builders)
					if ((owner->immobile || (unit->IsMoving() && !TargetInterceptable(unit, unitSpeed))) && !IsInBuildRange(unit))
						continue;

					// don't repair stuff that's being reclaimed
					if (!(options & CONTROL_KEY) && CBuilderCaches::IsUnitBeingReclaimed(unit, owner))
						continue;

					stationary |= (!stationary && !unit->IsMoving());

					bestDist = dist;
					bestUnit = unit;
				}
			}
		} else {
			if (unit->IsNeutral())
				continue;

			if (!attackEnemy || !owner->unitDef->canAttack || (owner->maxRange <= 0) )
				continue;

			if (!(unit->losStatus[owner->allyteam] & (LOS_INRADAR | LOS_INLOS)))
				continue;

			const float dist = f3SqDist(unit->pos, owner->pos);

			if ((dist < bestDist) || !haveEnemy) {
				if (owner->immobile && ((dist - unit->buildeeRadius) > owner->maxRange))
					continue;

				bestUnit = unit;
				bestDist = dist;
				haveEnemy = true;
			}
		}
	}

	if (bestUnit == nullptr) {
		if (!trySelfRepair || !owner->unitDef->canSelfRepair || (owner->health >= owner->maxHealth))
			return false;

		bestUnit = owner;
	}

	if (!haveEnemy) {
		if (attackEnemy)
			PushOrUpdateReturnFight();

		Command c(CMD_REPAIR, options | INTERNAL_ORDER, bestUnit->id, pos);
		c.PushParam(radius);
		commandQue.push_front(c);
	} else {
		PushOrUpdateReturnFight(); // attackEnemy must be true
		commandQue.push_front(Command(CMD_ATTACK, options | INTERNAL_ORDER, bestUnit->id));
	}

	return true;
}


/*
bool CBuilderCmdBehaviourAI::BuggerOff(const float3& pos, float radius) {
	RECOIL_DETAILED_TRACY_ZONE;
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	if (owner->unitDef->IsStaticBuilderUnit())
		return true;
	return false;
}*/

void CBuilderCmdBehaviourAI::PushOrUpdateReturnFight() {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->PushOrUpdateReturnFight();
	else
		LOG("NO PushOrUpdateReturnFight!!");
}

void CBuilderCmdBehaviourAI::StopMoveAndFinishCommand() {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->StopMoveAndFinishCommand();
	else
		owner->commandAI->FinishCommand();
}

void CBuilderCmdBehaviourAI::StopMove() {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->StopMove();
	else
		LOG("NO StopMove!!");
}

void CBuilderCmdBehaviourAI::SetGoal(const float3& pos, const float3& curPos, float goalRadius) {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->SetGoal(pos, curPos, goalRadius);
	else
		LOG("NO SetGoal!!");
}

void CBuilderCmdBehaviourAI::SetGoal(const float3& pos, const float3& curPos, float goalRadius, float speed) {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->SetGoal(pos, curPos, goalRadius, speed);
	else
		LOG("NO SetGoal!!");
}

void CBuilderCmdBehaviourAI::StartSlowGuard(float speed) {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->StartSlowGuard(speed);
	else
		LOG("NO StartSlowGuard!!");
}

void CBuilderCmdBehaviourAI::StopSlowGuard() {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->StopSlowGuard();
	else
		LOG("NO StopSlowGuard!!");
}

void CBuilderCmdBehaviourAI::NonMoving() {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		cai->NonMoving();
	else
		LOG("NO NonMoving!!");
}

int CBuilderCmdBehaviourAI::UpdateTargetLostTimer(int unitID) {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		return cai->UpdateTargetLostTimer(unitID);
	else {
		LOG("NO UpdateTargetLostTimer!!");
		return 1;
	}
}

void CBuilderCmdBehaviourAI::StopMoveAndKeepPointing(const float3& p, const float r, bool b) {
	CMobileCAI* cai = dynamic_cast<CMobileCAI*>(owner->commandAI);
	if (cai)
		return cai->StopMoveAndKeepPointing(p, r, b);
	else
		LOG("NO StopMoveAndKeepPointing!!");
}

