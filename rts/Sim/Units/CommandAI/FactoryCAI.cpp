/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "FactoryCAI.h"
#include "ExternalAI/EngineOutHandler.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Game/GameHelper.h"
#include "Game/GlobalUnsynced.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/WaitCommandsAI.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Units/BuildInfo.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitLoader.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitTypes/Factory.h"
#include "System/Log/ILog.h"
#include "System/creg/STL_Map.h"
#include "System/StringUtil.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Units/UnitTypes/Builder.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/CommandAI/BuilderCaches.h"

#include "Sim/Misc/ModInfo.h"

#include "System/Misc/TracyDefs.h"

CR_BIND_DERIVED(CFactoryCAI ,CCommandAI , )

CR_REG_METADATA(CFactoryCAI , (
	CR_MEMBER(range3D),
	CR_MEMBER(ownerFactory),
	CR_MEMBER(randomCounter),
	CR_MEMBER(newUnitCommands),
	CR_MEMBER(buildOptions),
	CR_MEMBER(lastPC1),
	CR_MEMBER(lastPC2),
	CR_MEMBER(lastPC3),
	CR_PREALLOC(GetPreallocContainer)
))

static std::string GetUnitDefBuildOptionToolTip(const UnitDef* ud, bool disabled) {
	std::string tooltip;

	if (disabled) {
		tooltip = "\xff\xff\x22\x22" "DISABLED: " "\xff\xff\xff\xff";
	} else {
		tooltip = "Build: ";
	}

	tooltip += (ud->humanName + " - " + ud->tooltip);
	tooltip += ("\nHealth "      + FloatToString(ud->health,      "%.0f"));
	tooltip += ("\nMetal cost "  + FloatToString(ud->cost.metal,  "%.0f"));
	tooltip += ("\nEnergy cost " + FloatToString(ud->cost.energy, "%.0f"));
	tooltip += ("\nBuild time "  + FloatToString(ud->buildTime,   "%.0f"));

	return tooltip;
}



CFactoryCAI::CFactoryCAI():
	ownerFactory(nullptr),
	lastPC1(-1),
	lastPC2(-1),
	lastPC3(-1),
	range3D(true),
	randomCounter(0),
	CCommandAI()
{}


