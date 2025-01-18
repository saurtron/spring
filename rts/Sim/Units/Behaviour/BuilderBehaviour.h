/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BUILDER_BEHAVIOUR_H
#define BUILDER_BEHAVIOUR_H

#include "BaseBuilderBehaviour.h"

#include <string>

#include "Sim/Misc/NanoPieceCache.h"
#include "System/float3.h"

struct UnitDef;
struct BuildInfo;
struct Command;
class CFeature;
class CSolidObject;

class CBuilderBehaviour : public CBaseBuilderBehaviour
{
public:
	CR_DECLARE(CBuilderBehaviour)

	CBuilderBehaviour();
	CBuilderBehaviour(CUnit* owner);

	virtual void UpdatePre() override;

	virtual void PreInit(const UnitLoadParams& params) override;
	void SlowUpdate() override;
	virtual void DependentDied(CObject* o) override;

	bool UpdateBuild(const Command& fCommand);

	bool StartBuild(BuildInfo& buildInfo, CFeature*& feature, bool& inWaitStance, bool& limitReached);
	float CalculateBuildTerraformCost(BuildInfo& buildInfo);
	void StopBuild(bool callScript = true);
	bool ScriptStartBuilding(float3 pos, bool silent);

public:
	constexpr static int TERRA_SMOOTHING_RADIUS = 3;

	float buildDistance;
};

#endif // BUILDER_BEHAVIOUR_H
