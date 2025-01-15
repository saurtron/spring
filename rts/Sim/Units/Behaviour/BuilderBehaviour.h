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

	//bool ScriptStartBuilding(float3 pos, bool silent);

	virtual void PreInit(const UnitLoadParams& params) override;
	//void Update();
	void SlowUpdate() override;
	virtual void DependentDied(CObject* o) override;

	bool UpdateTerraform(const Command& fCommand);
	bool AssistTerraform(const Command& fCommand);
	bool UpdateBuild(const Command& fCommand);
	bool UpdateReclaim(const Command& fCommand);
	bool UpdateResurrect(const Command& fCommand);
	bool UpdateCapture(const Command& fCommand);

	bool StartBuild(BuildInfo& buildInfo, CFeature*& feature, bool& inWaitStance, bool& limitReached);
	float CalculateBuildTerraformCost(BuildInfo& buildInfo);
	void StopBuild(bool callScript = true);
	void SetRepairTarget(CUnit* target);
	void SetReclaimTarget(CSolidObject* object);
	void StartRestore(float3 centerPos, float radius);
	bool ScriptStartBuilding(float3 pos, bool silent);

	void HelpTerraform(CBuilderBehaviour* unit);
	void SetResurrectTarget(CFeature* feature);
	void SetCaptureTarget(CUnit* unit);

	bool CanAssistUnit(const CUnit* u, const UnitDef* def = nullptr) const;
	bool CanRepairUnit(const CUnit* u) const;

public:
	constexpr static int TERRA_SMOOTHING_RADIUS = 3;

	float buildDistance;
	float repairSpeed;
	float reclaimSpeed;
	float resurrectSpeed;
	float captureSpeed;
	float terraformSpeed;

	CFeature* curResurrect;
	int lastResurrected;
	CUnit* curCapture;
	CSolidObject* curReclaim;
	bool reclaimingUnit;
	CBuilderBehaviour* helpTerraform;

	bool terraforming;
	float terraformHelp;
	float myTerraformLeft;
	enum TerraformType {
		Terraform_Building,
		Terraform_Restore
	} terraformType;
	int tx1,tx2,tz1,tz2;
	float3 terraformCenter;
	float terraformRadius;

};

#endif // BUILDER_BEHAVIOUR_H
