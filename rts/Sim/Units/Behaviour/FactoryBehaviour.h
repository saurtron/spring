/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _FACTORY_H
#define _FACTORY_H

#include "Sim/Units/Unit.h"
#include "BaseBuilderBehaviour.h"
#include "Sim/Units/CommandAI/Command.h"
#include "System/float3.h"

struct UnitDef;
struct Command;

class CFactoryBehaviour : public CBaseBuilderBehaviour
{
public:
	CR_DECLARE(CFactoryBehaviour)

	CFactoryBehaviour();
	CFactoryBehaviour(CUnit* owner);

	void StartBuild(const UnitDef* buildeeDef);
	void UpdateBuild(CUnit* buildee);
	void FinishBuild(CUnit* buildee);
	void StopBuild();
	/// @return whether the to-be-built unit is enqueued
	unsigned int QueueBuild(const UnitDef* buildeeDef, const Command& buildCmd);

	virtual void UpdatePre() override;

	virtual void DependentDied(CObject* o) override;
	void CreateNanoParticle(bool highPriority = false);

	/// supply the build piece to speed up
	float3 CalcBuildPos(int buildPiece = -1);

	void KillUnit(CUnit* attacker, bool selfDestruct, bool reclaimed, int weaponDefID);
	void PreInit(const UnitLoadParams& params);
	bool ChangeTeam(int newTeam, CUnit::ChangeType type);
	//bool ChangeTeam(int newTeam, int type); //TODO

private:
	void SendToEmptySpot(CUnit* unit);
	void AssignBuildeeOrders(CUnit* unit);

public:
	//BuggerOff fine tuning
	float boOffset;
	float boRadius;
	int boRelHeading;
	bool boSherical;
	bool boForced;
	bool boPerform;

	enum {
		FACTORY_SKIP_BUILD_ORDER = 0,
		FACTORY_KEEP_BUILD_ORDER = 1,
		FACTORY_NEXT_BUILD_ORDER = 2,
	};

private:
	const UnitDef* curBuildDef;
	int lastBuildUpdateFrame;

	Command finishedBuildCommand;

	bool IsStunned();
};

#endif // _FACTORY_H
