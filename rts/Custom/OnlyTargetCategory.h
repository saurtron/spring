/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef ONLY_TARGET_CATEGORY_H
#define ONLY_TARGET_CATEGORY_H

#include "System/UnorderedSet.hpp"
#include "System/UnorderedMap.hpp"

#include "CGadget.h"

class OnlyTargetCategory : public CGadget
{
public:
	OnlyTargetCategory(const char *name, int priority, bool synced);

	// CEventClient interface
	virtual bool AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua) override;
private:
	void Init();

	spring::unordered_map<int, unsigned int> unitOnlyTargetsCategory;
	spring::unordered_set<int> unitDontAttackGround;
};

#endif // ONLY_TARGET_CATEGORY_H
