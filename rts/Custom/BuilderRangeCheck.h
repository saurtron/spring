/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BUILDER_RANGE_CHECK
#define BUILDER_RANGE_CHECK

#include "System/EventClient.h"

class BuilderRangeCheck : public CEventClient
{
public:
	// CEventClient interface
	virtual void GameFrame(int frameNum) override;
	virtual bool AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua) override;
	virtual void UnitDestroyed(const CUnit* unit, const CUnit* attacker, int weaponDefID) override;

	static void SetEnabled(bool enable);
	static bool IsEnabled() { return (instance != nullptr); }

private:
	BuilderRangeCheck();

	static BuilderRangeCheck* instance;

	spring::unordered_map<int, std::vector<int>> trackingTable;
	bool debug = false;
	bool gate = false;
};

#endif // BUILDER_RANGE_CHECK
