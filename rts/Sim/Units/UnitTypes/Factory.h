/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _FACTORY_H
#define _FACTORY_H

#include "Building.h"
#include "Sim/Misc/NanoPieceCache.h"
#include "Sim/Units/CommandAI/Command.h"
#include "System/float3.h"

struct UnitDef;
struct Command;
class CFactory;
class CFeature;

class CFactory : public CBuilding
{
public:
	CR_DECLARE(CFactory)

	CFactory();

	void StartBuild(const UnitDef* buildeeDef);
	void UpdateBuild(CUnit* buildee);
	void FinishBuild(CUnit* buildee);
	void StopBuild(bool callScript = false);
	bool UpdateReclaim(const Command& fCommand);
	//bool UpdateResurrect(const Command& fCommand);
	/// @return whether the to-be-built unit is enqueued
	unsigned int QueueBuild(const UnitDef* buildeeDef, const Command& buildCmd);

	void Update();

	void DependentDied(CObject* o);
	//void CreateNanoParticle(bool highPriority = false);
	void CreateNanoParticle(const float3& goal, float radius, bool inverse, bool highPriority = false);

	/// supply the build piece to speed up
	float3 CalcBuildPos(int buildPiece = -1);

	void KillUnit(CUnit* attacker, bool selfDestruct, bool reclaimed, int weaponDefID);
	void PreInit(const UnitLoadParams& params);
	bool ChangeTeam(int newTeam, ChangeType type);

	const NanoPieceCache& GetNanoPieceCache() const { return nanoPieceCache; }
	      NanoPieceCache& GetNanoPieceCache()       { return nanoPieceCache; }

	void SetRepairTarget(CUnit* target);
	void SetReclaimTarget(CSolidObject* object);
	//void SetResurrectTarget(CFeature* feature);
	bool ScriptStartBuilding(float3 pos, bool silent);
	bool CanAssistUnit(const CUnit* u, const UnitDef* def = nullptr) const;
	bool CanRepairUnit(const CUnit* u) const;
	inline float f3SqLen(const float3& a) const { return (range3D ? a.SqLength() : a.SqLength2D()); }
	inline float f3SqDist(const float3& a, const float3& b) const { return (f3SqLen(a - b)); }
	inline float f3Dist(const float3& a, const float3& b) const { return (f3Len(a - b)); }
	inline float f3Len(const float3& a) const { return (range3D ? a.Length() : a.Length2D()); }
private:
	void SendToEmptySpot(CUnit* unit);
	void AssignBuildeeOrders(CUnit* unit);
	bool StartBuild(BuildInfo& buildInfo, CFeature*& feature, bool& inWaitStance, bool& limitReached);

public:
	float buildSpeed;
	float reclaimSpeed;

	//BuggerOff fine tuning
	float boOffset;
	float boRadius;
	int boRelHeading;
	bool boSherical;
	bool boForced;
	bool boPerform;

	CUnit* curBuild;

	enum {
		FACTORY_SKIP_BUILD_ORDER = 0,
		FACTORY_KEEP_BUILD_ORDER = 1,
		FACTORY_NEXT_BUILD_ORDER = 2,
	};

	bool range3D; ///< spheres instead of infinite cylinders for range tests
	float buildDistance;

	CSolidObject* curReclaim;
private:
	const UnitDef* curBuildDef;
	int lastBuildUpdateFrame;

	Command finishedBuildCommand;

	NanoPieceCache nanoPieceCache;


	CFeature* curResurrect;
	int lastResurrected;
	CUnit* curCapture;
	bool reclaimingUnit;

	bool terraforming;
	/*float terraformHelp;
	float myTerraformLeft;*/
	enum TerraformType {
		Terraform_Building,
		Terraform_Restore
	} terraformType;
	int tx1,tx2,tz1,tz2;
	float3 terraformCenter;
	float terraformRadius;

};

#endif // _FACTORY_H
