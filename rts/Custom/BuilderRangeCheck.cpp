/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "DebugDrawerQuadField.h"

#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/UI/MouseHandler.h"

#include "Map/Ground.h"
#include "Map/ReadMap.h"

#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Units/UnitHandler.h"

#include "Rendering/LineDrawer.h"

#include "System/EventHandler.h"
#include "System/GlobalConfig.h"
#include "System/Misc/TracyDefs.h"
#include "System/SafeUtil.h"


BuilderRangeCheck* BuilderRangeCheck::instance = nullptr;

BuilderRangeCheck::BuilderRangeCheck()
: CEventClient("[BuilderRangeCheck]", 199991, false)
{
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
}

void BuilderRangeCheck::SetEnabled(bool enable)
{
	if (!enable) {
		spring::SafeDelete(instance);
		return;
	}

	assert(instance == nullptr);
	instance = new BuilderRangeCheck();
}

bool BuilderRangeCheck::CheckDistance(CUnit *unit, int targetID)
{
	auto unitDef = unit->unitDef;
	const CUnit* target = unitHandler.GetUnit(targetID);
	auto maxUnits = unitHandler.MaxUnits(;)

	auto distance = std::numeric_limits<float>::max();
	if (targetId < maxUnits && target) {
		auto targetUnitDef = target->unitDef;
		if (targetUnitDef.canMove)
			return true
		if (unitDef.buildRange3D)
			distance = unit->midPos.distance(targetUnit->midPos);
		else
			distance = unit->midPos.distance2D(targetUnit->midPos);
	} else {
		targetId = targetID - maxUnits;
		CFeature* targetFeature = featureHandler.GetFeature(targetID);
		if (targetFeature) {
			if (unitDef.buildRange3D)
				distance = unit->midPos.distance(targetFeature->midPos);
			else
				distance = unit->midPos.distance2D(targetFeature->midPos);
		}
	}
	if (distance > unitDef.buildDistance + unitDef.range) {
		return false;
	}
	return true;
}

void BuilderRangeCheck::GameFrame(int frameNum)
{
	auto pointer = gs->GameFrame();
	for(auto unitID: trackingTable) {
		auto unitDef = unitHandler.GetUnit(unitID);
		auto maxDistance = unitDef.buildDistance;

		const CCommandAI* commandAI = unit->commandAI;
		const CFactoryCAI* factoryCAI = dynamic_cast<const CFactoryCAI*>(commandAI);
		const CCommandQueue* queue = (factoryCAI == nullptr)? &commandAI->commandQue : &factoryCAI->newUnitCommands;

		for(auto cmd: queue) {
			if (!CheckDistance(unit, targetID)) {
				trackingTable[unit->id].erase(idx);
				auto newCmd = Command(CMD_REMOVE, 0, curTag = cmd.GetTag());
				unit->commandAI->GiveCommand(newCmd, -1, true, true);
			}
		}
		if (queue.size() == 1) {
			auto cmd = queue[0];
			if (cmd.id == CMD_FIGHT) {
				trackingTable.erase(unit->id);
			}
		}
	}
	bool debug = false;
	if (debug) {
		if (gate && trackingTable.size() > 0) {
			LOG_L(LOG.INFO, "Tracking: %s, NanoRuns: %s, uProc: %s, dropped: %s, inside: %s.",
					trackingTable.size(),
					nanosRun,
					unitsCalced,
					removes,
					inside);
		}
		if (gate && trackingTable.size() == 0) {
			LOG_L(LOG.INFO, "Tracking stopped.");
			gate = false;
		}
	}
}

bool BuilderRangeCheck::AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua)
{
	if (cmd.GetNumParams() != 1)
		return true;
	auto unitDef = unit->unitDef;
	auto targetID = cmd.GetParam(0);
	return CheckDistance(targetID);
}

void BuilderRangeCheck::UnitDestroyed(const CUnit* unit, const CUnit* attacker, int weaponDefID)
{
	trackingTable.erase[unit->id];
	for(auto targets: trackingTable) {
		targets.erase[unit->id];
	}
}

