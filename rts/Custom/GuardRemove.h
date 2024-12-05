/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GUARD_REMOVE
#define GUARD_REMOVE

#include "GuardRemove.h"

#include "System/EventClient.h"

#include "CGadget.h"

class GuardRemove : public CGadget
{
public:
	GuardRemove(const char *name, int priority);

	// CEventClient interface
	virtual void UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua) override;
	//virtual void Update() override;
};

#endif // GUARD_REMOVE
