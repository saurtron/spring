/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BEHAVIOUR_AI_H
#define BEHAVIOUR_AI_H

#include "System/Object.h"
#include "System/float3.h"

struct Command;
class CUnit;
class CObject;
class CFeature;
struct UnitLoadParams;

class CBehaviourAI : public CObject
{
public:
	CR_DECLARE(CBehaviourAI)

	CBehaviourAI();
	CBehaviourAI(CUnit *owner);
	virtual ~CBehaviourAI() {};

	void* GetPreallocContainer() { return owner; }  // creg

	virtual bool Update() { return false; };
	virtual bool SlowUpdate() { return false; };
	virtual void Execute(Command& c) {};
	virtual void FinishCommand() {};
	virtual bool GiveCommandReal(const Command& c, bool fromSynced = true) { return false; };
	virtual int GetDefaultCmd(const CUnit* pointed, const CFeature* feature) = 0;
	virtual bool BuggerOff(const float3& pos, float radius) { return false; };

	CUnit *owner;
};

#endif // BEHAVIOUR_AI_H
