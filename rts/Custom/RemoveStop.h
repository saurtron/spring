/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef REMOVE_STOP_H
#define REMOVE_STOP_H

#include "System/UnorderedSet.hpp"

#include "CGadget.h"

class RemoveStop : public CGadget
{
public:
	ENABLE_GADGET_EVENTS
	RemoveStop(const char *name, int priority, bool synced);

	// CEventClient interface
	virtual void UnitCreated(const CUnit* unit, const CUnit* builder) override;
	virtual bool AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua) override;
	//virtual void Update() override;
private:
	void Init();

	spring::unordered_set<int> stopRemoveDefs;
};

#endif // REMOVE_STOP_H
