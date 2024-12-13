/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef UNIT_IMMOBILE_BUILDER_H
#define UNIT_IMMOBILE_BUILDER_H

#include "CGadget.h"

class UnitImmobileBuilder : public CGadget
{
public:
	ENABLE_GADGET_EVENTS
	UnitImmobileBuilder(const char *name, int priority, bool synced);

	// CEventClient interface
	virtual void UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua) override;
	virtual void PlayerChanged(int playerID) override;
	virtual void GameStart() override;
	virtual void UnitCreated(const CUnit* unit, const CUnit* builder) override;
	virtual void UnitGiven(const CUnit* unit, int oldTeam, int newTeam) override;
	virtual void UnitIdle(const CUnit* unit) override;

private:

	void Init();
	void MaybeRemoveSelf(bool gamestart);
	void SetupUnit(const CUnit *unit, bool openingCmd);
	bool TestUnit(const CUnit *unit);
};

#endif // UNIT_IMMOBILE_BUILDER_H