CFactoryCAI::CFactoryCAI(CUnit* owner):
	CCommandAI(owner),
	randomCounter(0),
	lastPC1(-1),
	lastPC2(-1),
	lastPC3(-1),
	range3D(owner->unitDef->buildRange3D)
{
	ownerFactory = static_cast<CFactory*>(owner);
	commandQue.SetQueueType(CCommandQueue::BuildQueueType);
	newUnitCommands.SetQueueType(CCommandQueue::NewUnitQueueType);

	if (owner->unitDef->canmove) {
		SCommandDescription c;

		c.id        = CMD_MOVE;
		c.type      = CMDTYPE_ICON_MAP;

		c.action    = "move";
		c.name      = "Move";
		c.tooltip   = c.name + ": Order ready built units to move to a position";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

	if (owner->unitDef->canPatrol) {
		SCommandDescription c;

		c.id        = CMD_PATROL;
		c.type      = CMDTYPE_ICON_MAP;

		c.action    = "patrol";
		c.name      = "Patrol";
		c.tooltip   = c.name + ": Order ready built units to patrol to one or more waypoints";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

	if (owner->unitDef->canFight) {
		SCommandDescription c;

		c.id        = CMD_FIGHT;
		c.type      = CMDTYPE_ICON_MAP;

		c.action    = "fight";
		c.name      = "Fight";
		c.tooltip   = c.name + ": Order ready built units to take action while moving to a position";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

	if (owner->unitDef->canGuard) {
		SCommandDescription c;

		c.id        = CMD_GUARD;
		c.type      = CMDTYPE_ICON_UNIT;

		c.action    = "guard";
		c.name      = "Guard";
		c.tooltip   = c.name + ": Order ready built units to guard another unit and attack units attacking it";
		c.mouseicon = c.name;
		possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
	}

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


	CFactory* fac = static_cast<CFactory*>(owner);

	for (const auto& bi: fac->unitDef->buildOptions) {
		const std::string& name = bi.second;
		const UnitDef* ud = unitDefHandler->GetUnitDefByName(name);

		if (ud == nullptr) {
			string errmsg = "MOD ERROR: loading ";
			errmsg += name.c_str();
			errmsg += " for ";
			errmsg += owner->unitDef->name;
			throw content_error(errmsg);
		}

		{
			SCommandDescription c;

			c.id   = -ud->id; // build-options are always negative
			c.type = CMDTYPE_ICON;

			c.action    = "buildunit_" + StringToLower(ud->name);
			c.name      = name;
			c.mouseicon = c.name;
			c.tooltip   = GetUnitDefBuildOptionToolTip(ud, c.disabled = (ud->maxThisUnit <= 0));

			buildOptions[c.id] = 0;
			possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
		}
	}
}


static constexpr int GetCountMultiplierFromOptions(int opts)
{
	// The choice of keys and their associated multipliers are from OTA.
	int ret = 1;
	if (opts &   SHIFT_KEY) ret *=  5;
	if (opts & CONTROL_KEY) ret *= 20;
	return ret;
}


void CFactoryCAI::ExecuteGuard(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!owner->unitDef->canGuard)
		return;

	CUnit* guardee = unitHandler.GetUnit(c.GetParam(0));

	if (guardee == nullptr) {
		FinishCommand();
		return;
	}

	if (guardee == owner) {
		FinishCommand();
		return;
	}
	if (UpdateTargetLostTimer(guardee->id) == 0) {
		FinishCommand();
		return;
	}
	if (guardee->outOfMapTime > (GAME_SPEED * 5)) {
		FinishCommand();
		return;
	}

	if (CBuilder* b = dynamic_cast<CBuilder*>(guardee)) {
		if (b->terraforming) {
			if (IsInBuildRange(b->terraformCenter, b->terraformRadius * 0.7f)) {
				//ownerFactory->HelpTerraform(b);
			}
			return;
		} else if (b->curReclaim && owner->unitDef->canReclaim) {
			if (!ReclaimObject(b->curReclaim)) {
				//StopMove();
			}
			return;
		} else if (b->curResurrect && owner->unitDef->canResurrect) {
			/*if (!ResurrectObject(b->curResurrect)) {
				//StopMove();
			}*/
			return;
		} else {
			ownerFactory->StopBuild();
		}

		const bool pushRepairCommand =
			(  b->curBuild != nullptr) &&
			(  b->curBuild->soloBuilder == nullptr || b->curBuild->soloBuilder == owner) &&
			(( b->curBuild->beingBuilt && owner->unitDef->canAssist) ||
			( !b->curBuild->beingBuilt && owner->unitDef->canRepair));

		if (pushRepairCommand) {
			Command nc(CMD_REPAIR, c.GetOpts(), b->curBuild->id);

			commandQue.push_front(nc);
			inCommand = false;
			SlowUpdate();
			return;
		}
	}

	if (CFactory* fac = dynamic_cast<CFactory*>(guardee)) {
		const bool pushRepairCommand =
			(  fac->curBuild != nullptr) &&
			(  fac->curBuild->soloBuilder == nullptr || fac->curBuild->soloBuilder == owner) &&
			(( fac->curBuild->beingBuilt && owner->unitDef->canAssist) ||
			 (!fac->curBuild->beingBuilt && owner->unitDef->canRepair));

		if (pushRepairCommand) {
			commandQue.push_front(Command(CMD_REPAIR, c.GetOpts(), fac->curBuild->id));
			inCommand = false;
			// SlowUpdate();
			return;
		}
	}

	if (!(c.GetOpts() & CONTROL_KEY) && CBuilderCaches::IsUnitBeingReclaimed(guardee, owner))
		return;

	const float3 pos    = guardee->pos;
	const float  radius = (guardee->immobile) ? guardee->buildeeRadius : guardee->buildeeRadius * 0.8f; // in case of mobile units reduce radius a bit

	if (IsInBuildRange(pos, radius)) {
		const bool pushRepairCommand =
			(  guardee->health < guardee->maxHealth) &&
			(  guardee->soloBuilder == nullptr || guardee->soloBuilder == owner) &&
			(( guardee->beingBuilt && owner->unitDef->canAssist) ||
			 (!guardee->beingBuilt && owner->unitDef->canRepair));

		if (pushRepairCommand) {
			commandQue.push_front(Command(CMD_REPAIR, c.GetOpts(), guardee->id));
			inCommand = false;
			return;
		}

		//NonMoving();
	}
}


void CFactoryCAI::ExecuteRepair(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// not all builders are repair-capable by default
	if (!owner->unitDef->canRepair)
		return;

	if (c.GetNumParams() == 1 || c.GetNumParams() == 5) {
		// repair unit
		CUnit* unit = unitHandler.GetUnit(c.GetParam(0));

		if (unit == nullptr) {
			FinishCommand();
			return;
		}

		if (tempOrder && owner->moveState <= MOVESTATE_MANEUVER) {
			// limit how far away we go when not roaming
			if (LinePointDist(commandPos1, commandPos2, unit->pos) > std::max(500.0f, GetBuildRange(unit->buildeeRadius))) {
				FinishCommand();
				return;
			}
		}

		if (c.GetNumParams() == 5) {
			if (!IsInBuildRange(unit)) {
				FinishCommand();
				return;
			}
			/*const float3& pos = c.GetPos(1);
			const float radius = c.GetParam(4) + 100.0f; // do not walk too far outside repair area

			if ((pos - unit->pos).SqLength2D() > radius * radius ||
				(unit->IsMoving() && ((c.IsInternalOrder() && !TargetInterceptable(unit, unit->speed.Length2D())) || ownerFactory->curBuild == unit)
				&& !IsInBuildRange(unit))) {
				FinishCommand();
				return;
			}*/
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
			if (IsInBuildRange(unit))
				ownerFactory->SetRepairTarget(unit);
		} else {
			FinishCommand();
		}
	} else if (c.GetNumParams() == 4) {
		// area repair
		const float3 pos = c.GetPos(0);
		const float radius = c.GetParam(3);

		ownerFactory->StopBuild();
		if (FindRepairTargetAndRepair(pos, radius, c.GetOpts(), false, (c.GetOpts() & META_KEY))) {
			inCommand = false;
			SlowUpdate();
			return;
		}

		if (!(c.GetOpts() & ALT_KEY))
			FinishCommand();

	} else {
		FinishCommand();
	}

}

void CFactoryCAI::ExecuteFight(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(c.IsInternalOrder() || owner->unitDef->canFight);

	if (tempOrder) {
		tempOrder = false;
		inCommand = true;
	}
	if (c.GetNumParams() < 3) {
		LOG_L(L_ERROR, "[BuilderCAI::%s][f=%d][id=%d][#c.params=%d min=3]", __func__, gs->frameNum, owner->id, c.GetNumParams());
		return;
	}

	if (c.GetNumParams() >= 6) {
		if (!inCommand)
			commandPos1 = c.GetPos(3);

	} else {
		// Some hackery to make sure the line (commandPos1,commandPos2) is NOT
		// rotated (only shortened) if we reach this because the previous return
		// fight command finished by the 'if((curPos-pos).SqLength2D()<(64*64)){'
		// condition, but is actually updated correctly if you click somewhere
		// outside the area close to the line (for a new command).
		if (f3SqDist(owner->pos, commandPos1 = ClosestPointOnLine(commandPos1, commandPos2, owner->pos)) > Square(96.0f))
			commandPos1 = owner->pos;
	}

	float3 pos = c.GetPos(0);
	if (!inCommand) {
		inCommand = true;
		commandPos2 = pos;
	}

	float3 curPosOnLine = ClosestPointOnLine(commandPos1, commandPos2, owner->pos);

	if (c.GetNumParams() >= 6)
		pos = curPosOnLine;

	/*if (pos != owner->moveType->goalPos)
		SetGoal(pos, owner->pos);*/

	const UnitDef* ownerDef = owner->unitDef;

	const bool resurrectMode = !!(c.GetOpts() & ALT_KEY);
	const bool reclaimEnemyMode = !!(c.GetOpts() & META_KEY);
	const bool reclaimEnemyOnlyMode = (c.GetOpts() & CONTROL_KEY) && (c.GetOpts() & META_KEY);

	ReclaimOption recopt;
	if (resurrectMode       ) recopt |= REC_NONREZ;
	if (reclaimEnemyMode    ) recopt |= REC_ENEMY;
	if (reclaimEnemyOnlyMode) recopt |= REC_ENEMYONLY;

	const float searchRadius = (owner->immobile ? 0.0f : (300.0f * owner->moveState)) + ownerFactory->buildDistance;

	// Priority 1: Repair
	if (!reclaimEnemyOnlyMode && (ownerDef->canRepair || ownerDef->canAssist) && FindRepairTargetAndRepair(curPosOnLine, searchRadius, c.GetOpts(), true, resurrectMode)){
		tempOrder = true;
		inCommand = false;

		if (lastPC1 != gs->frameNum) {  //avoid infinite loops
			lastPC1 = gs->frameNum;
			SlowUpdate();
		}

		return;
	}

	// Priority 2: Resurrect (optional)
	/*if (!reclaimEnemyOnlyMode && resurrectMode && ownerDef->canResurrect && FindResurrectableFeatureAndResurrect(curPosOnLine, searchRadius, c.GetOpts(), false)) {
		tempOrder = true;
		inCommand = false;

		if (lastPC2 != gs->frameNum) {  //avoid infinite loops
			lastPC2 = gs->frameNum;
			SlowUpdate();
		}

		return;
	}*/

	// Priority 3: Reclaim / reclaim non resurrectable (optional) / reclaim enemy units (optional)
	if (ownerDef->canReclaim && FindReclaimTargetAndReclaim(curPosOnLine, searchRadius, c.GetOpts(), recopt)) {
		tempOrder = true;
		inCommand = false;

		if (lastPC3 != gs->frameNum) {  //avoid infinite loops
			lastPC3 = gs->frameNum;
			SlowUpdate();
		}

		return;
	}

	if (f3SqDist(owner->pos, pos) < Square(64.0f)) {
		FinishCommand();
		return;
	}

	/*if (owner->HaveTarget() && owner->moveType->progressState != AMoveType::Done) {
		StopMove();
	} else {
		SetGoal(owner->moveType->goalPos, owner->pos);
	}*/
}


bool CFactoryCAI::ReclaimObject(CSolidObject* object) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (IsInBuildRange(object)) {
		ownerFactory->SetReclaimTarget(object);
		return true;
	}

	return false;
}

/*
bool CFactoryCAI::ResurrectObject(CFeature *feature) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (IsInBuildRange(feature)) {
		ownerFactory->SetResurrectTarget(feature);
		return true;
	}

	return false;
}*/


float CFactoryCAI::GetBuildRange(const float targetRadius) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (ownerFactory->buildDistance);
}


