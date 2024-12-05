/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GuardRemove.h"

#include "Sim/Units/UnitDef.h"

#include "System/EventHandler.h"
#include "System/Misc/TracyDefs.h"


GuardRemove::GuardRemove(const char *name, int priority)
: CGadget(name, priority)
{
}

void GuardRemove::UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua)
{
	if (!(command.GetOpts() & SHIFT_KEY))
		return;
	if (unit->unitDef->builder && !unit->unitDef->IsFactoryUnit())
	{
		const CCommandQueue& queue = unit->commandAI->commandQue;
		if (queue.size() == 0)
			return;
		auto cmd = queue[queue.size()-1];
		if (cmd.GetID() == CMD_GUARD || cmd.GetID() == CMD_PATROL) {
			auto newCmd = Command(CMD_REMOVE, 0, command.GetTag());
			unit->commandAI->GiveCommand(newCmd, -1, false, false);
		}
	}
}

