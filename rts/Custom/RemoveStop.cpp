/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "RemoveStop.h"

#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"

#include "System/EventHandler.h"
#include "System/Misc/TracyDefs.h"


RemoveStop::RemoveStop(const char *name, int priority, bool synced)
: CGadget(name, priority, synced)
{
	Init();
}

void RemoveStop::Init()
{
	ZoneScopedN("M:RemoveStop::Init");

	for (const UnitDef& unitDef: unitDefHandler->GetUnitDefsVec()) {
		auto customParams = unitDef.customParams;
		if (customParams.contains("removestop")) {
			stopRemoveDefs.emplace(unitDef.id);
		}
	}

	for (const CUnit* unit: unitHandler.GetActiveUnits()) {
		UnitCreated(unit, nullptr);
	}
}

bool RemoveStop::AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua)
{
	ZoneScopedN("M:RemoveStop::AllowCommand");

	if (cmd.GetID() == CMD_STOP && stopRemoveDefs.contains(unit->unitDef->id))
		return false;
	return true;
}


void RemoveStop::UnitCreated(const CUnit* unit, const CUnit* builder)
{
	if (stopRemoveDefs.contains(unit->unitDef->id))
	{
		const std::vector<const SCommandDescription*>& cmdDescs = unit->commandAI->GetPossibleCommands();
		for (int i = 0; i < (int)cmdDescs.size(); i++) {
			if (cmdDescs[i]->id == CMD_STOP) {
				unit->commandAI->RemoveCommandDescription(i);
				return;
			}
		}
	}
}