bool CFactoryCAI::IsInBuildRange(const CWorldObject* obj) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return IsInBuildRange(obj->pos, obj->buildeeRadius);
}

bool CFactoryCAI::IsInBuildRange(const float3& objPos, const float objRadius) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float immDistSqr = f3SqDist(owner->pos, objPos);
	const float buildDist = GetBuildRange(objRadius);

	return (immDistSqr <= (buildDist * buildDist));
}

void CFactoryCAI::GiveCommandReal(const Command& c, bool fromSynced)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int cmdID = c.GetID();

	// move is always allowed for factories (passed to units it produces)
	if ((cmdID != CMD_MOVE) && !AllowedCommand(c, fromSynced))
		return;

	CCommandQueue* queue = &commandQue;
	if (buildOptions.size() > 0)
		queue = &newUnitCommands;

	auto boi = buildOptions.find(cmdID);

	// not a build order (or a build order we do not support, eg. if multiple
	// factories of different types were selected) so queue it to built units
	if (boi == buildOptions.end()) {
		if (cmdID < 0)
			return;

		if (nonQueingCommands.find(cmdID) != nonQueingCommands.end()) {
			CCommandAI::GiveAllowedCommand(c);
			return;
		}

		if (cmdID == CMD_INSERT || cmdID == CMD_REMOVE) {
			CCommandAI::GiveAllowedCommand(c);
			return;
		}

		if (!(c.GetOpts() & SHIFT_KEY) && (cmdID == CMD_WAIT || cmdID == CMD_SELFD)) {
			CCommandAI::GiveAllowedCommand(c);
			return;
		}

		if (!(c.GetOpts() & SHIFT_KEY)) {
			waitCommandsAI.ClearUnitQueue(owner, *queue);
			CCommandAI::ClearCommandDependencies();
			queue->clear();
		}

		CCommandAI::AddCommandDependency(c);

		if (cmdID != CMD_STOP) {
			if ((cmdID == CMD_WAIT) || (cmdID == CMD_SELFD)) {
				if (!queue->empty() && (queue->back().GetID() == cmdID)) {
					if (cmdID == CMD_WAIT) {
						waitCommandsAI.RemoveWaitCommand(owner, c);
					}
					queue->pop_back();
				} else {
					queue->push_back(c);
				}
			} else {
				bool dummy;
				if (CancelCommands(c, *queue, dummy) > 0) {
					return;
				} else {
					if (GetOverlapQueued(c, *queue).empty()) {
						queue->push_back(c);
					} else {
						return;
					}
				}
			}
		}

		// the first new-unit build order can not be WAIT or SELFD
		while (!queue->empty() && queue == &newUnitCommands) {
			const Command& newUnitCommand = newUnitCommands.front();
			const int id = newUnitCommand.GetID();

			if ((id == CMD_WAIT) || (id == CMD_SELFD)) {
				if (cmdID == CMD_WAIT) {
					waitCommandsAI.RemoveWaitCommand(owner, c);
				}
				newUnitCommands.pop_front();
			} else {
				break;
			}
		}

		return;
	}

	int& numQueued = boi->second;
	int numItems = GetCountMultiplierFromOptions(c.GetOpts());

	if (c.GetOpts() & RIGHT_MOUSE_KEY) {
		numQueued -= numItems;
		numQueued  = std::max(numQueued, 0);

		int numToErase = numItems;
		if (c.GetOpts() & ALT_KEY) {
			for (unsigned int cmdNum = 0; cmdNum < commandQue.size() && numToErase; ++cmdNum) {
				if (commandQue[cmdNum].GetID() == cmdID) {
					commandQue[cmdNum] = Command(CMD_STOP);
					numToErase--;
				}
			}
		} else {
			for (int cmdNum = commandQue.size() - 1; cmdNum != -1 && numToErase; --cmdNum) {
				if (commandQue[cmdNum].GetID() == cmdID) {
					commandQue[cmdNum] = Command(CMD_STOP);
					numToErase--;
				}
			}
		}
	} else {
		if (c.GetOpts() & ALT_KEY) {
			Command nc(c);
			nc.SetOpts(nc.GetOpts() | INTERNAL_ORDER);
			for (int a = 0; a < numItems; ++a) {
				if (repeatOrders) {
					if (commandQue.empty()) {
						commandQue.push_front(nc);
					} else {
						commandQue.insert(commandQue.begin() + 1, nc);
					}
				} else {
					commandQue.push_front(c);
				}
			}

			if (!repeatOrders)
				static_cast<CFactory*>(owner)->StopBuild();

		} else {
			for (int a = 0; a < numItems; ++a) {
				commandQue.push_back(c);
			}
		}
		numQueued += numItems;
	}

	UpdateIconName(cmdID, numQueued);
	SlowUpdate();
}


