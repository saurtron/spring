/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "BuilderRangeCheck.h"

#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitHandler.h"

#include "System/EventHandler.h"
#include "System/Misc/TracyDefs.h"


BuilderRangeCheck::BuilderRangeCheck(const char *name, int priority, bool synced)
: CGadget(name, priority, synced)
{
}

bool BuilderRangeCheck::CheckDistance(const CUnit *unit, int targetID)
{
	auto unitDef = unit->unitDef;
	const unsigned int maxUnits = unitHandler.MaxUnits();

	auto distance = std::numeric_limits<float>::max();

	CSolidObject* target;
	if (targetID >= unitHandler.MaxUnits()) {
		target = featureHandler.GetFeature(targetID - unitHandler.MaxUnits());
	} else {
		target = unitHandler.GetUnit(targetID);
	}

	if (unitDef->buildRange3D)
		distance = unit->midPos.distance(target->midPos);
	else
		distance = unit->midPos.distance2D(target->midPos);

	if (distance > unitDef->buildDistance + target->radius) {
		return false;
	}
	return true;
}

void BuilderRangeCheck::GameFrame(int frameNum)
{
	for(auto unitID: trackingTable) {
		auto unit = unitHandler.GetUnit(unitID);
		auto maxDistance = unit->unitDef->buildDistance;

		const CCommandQueue& queue = unit->commandAI->commandQue;

		for(const Command& cmd: queue) {
			auto targetID = cmd.GetParam(0);
			const CUnit* target = unitHandler.GetUnit(targetID);
			if (target && !CheckDistance(unit, targetID)) {
				auto newCmd = Command(CMD_REMOVE, 0, cmd.GetTag());
				unit->commandAI->GiveCommand(newCmd, -1, false, false);
			}
		}
		if (queue.size() == 1) {
			auto cmd = queue[0];
			if (cmd.GetID() == CMD_FIGHT) {
				trackingTable.erase(unit->id);
			}
		}
		if (queue.size() == 0) {
			trackingTable.erase(unit->id);
		}
	}/*
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
	}*/
}

bool BuilderRangeCheck::AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua)
{
	if (cmd.GetNumParams() != 1)
		return true;
	auto unitDef = unit->unitDef;
	const int targetID = (int) cmd.GetParam(0);
	const CUnit* target = unitHandler.GetUnit(targetID);

	const unsigned int maxUnits = unitHandler.MaxUnits();
	if (targetID < maxUnits && target) {
		auto targetUnitDef = target->unitDef;
		if (targetUnitDef->canmove) {
			trackingTable.insert(unit->id);
			return true;
		}
	}

	return CheckDistance(unit, targetID);
}

void BuilderRangeCheck::UnitDestroyed(const CUnit* unit, const CUnit* attacker, int weaponDefID)
{
	trackingTable.erase(unit->id);
}

