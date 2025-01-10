/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _FACTORY_AI_H_
#define _FACTORY_AI_H_

#include "CommandAI.h"
#include "CommandQueue.h"

#include <string>
#include "System/Misc/BitwiseEnum.h"
#include "System/UnorderedMap.hpp"

class CUnit;
class CFactory;
class CFeature;
class CWorldObject;
class CSolidObject;
struct Command;

class CFactoryCAI : public CCommandAI
{
public:
	CR_DECLARE(CFactoryCAI)

	CFactoryCAI(CUnit* owner);
	CFactoryCAI();

	int GetDefaultCmd(const CUnit* pointed, const CFeature* feature);
	void SlowUpdate();

	void GiveCommandReal(const Command& c, bool fromSynced = true);

	void InsertBuildCommand(CCommandQueue::iterator& it, const Command& c);
	bool RemoveBuildCommand(CCommandQueue::iterator& it);

	void DecreaseQueueCount(const Command& c, int& buildOption);
	void FactoryFinishBuild(const Command& command);
	void ExecuteStop(Command& c);

	virtual void ExecuteGuard(Command& c);
	virtual void ExecuteRepair(Command& c);
	virtual void ExecuteReclaim(Command& c);
	virtual void ExecuteFight(Command& c);
	bool ReclaimObject(CSolidObject* o);
	//bool ResurrectObject(CFeature* feature);

	CCommandQueue newUnitCommands;

	spring::unordered_map<int, int> buildOptions;

	bool tempOrder;
	float3 commandPos1;
	float3 commandPos2;

	int lastPC1; ///< helps avoid infinite loops
	int lastPC2;
	int lastPC3;

	void PushOrUpdateReturnFight() {
		CCommandAI::PushOrUpdateReturnFight(commandPos1, commandPos2);
	}

private:
	int randomCounter; ///< used to balance intervals of time intensive ai optimizations
	enum ReclaimOptions {
		REC_NORESCHECK = 1<<0,
		REC_UNITS      = 1<<1,
		REC_NONREZ     = 1<<2,
		REC_ENEMY      = 1<<3,
		REC_ENEMYONLY  = 1<<4,
		REC_SPECIAL    = 1<<5
	};
	typedef Bitwise::BitwiseEnum<ReclaimOptions> ReclaimOption;
	int FindReclaimTarget(const float3& pos, float radius, unsigned char cmdopt, ReclaimOption recoptions, float bestStartDist = 1.0e30f) const;
	bool FindReclaimTargetAndReclaim(const float3& pos, float radius, unsigned char cmdopt, ReclaimOption recoptions);

private:
	CFactory* ownerFactory;
	bool range3D;
	bool IsInBuildRange(const CWorldObject* obj) const;
	bool IsInBuildRange(const float3& pos, const float radius) const;
	float GetBuildRange(const float targetRadius) const;
	void UpdateIconName(int id, const int& numQueued);

	inline float f3Dist(const float3& a, const float3& b) const {
		return range3D ? a.distance(b) : a.distance2D(b);
	}

	inline float f3SqDist(const float3& a, const float3& b) const {
		return range3D ? a.SqDistance(b) : a.SqDistance2D(b);
	}

	bool FindRepairTargetAndRepair(const float3& pos, float radius, unsigned char options, bool attackEnemy, bool builtOnly);
};

#endif // _FACTORY_AI_H_