void CFactoryCAI::InsertBuildCommand(CCommandQueue::iterator& it,
                                     const Command& newCmd)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto boi = buildOptions.find(newCmd.GetID());
	auto buildCount = GetCountMultiplierFromOptions(newCmd.GetOpts());
	if (boi != buildOptions.end()) {
		boi->second += buildCount;
		UpdateIconName(newCmd.GetID(), boi->second);
	}
	if (!commandQue.empty() && (it == commandQue.begin())) {
		// ExecuteStop(), without the pop_front()
		CFactory* fac = static_cast<CFactory*>(owner);
		fac->StopBuild();
	}
	while (buildCount--)
		it = commandQue.insert(it, newCmd);
}


bool CFactoryCAI::RemoveBuildCommand(CCommandQueue::iterator& it)
{
	RECOIL_DETAILED_TRACY_ZONE;
	Command& cmd = *it;
	const auto boi = buildOptions.find(cmd.GetID());
	if (boi != buildOptions.end()) {
		boi->second--;
		UpdateIconName(cmd.GetID(), boi->second);
	}
	if (!commandQue.empty() && (it == commandQue.begin())) {
		ExecuteStop(cmd);
		return true;
	}

	if (cmd.GetID() < 0) {
		// build command, convert into a stop command
		cmd = Command(CMD_STOP);
	}

	return false;
}


