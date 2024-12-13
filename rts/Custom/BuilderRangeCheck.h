/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BUILDER_RANGE_CHECK_H
#define BUILDER_RANGE_CHECK_H

#include <unordered_map>

#include "System/UnorderedSet.hpp"
#include "CGadget.h"

class BuilderRangeCheck : public CGadget
{
public:
	ENABLE_GADGET_EVENTS
	BuilderRangeCheck(const char *name, int priority, bool synced);

	// CEventClient interface
	virtual void GameFrame(int frameNum) override;
	virtual bool AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua) override;
	virtual void UnitDestroyed(const CUnit* unit, const CUnit* attacker, int weaponDefID) override;

	const char *name = "BuilderRangeCheck";

private:
	bool CheckDistance(const CUnit *unit, int targetID);

	std::unordered_map<int, spring::unordered_set<int>> trackingTable;
	bool debug = false;
	bool gate = false;
};

#endif // BUILDER_RANGE_CHECK_H
