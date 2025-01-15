/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BEHAVIOUR_H
#define BEHAVIOUR_H

#include "System/Object.h"

struct Command;
class CUnit;
class CObject;
struct UnitLoadParams;

class CBehaviour : public CObject
{
public:
	CR_DECLARE(CBehaviour)

	CBehaviour();
	CBehaviour(CUnit *owner);
	virtual ~CBehaviour() {};

	virtual void PreInit(const UnitLoadParams& params) {};
	virtual void Activate() {};
	virtual void Deactivate() {};
	virtual void UpdatePre() { };
	virtual void SlowUpdate() {};
	virtual void DependentDied(CObject* o) {};
	virtual void KillUnit(CUnit* attacker, bool selfDestruct, bool reclaimed, int weaponDefID = 0) {};

	virtual void Execute(Command& c) {};

	CUnit *owner;
};

#endif // BEHAVIOUR_H
