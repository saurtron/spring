/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "UnitImmobileBuilder.h"

#include "Sim/Units/UnitDef.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Game/GlobalUnsynced.h"
#include "Map/ReadMap.h"
#include "Game/GameSetup.h"
#include "Sim/Units/UnitHandler.h"

#include "System/EventHandler.h"
#include "System/Misc/TracyDefs.h"
#include "System/SafeUtil.h"


UnitImmobileBuilder* UnitImmobileBuilder::instance = nullptr;

UnitImmobileBuilder::UnitImmobileBuilder()
: CEventClient("[UnitImmobileBuilder]", 199991, false)
{
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
	Init();
}

void UnitImmobileBuilder::SetEnabled(bool enable)
{
	if (!enable) {
		spring::SafeDelete(instance);
		return;
	}

	assert(instance == nullptr);
	instance = new UnitImmobileBuilder();
}

bool UnitImmobileBuilder::TestUnit(const CUnit *unit)
{
	if (unit->team != gu->myTeam)
		return false;
	if (unit->unitDef->builder && !unit->unitDef->IsFactoryUnit() && !unit->unitDef->canmove)
		return true;
	return false;
}

void UnitImmobileBuilder::MaybeRemoveSelf(bool gamestart)
{
   if (gu->spectating && (gs->frameNum > 0 || gamestart))
	   UnitImmobileBuilder::SetEnabled(false);
}


void UnitImmobileBuilder::UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua)
{
	if (!(command.GetOpts() & SHIFT_KEY))
		return;

	if (command.GetID() == CMD_FIGHT)
		return;

	if (TestUnit(unit)) {
		const CCommandQueue& queue = unit->commandAI->commandQue;
		if (queue.size() == 0)
			return;
		auto cmd = queue[queue.size()-1];
		if (cmd.GetID() == CMD_FIGHT) {
			auto newCmd = Command(CMD_REMOVE, 0, cmd.GetTag());
			unit->commandAI->GiveCommand(newCmd, -1, false, false);
		}
	}
}

void UnitImmobileBuilder::SetupUnit(const CUnit *unit, bool openingCmd)
{
	if (openingCmd) {
		//spGiveOrderToUnit(unitID, CMD_MOVE_STATE, { 1 }, 0)
		auto newCmd = Command(CMD_MOVE_STATE, 0, MOVESTATE_MANEUVER);
		// void GiveCommand(const Command& c, int playerNum, bool fromSynced       , bool fromLua); // net,Lua
		unit->commandAI->GiveCommand(newCmd, -1, false, false);
	}
	float3 pos = unit->pos;
	if (pos.x > mapDims.mapx/2)
		pos.x -= 50;
	else
		pos.x += 50;
	if (pos.z > mapDims.mapy/2)
		pos.z -= 50;
	else
		pos.z += 50;

	auto newCmd = Command(CMD_FIGHT, META_KEY, pos);
	unit->commandAI->GiveCommand(newCmd, -1, false, false);
}

void UnitImmobileBuilder::Init()
{
	if (gameSetup->hostDemo || gs->frameNum > 0)
		MaybeRemoveSelf(false);

	for (const CUnit* unit: unitHandler.GetUnitsByTeam(gu->myTeam)) {
		if (TestUnit(unit)) {
			SetupUnit(unit, true);
		}
	}
}

void UnitImmobileBuilder::PlayerChanged(int playerID)
{
	MaybeRemoveSelf(false);
}

void UnitImmobileBuilder::GameStart()
{
	MaybeRemoveSelf(true);
}

void UnitImmobileBuilder::UnitCreated(const CUnit* unit, const CUnit* builder)
{
	if (TestUnit(unit))
		SetupUnit(unit, true);
}

void UnitImmobileBuilder::UnitGiven(const CUnit* unit, int oldTeam, int newTeam)
{
	UnitCreated(unit, nullptr);
}

void UnitImmobileBuilder::UnitIdle(const CUnit* unit)
{
	if (TestUnit(unit))
		SetupUnit(unit, false);
}