void CFactoryCAI::DecreaseQueueCount(const Command& buildCommand, int& numQueued)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// copy in case we get pop'ed
	// NOTE: the queue should not be empty at this point!
	const Command frontCommand = commandQue.empty()? Command(CMD_STOP): commandQue.front();

	if (!repeatOrders || buildCommand.IsInternalOrder())
		numQueued--;

	UpdateIconName(buildCommand.GetID(), numQueued);

	// if true, factory was set to wait and its buildee
	// could only have been finished by assisting units
	// --> make sure not to cancel the wait-order
	if (frontCommand.GetID() == CMD_WAIT)
		commandQue.pop_front();

	// can only finish the real build-command command if
	// we still have it in our queue (FinishCommand also
	// asserts this)
	if (!commandQue.empty())
		FinishCommand();

	if (frontCommand.GetID() == CMD_WAIT)
		commandQue.push_front(frontCommand);
}



// NOTE:
//   only called if Factory::QueueBuild returned FACTORY_NEXT_BUILD_ORDER
//   (meaning the order was not rejected and the callback was installed)
void CFactoryCAI::FactoryFinishBuild(const Command& command) {
	DecreaseQueueCount(command, buildOptions[command.GetID()]);
}

void CFactoryCAI::SlowUpdate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Commands issued may invoke SlowUpdate when paused
	if (gs->paused)
		return;
	if (commandQue.empty() || owner->beingBuilt)
		return;

	CFactory* fac = static_cast<CFactory*>(owner);

	while (!commandQue.empty()) {
		Command& c = commandQue.front();

		const size_t oldQueueSize = commandQue.size();

		if (buildOptions.find(c.GetID()) != buildOptions.end()) {
			// build-order
			switch (fac->QueueBuild(unitDefHandler->GetUnitDefByID(-c.GetID()), c)) {
				case CFactory::FACTORY_SKIP_BUILD_ORDER: {
					// order rejected and we want to skip it permanently
					DecreaseQueueCount(c, buildOptions[c.GetID()]);
				} break;
			}
		} else {
			// regular order (move/wait/etc)
			switch (c.GetID()) {
				case CMD_FIGHT:    { ExecuteFight(c); } break;
				case CMD_REPAIR:    { ExecuteRepair(c); } break;
				case CMD_RECLAIM:   { ExecuteReclaim(c); } break;
				case CMD_GUARD:     { ExecuteGuard(c); } break;
				case CMD_STOP: {
					ExecuteStop(c);
				} break;
				default: {
					CCommandAI::SlowUpdate();
				} break;
			}
		}

		// exit if no command was consumed
		if (oldQueueSize == commandQue.size())
			break;
	}
}


