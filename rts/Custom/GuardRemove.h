/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GUARD_REMOVE
#define GUARD_REMOVE

#include "System/EventClient.h"

#include "System/UnorderedSet.hpp"

class GuardRemove : public CEventClient
{
public:
	// CEventClient interface
	virtual void UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua) override;
	//virtual void Update() override;

	static void SetEnabled(bool enable);
	static bool IsEnabled() { return (instance != nullptr); }

private:
	GuardRemove();

	static GuardRemove* instance;
};

#endif // GUARD_REMOVE
