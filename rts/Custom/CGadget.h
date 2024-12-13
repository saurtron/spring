/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GADGET_H
#define GADGET_H

#include "System/EventClient.h"

class CGadget;

class CGadgetFactory
{
	public:
	virtual ~CGadgetFactory() {}
	virtual const char* GetName() {return "";}
	virtual CGadget* Create(int priority=0) {return nullptr;};
};

template <typename T>
class Factory : public CGadgetFactory
{
	public:
		Factory(const char* gname, int gpriority=19991, bool gsynced=false) {
			name = gname;
			priority = gpriority;
			synced = gsynced;
		}
		CGadget* Create(int gpriority=0) override {

			return new T(name, (gpriority > 0) ? gpriority : priority, synced);
		}
		const char *GetName() override {
			return name;
		}
		const char *name;
		int priority;
		int synced;
};

class CGadget : public CEventClient {
public:
	CGadget(const char *name, int priority, bool synced);
	virtual ~CGadget() {}

	virtual void EnableEvents();

	virtual void UnitCommand(const CUnit* unit, const Command& command, int playerNum, bool fromSynced, bool fromLua) override {};
	virtual void PlayerChanged(int playerID) override {};
	virtual void GameStart() override {};
	virtual void UnitCreated(const CUnit* unit, const CUnit* builder) override {};
	virtual void UnitGiven(const CUnit* unit, int oldTeam, int newTeam) override {};
	virtual void UnitIdle(const CUnit* unit) override {};
	virtual void GameFrame(int frameNum) override {};
	virtual bool AllowCommand(const CUnit* unit, const Command& cmd, int playerNum, bool fromSynced, bool fromLua) override {return true;};
	virtual void UnitDestroyed(const CUnit* unit, const CUnit* attacker, int weaponDefID) override {};

	bool IsEnabled() { return enabled; };
	void Enable();
	void Disable();
	bool enabled;
};

#endif // GADGET_H
