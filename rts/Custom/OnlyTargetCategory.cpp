/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "OnlyTargetCategory.h"

#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"

#include "System/EventHandler.h"
#include "System/Misc/TracyDefs.h"
#include "Sim/Misc/CategoryHandler.h"


OnlyTargetCategory::OnlyTargetCategory(const char *name, int priority, bool synced)
: CGadget(name, priority, synced)
{
	Init();
}

void OnlyTargetCategory::Init()
{
	RECOIL_DETAILED_TRACY_ZONE;

	CCategoryHandler* categoryHandler = CCategoryHandler::Instance();
	const unsigned vtolCategory = categoryHandler->GetCategories("VTOL");
	for (const UnitDef& unitDef: unitDefHandler->GetUnitDefsVec()) {
		int allCategories = 0;

		for (int i=0; i < unitDef.NumWeapons(); i++) {
			const UnitDefWeapon& weaponDef = unitDef.GetWeapon(i);
			allCategories |= weaponDef.onlyTargetCat;
		}
		if (allCategories == vtolCategory) {
			unitDontAttackGround.insert(unitDef.id);
		} else if (allCategories > 0 && !(allCategories & (allCategories - 1))) {
			// just one category
			unitOnlyTargetsCategory[unitDef.id] = allCategories;
		}
	}
}

bool OnlyTargetCategory::AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (cmd.GetID() != CMD_ATTACK || cmd.GetNumParams() == 0)
		return true;
	if (cmd.GetNumParams() > 1 && unitDontAttackGround.contains(unit->unitDef->id))
		return false;
	if (unitOnlyTargetsCategory.contains(unit->unitDef->id)) {
		const int targetID = (int) cmd.GetParam(0);
		const CUnit* target = unitHandler.GetUnit(targetID);
		if (!target)
			return false;
		if (!(target->unitDef->category & unitOnlyTargetsCategory[unit->unitDef->id]))
			return false;
	}
	return true;
}