void CFactoryCAI::ExecuteStop(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CFactory* fac = static_cast<CFactory*>(owner);
	fac->StopBuild();

	commandQue.pop_front();
}

void CFactoryCAI::ExecuteReclaim(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
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
					FinishCommand();
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
					FinishCommand();
					CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
				} else {
					CBuilderCaches::AddUnitToFeatureReclaimers(owner);
				}
			} else {
				FinishCommand();
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
					(ownerFactory->curReclaim == unit && unit->IsMoving() && !IsInBuildRange(unit));
				const bool busyAlliedBuilder =
					unit->unitDef->builder &&
					!unit->commandAI->commandQue.empty() &&
					teamHandler.Ally(owner->allyteam, unit->allyteam);

				if (outOfReclaimRange || busyAlliedBuilder) {
					FinishCommand();
					CBuilderCaches::RemoveUnitFromReclaimers(owner);
					CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
					return;
				}
			}

			if (unit != nullptr && unit != owner && unit->unitDef->reclaimable && UpdateTargetLostTimer(unit->id) && unit->AllowedReclaim(owner)) {
				if (!ReclaimObject(unit)) {
					FinishCommand();
				} else {
					CBuilderCaches::AddUnitToReclaimers(owner);
				}
			} else {
				CBuilderCaches::RemoveUnitFromReclaimers(owner);
				FinishCommand();
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
		ownerFactory->StopBuild();

		ReclaimOption recopt = REC_NORESCHECK;
		if (recUnits)     recopt |= REC_UNITS;
		if (recEnemyOnly) recopt |= REC_ENEMYONLY;
		if (recSpecial)   recopt |= REC_SPECIAL;

		if (FindReclaimTargetAndReclaim(pos, radius, c.GetOpts(), recopt)) {
			inCommand = false;
			SlowUpdate();
			return;
		}

		if (!(c.GetOpts() & ALT_KEY))
			FinishCommand();

	} else {
		// wrong number of parameters
		CBuilderCaches::RemoveUnitFromReclaimers(owner);
		CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
		FinishCommand();
	}
}

