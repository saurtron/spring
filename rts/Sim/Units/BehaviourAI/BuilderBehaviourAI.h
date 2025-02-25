/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _BUILDER_BEHAVIOUR_AI_H_
#define _BUILDER_BEHAVIOUR_AI_H_

#include "BaseBuilderBehaviourAI.h"
#include "Sim/Units/CommandAI/MobileCAI.h"
#include "Sim/Units/BuildInfo.h"
#include "System/Misc/BitwiseEnum.h"
#include "System/UnorderedSet.hpp"

#include <vector>

class CUnit;
class CBuilderBehaviour;
class CFeature;
class CSolidObject;
class CWorldObject;
struct Command;
struct UnitDef;


class CBuilderBehaviourAI : public CBaseBuilderBehaviourAI
{
public:
	CR_DECLARE(CBuilderBehaviourAI)
	CBuilderBehaviourAI(CUnit* owner);
	CBuilderBehaviourAI();
	~CBuilderBehaviourAI();

	void PostLoad();

	virtual int GetDefaultCmd(const CUnit* unit, const CFeature* feature) override;
	virtual bool SlowUpdate() override;

	virtual void FinishCommand() override;
	bool GiveCommandReal(const Command& c, bool fromSynced = true) override;
	virtual bool BuggerOff(const float3& pos, float radius);

	void ExecuteBuildCmd(Command& c);
	bool ExecuteStop(Command& c) override;

	bool IsInBuildRange(const CWorldObject* obj) const;
	bool IsInBuildRange(const float3& pos, const float radius) const;
	float GetBuildRange(const float targetRadius) const;

	spring::unordered_set<int> buildOptions;

private:

	bool MoveInBuildRange(const CWorldObject* obj, const bool checkMoveTypeForFailed = false);
	bool MoveInBuildRange(const float3& pos, float radius, const bool checkMoveTypeForFailed = false);

	bool IsBuildPosBlocked(const BuildInfo& bi, const CUnit** nanoFrame) const;
	bool IsBuildPosBlocked(const BuildInfo& bi) const {
		const CUnit* u = nullptr;
		return IsBuildPosBlocked(build, &u);
	}

	void CancelRestrictedUnit();

	inline float f3Dist(const float3& a, const float3& b) const {
		return range3D ? a.distance(b) : a.distance2D(b);
	}
	inline float f3SqDist(const float3& a, const float3& b) const {
		return range3D ? a.SqDistance(b) : a.SqDistance2D(b);
	}
	//inline float f3Len(const float3& a) const {
	//	return range3D ? a.Length() : a.Length2D();
	//}
	//inline float f3SqLen(const float3& a) const {
	//	return range3D ? a.SqLength() : a.SqLength2D();
	//}

	float GetBuildOptionRadius(const UnitDef* unitdef, int cmdId);

private:
	CBuilderBehaviour* ownerBuilder;

	bool building;
	BuildInfo build;

	int cachedRadiusId;
	float cachedRadius;

	int buildRetries;
	int randomCounter; ///< used to balance intervals of time intensive ai optimizations

	bool range3D;

	void StopMove();
	void StopMoveAndFinishCommand();
	void StopMoveAndKeepPointing(const float3& p, const float r, bool b);
	void NonMoving();
	void SetGoal(const float3& pos, const float3& curPos, float goalRadius = SQUARE_SIZE);
	void SetGoal(const float3& pos, const float3& curPos, float goalRadius, float speed);
};

#endif // _BUILDER_BEHAVIOUR_AI_H_
