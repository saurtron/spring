/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "BuilderRangeCheck.h"

#include "Game/SelectedUnitsHandler.h"

#include "Sim/Units/UnitDef.h"

#include "Sim/Units/UnitHandler.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/CommandAI/FactoryCAI.h"

#include "System/EventHandler.h"
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

bool BuilderRangeCheck::CheckDistance(const CUnit *unit, int targetID)
{
	auto unitDef = unit->unitDef;
	const CUnit* target = unitHandler.GetUnit(targetID);
	const unsigned int maxUnits = unitHandler.MaxUnits();

	auto distance = std::numeric_limits<float>::max();
	if (targetID < maxUnits && target) {
		if (unitDef->buildRange3D)
			distance = unit->midPos.distance(target->midPos);
		else
			distance = unit->midPos.distance2D(target->midPos);
	} else {
		targetID = targetID - maxUnits;
		CFeature* targetFeature = featureHandler.GetFeature(targetID);
		if (targetFeature) {
			if (unitDef->buildRange3D)
				distance = unit->midPos.distance(targetFeature->midPos);
			else
				distance = unit->midPos.distance2D(targetFeature->midPos);
		}
	}
	if (distance > unitDef->buildDistance + unit->radius) {
		return false;
	}
	return true;
}

void BuilderRangeCheck::GameFrame(int frameNum)
{
	for(auto iter: trackingTable) {
		auto unit = unitHandler.GetUnit(iter.first);
		auto maxDistance = unit->unitDef->buildDistance;

		const CCommandAI* commandAI = unit->commandAI;
		const CFactoryCAI* factoryCAI = dynamic_cast<const CFactoryCAI*>(commandAI);
		const CCommandQueue& queue = (factoryCAI == nullptr)? commandAI->commandQue : factoryCAI->newUnitCommands;

		for(const Command& cmd: queue) {
			auto targetID = cmd.GetParam(0);
			const CUnit* target = unitHandler.GetUnit(targetID);
			if (target && !CheckDistance(unit, targetID)) {
				auto newCmd = Command(CMD_REMOVE, 0, cmd.GetTag());
				unit->commandAI->GiveCommand(newCmd, -1, true, true);
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
	auto targetID = cmd.GetParam(0);
	const CUnit* target = unitHandler.GetUnit(targetID);

	const unsigned int maxUnits = unitHandler.MaxUnits();
	if (targetID < maxUnits && target) {
		auto targetUnitDef = target->unitDef;
		if (targetUnitDef->canmove) {
			trackingTable[unit->id] = true;
			return true;
		}
	}

	return CheckDistance(unit, targetID);
}

void BuilderRangeCheck::UnitDestroyed(const CUnit* unit, const CUnit* attacker, int weaponDefID)
{
	trackingTable.erase(unit->id);
}