bool CFactoryCAI::FindReclaimTargetAndReclaim(const float3& pos, float radius, unsigned char cmdopt, ReclaimOption recoptions)
{
	RECOIL_DETAILED_TRACY_ZONE;
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


int CFactoryCAI::FindReclaimTarget(const float3& pos, float radius, unsigned char cmdopt, ReclaimOption recoptions, float bestStartDist) const
{
	RECOIL_DETAILED_TRACY_ZONE;
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

				/*if (CBuilderCaches::IsFeatureBeingResurrected(f->id, owner))
					continue;*/

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

int CFactoryCAI::GetDefaultCmd(const CUnit* pointed, const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (pointed == nullptr)
		return CMD_MOVE;

	if (!teamHandler.Ally(gu->myAllyTeam, pointed->allyteam))
		return CMD_MOVE;

	if (!owner->unitDef->canGuard)
		return CMD_MOVE;

	return CMD_GUARD;
}


void CFactoryCAI::UpdateIconName(int cmdID, const int& numQueued)
{
	RECOIL_DETAILED_TRACY_ZONE;
	for (const SCommandDescription*& cd: possibleCommands) {
		if (cd->id != cmdID)
			continue;

		char t[32];
		SNPRINTF(t, 10, "%d", numQueued);

		SCommandDescription ucd = *cd;
		ucd.params.clear();

		if (numQueued > 0)
			ucd.params.push_back(t);

		commandDescriptionCache.DecRef(*cd);
		cd = commandDescriptionCache.GetPtr(std::move(ucd));
		break;
	}

	selectedUnitsHandler.PossibleCommandChange(owner);
}


bool CFactoryCAI::FindRepairTargetAndRepair(
	const float3& pos,
	float radius,
	unsigned char options,
	bool attackEnemy,
	bool builtOnly
) {
	RECOIL_DETAILED_TRACY_ZONE;
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
				if (!ownerFactory->CanAssistUnit(unit) && !ownerFactory->CanRepairUnit(unit))
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
					//if ((owner->immobile || (unit->IsMoving() && !TargetInterceptable(unit, unitSpeed))) && !IsInBuildRange(unit))
						continue;

					// don't repair stuff that's being reclaimed
					/*if (!(options & CONTROL_KEY) && CBuilderCaches::IsUnitBeingReclaimed(unit, owner))
						continue;

					stationary |= (!stationary && !unit->IsMoving());

					bestDist = dist;
					bestUnit = unit;*/
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
