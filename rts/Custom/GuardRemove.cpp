/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GuardRemove.h"

#include "Game/SelectedUnitsHandler.h"

#include "Sim/Units/UnitDef.h"

#include "Sim/Units/UnitHandler.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/CommandAI/FactoryCAI.h"

#include "System/EventHandler.h"
#include "System/Misc/TracyDefs.h"
#include "System/SafeUtil.h"


GuardRemove* GuardRemove::instance = nullptr;

GuardRemove::GuardRemove()
: CEventClient("[GuardRemove]", 199991, false)
{
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
}

void GuardRemove::SetEnabled(bool enable)
{
	if (!enable) {
		spring::SafeDelete(instance);
		return;
	}

	assert(instance == nullptr);
	instance = new GuardRemove();
}

void GuardRemove::UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua)
{
	if (!(command.GetOpts() & SHIFT_KEY))
		return;
	if (unit->unitDef->builder && !unit->unitDef->IsFactoryUnit())
	{
		const CCommandAI* commandAI = unit->commandAI;
		const CFactoryCAI* factoryCAI = dynamic_cast<const CFactoryCAI*>(commandAI);
		const CCommandQueue& queue = (factoryCAI == nullptr)? commandAI->commandQue : factoryCAI->newUnitCommands;
		if (queue.size() == 0)
			return;
		auto cmd = queue[queue.size()-1];
		if (cmd.GetID() == CMD_GUARD || cmd.GetID() == CMD_PATROL) {
			auto newCmd = Command(CMD_REMOVE, 0, command.GetTag());
			unit->commandAI->GiveCommand(newCmd, -1, false, false);
		}
	}
}

