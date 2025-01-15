#include "BuilderBehaviour.h"

#include "Sim/Units/CommandAI/Command.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "System/SpringMath.h"
#include "System/Sound/ISoundChannels.h"
#include "Sim/Units/Scripts/CobInstance.h"
#include "Game/GlobalUnsynced.h"

// from Builder.cpp
#include <assert.h>
#include <algorithm>
//#include "Building.h"
#include "Game/GameHelper.h"
#include "Game/GlobalUnsynced.h"
#include "Map/Ground.h"
#include "Map/MapDamage.h"
#include "Map/ReadMap.h"
#include "System/SpringMath.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "Sim/Misc/ModInfo.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/MoveDefHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Units/BehaviourAI/BuilderBehaviourAI.h"
#include "Sim/Units/Scripts/CobInstance.h"
//#include "Sim/Units/CommandAI/BuilderCAI.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/BuilderCaches.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitLoader.h"
#include "System/EventHandler.h"
#include "System/Log/ILog.h"
#include "System/Sound/ISoundChannels.h"

#include "System/Misc/TracyDefs.h"

template CBuilderBehaviour* CUnit::GetBehaviour<CBuilderBehaviour>() const;

CR_BIND_DERIVED(CBuilderBehaviour, CBaseBuilderBehaviour, )

CR_REG_METADATA(CBuilderBehaviour, (
	CR_MEMBER(buildDistance),
	CR_MEMBER(repairSpeed),
	CR_MEMBER(reclaimSpeed),
	CR_MEMBER(resurrectSpeed),
	CR_MEMBER(captureSpeed),
	CR_MEMBER(terraformSpeed),
	CR_MEMBER(curResurrect),
	CR_MEMBER(lastResurrected),
	CR_MEMBER(curCapture),
	CR_MEMBER(curReclaim),
	CR_MEMBER(reclaimingUnit),
	CR_MEMBER(helpTerraform),
	CR_MEMBER(terraforming),
	CR_MEMBER(myTerraformLeft),
	CR_MEMBER(terraformHelp),
	CR_MEMBER(tx1), CR_MEMBER(tx2), CR_MEMBER(tz1), CR_MEMBER(tz2),
	CR_MEMBER(terraformCenter),
	CR_MEMBER(terraformRadius),
	CR_MEMBER(terraformType)
))

CBuilderBehaviour::CBuilderBehaviour():
	CBaseBuilderBehaviour(),
	buildDistance(16),
	repairSpeed(100),
	reclaimSpeed(100),
	resurrectSpeed(100),
	captureSpeed(100),
	terraformSpeed(100),
	curResurrect(0),
	lastResurrected(0),
	curCapture(0),
	curReclaim(0),
	reclaimingUnit(false),
	helpTerraform(0),
	terraforming(false),
	terraformHelp(0),
	myTerraformLeft(0),
	terraformType(Terraform_Building),
	tx1(0),
	tx2(0),
	tz1(0),
	tz2(0),
	terraformCenter(ZeroVector),
	terraformRadius(0)
{
}

CBuilderBehaviour::CBuilderBehaviour(CUnit* owner):
	CBaseBuilderBehaviour(owner),
	buildDistance(16),
	repairSpeed(100),
	reclaimSpeed(100),
	resurrectSpeed(100),
	captureSpeed(100),
	terraformSpeed(100),
	curResurrect(0),
	lastResurrected(0),
	curCapture(0),
	curReclaim(0),
	reclaimingUnit(false),
	helpTerraform(0),
	terraforming(false),
	terraformHelp(0),
	myTerraformLeft(0),
	terraformType(Terraform_Building),
	tx1(0),
	tx2(0),
	tz1(0),
	tz2(0),
	terraformCenter(ZeroVector),
	terraformRadius(0)
{
}

/* Builder.cpp */
using std::min;
using std::max;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


void CBuilderBehaviour::PreInit(const UnitLoadParams& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& unitDef = params.unitDef;
	range3D = unitDef->buildRange3D;
	buildDistance = (params.unitDef)->buildDistance;

	buildSpeed     = INV_GAME_SPEED * unitDef->buildSpeed;
	repairSpeed    = INV_GAME_SPEED * unitDef->repairSpeed;
	reclaimSpeed   = INV_GAME_SPEED * unitDef->reclaimSpeed;
	resurrectSpeed = INV_GAME_SPEED * unitDef->resurrectSpeed;
	captureSpeed   = INV_GAME_SPEED * unitDef->captureSpeed;
	terraformSpeed = INV_GAME_SPEED * unitDef->terraformSpeed;

	CBaseBuilderBehaviour::PreInit(params);
}


bool CBuilderBehaviour::CanAssistUnit(const CUnit* u, const UnitDef* def) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& unitDef = owner->unitDef;
	if (!unitDef->canAssist)
		return false;

	return ((def == nullptr || u->unitDef == def) && u->beingBuilt && (u->buildProgress < 1.0f) && (u->soloBuilder == nullptr || u->soloBuilder == owner));
}


bool CBuilderBehaviour::CanRepairUnit(const CUnit* u) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& unitDef = owner->unitDef;
	if (!unitDef->canRepair)
		return false;
	if (u->beingBuilt)
		return false;
	if (u->health >= u->maxHealth)
		return false;

	return (u->unitDef->repairable);
}



bool CBuilderBehaviour::UpdateTerraform(const Command&)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto inBuildStance = owner->inBuildStance;
	CUnit* curBuildee = curBuild;

	if (!terraforming || !inBuildStance)
		return false;

	const float* heightmap = readMap->GetCornerHeightMapSynced();
	float terraformScale = 0.1f;

	assert(!mapDamage->Disabled());

	const auto SmoothBorders = [this, heightmap, &terraformScale]() {
		// smooth the x-borders
		for (int z = tz1; z <= tz2; z++) {
			for (int x = 1; x <= TERRA_SMOOTHING_RADIUS; x++) {
				if (tx1 - TERRA_SMOOTHING_RADIUS >= 0) {
					const float ch3 = heightmap[z * mapDims.mapxp1 + tx1];
					const float ch = heightmap[z * mapDims.mapxp1 + tx1 - x];
					const float ch2 = heightmap[z * mapDims.mapxp1 + tx1 - TERRA_SMOOTHING_RADIUS];
					const float amount = ((ch3 * (TERRA_SMOOTHING_RADIUS - x) + ch2 * x) / TERRA_SMOOTHING_RADIUS - ch) * terraformScale;

					readMap->AddHeight(z * mapDims.mapxp1 + tx1 - x, amount);
				}
				if (tx2 + TERRA_SMOOTHING_RADIUS < mapDims.mapx) {
					const float ch3 = heightmap[z * mapDims.mapxp1 + tx2];
					const float ch = heightmap[z * mapDims.mapxp1 + tx2 + x];
					const float ch2 = heightmap[z * mapDims.mapxp1 + tx2 + TERRA_SMOOTHING_RADIUS];
					const float amount = ((ch3 * (TERRA_SMOOTHING_RADIUS - x) + ch2 * x) / TERRA_SMOOTHING_RADIUS - ch) * terraformScale;

					readMap->AddHeight(z * mapDims.mapxp1 + tx2 + x, amount);
				}
			}
		}

		// smooth the z-borders
		for (int z = 1; z <= TERRA_SMOOTHING_RADIUS; z++) {
			for (int x = tx1; x <= tx2; x++) {
				if ((tz1 - TERRA_SMOOTHING_RADIUS) >= 0) {
					const float ch3 = heightmap[(tz1)*mapDims.mapxp1 + x];
					const float ch = heightmap[(tz1 - z) * mapDims.mapxp1 + x];
					const float ch2 = heightmap[(tz1 - TERRA_SMOOTHING_RADIUS) * mapDims.mapxp1 + x];
					const float adjust = ((ch3 * (TERRA_SMOOTHING_RADIUS - z) + ch2 * z) / TERRA_SMOOTHING_RADIUS - ch) * terraformScale;

					readMap->AddHeight((tz1 - z) * mapDims.mapxp1 + x, adjust);
				}
				if ((tz2 + TERRA_SMOOTHING_RADIUS) < mapDims.mapy) {
					const float ch3 = heightmap[(tz2)*mapDims.mapxp1 + x];
					const float ch = heightmap[(tz2 + z) * mapDims.mapxp1 + x];
					const float ch2 = heightmap[(tz2 + TERRA_SMOOTHING_RADIUS) * mapDims.mapxp1 + x];
					const float adjust = ((ch3 * (TERRA_SMOOTHING_RADIUS - z) + ch2 * z) / TERRA_SMOOTHING_RADIUS - ch) * terraformScale;

					readMap->AddHeight((tz2 + z) * mapDims.mapxp1 + x, adjust);
				}
			}
		}
	};

	switch (terraformType) {
	case Terraform_Building: {
		if (curBuildee != nullptr) {
			if (curBuildee->terraformLeft <= 0.0f)
				terraformScale = 0.0f;
			else
				terraformScale = (terraformSpeed + terraformHelp) / curBuildee->terraformLeft;

			curBuildee->terraformLeft -= (terraformSpeed + terraformHelp);

			terraformHelp = 0.0f;
			terraformScale = std::min(terraformScale, 1.0f);

			// prevent building from timing out while terraforming for it
			curBuildee->AddBuildPower(owner, 0.0f);

			for (int z = tz1; z <= tz2; z++) {
				for (int x = tx1; x <= tx2; x++) {
					const int idx = z * mapDims.mapxp1 + x;

					readMap->AddHeight(idx, (curBuildee->pos.y - heightmap[idx]) * terraformScale);
				}
			}
			SmoothBorders();

			if (curBuildee->terraformLeft <= 0.0f) {
				terraforming = false;
				curBuildee->groundLevelled = true;

				if (eventHandler.TerraformComplete(owner, curBuildee)) {
					StopBuild();
				}
			}
		}
	} break;
	case Terraform_Restore: {
		if (myTerraformLeft <= 0.0f)
			terraformScale = 0.0f;
		else
			terraformScale = (terraformSpeed + terraformHelp) / myTerraformLeft;

		myTerraformLeft -= (terraformSpeed + terraformHelp);

		terraformHelp = 0.0f;
		terraformScale = std::min(terraformScale, 1.0f);

		for (int z = tz1; z <= tz2; z++) {
			for (int x = tx1; x <= tx2; x++) {
				int idx = z * mapDims.mapxp1 + x;
				float ch = heightmap[idx];
				float oh = readMap->GetOriginalHeightMapSynced()[idx];

				readMap->AddHeight(idx, (oh - ch) * terraformScale);
			}
		}
		SmoothBorders();

		if (myTerraformLeft <= 0.0f) {
			terraforming = false;
			StopBuild();
		}
	} break;
	}

	owner->ScriptDecloak(curBuildee, nullptr);
	CreateNanoParticle(terraformCenter, terraformRadius * 0.5f, false);



	return true;
}

bool CBuilderBehaviour::AssistTerraform(const Command&)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto inBuildStance = owner->inBuildStance;
	CBuilderBehaviour* helpTerraformee = helpTerraform;

	if (helpTerraformee == nullptr || !inBuildStance)
		return false;

	if (!helpTerraformee->terraforming) {
		// delete our helpTerraform dependence
		StopBuild(true);
		return true;
	}

	owner->ScriptDecloak(helpTerraformee->owner, nullptr);

	helpTerraformee->terraformHelp += terraformSpeed;
	CreateNanoParticle(helpTerraformee->terraformCenter, helpTerraformee->terraformRadius * 0.5f, false);
	return true;
}

bool CBuilderBehaviour::UpdateBuild(const Command& fCommand)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto inBuildStance = owner->inBuildStance;
	const auto& unitDef = owner->unitDef;
	CUnit* curBuildee = curBuild;
	//CBuilderCAI* cai = static_cast<CBuilderCAI*>(owner->commandAI);
	CBuilderBehaviourAI* cai = owner->commandAI->GetBehaviourAI<CBuilderBehaviourAI>();

	if (curBuildee == nullptr || !cai->IsInBuildRange(curBuildee))
		return false;

	if (fCommand.GetID() == CMD_WAIT) {
		if (curBuildee->buildProgress < 1.0f) {
			// prevent buildee from decaying (we cannot call StopBuild here)
			curBuildee->AddBuildPower(owner, 0.0f);
		} else {
			// stop repairing (FIXME: should be much cleaner to let BuilderCAI
			// call this instead when a wait command is given?)
			StopBuild();
		}

		return true;
	}

	if (curBuildee->soloBuilder != nullptr && (curBuildee->soloBuilder != owner)) {
		StopBuild();
		return true;
	}

	// NOTE:
	//   technically this block of code should be guarded by
	//   "if (inBuildStance)", but doing so can create zombie
	//   guarders because scripts might not set inBuildStance
	//   to true when guard or repair orders are executed and
	//   SetRepairTarget does not check for it
	//
	//   StartBuild *does* ensure construction will not start
	//   until inBuildStance is set to true by the builder's
	//   script, and there are no cases during construction
	//   when inBuildStance can become false yet the buildee
	//   should be kept from decaying, so this is free from
	//   serious side-effects (when repairing, a builder might
	//   start adding build-power before having fully finished
	//   its opening animation)
	if (!(inBuildStance || true))
		return true;

	owner->ScriptDecloak(curBuildee, nullptr);

	// adjusted build-speed: use repair-speed on units with
	// progress >= 1 rather than raw build-speed on buildees
	// with progress < 1
	float adjBuildSpeed = buildSpeed;

	if (curBuildee->buildProgress >= 1.0f)
		adjBuildSpeed = std::min(repairSpeed, unitDef->maxRepairSpeed * 0.5f - curBuildee->repairAmount); // repair

	if (adjBuildSpeed > 0.0f && curBuildee->AddBuildPower(owner, adjBuildSpeed)) {
		CreateNanoParticle(curBuildee->midPos, curBuildee->radius * 0.5f, false);
		return true;
	}

	// check if buildee finished construction
	if (curBuildee->beingBuilt || curBuildee->health < curBuildee->maxHealth)
		return true;

	StopBuild();
	return true;
}

bool CBuilderBehaviour::UpdateReclaim(const Command& fCommand)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& pos = owner->pos;
	const auto team = owner->team;
	const auto inBuildStance = owner->inBuildStance;
	// AddBuildPower can invoke StopBuild indirectly even if returns true
	// and reset curReclaim to null (which would crash CreateNanoParticle)
	CSolidObject* curReclaimee = curReclaim;

	if (curReclaimee == nullptr || f3SqDist(curReclaimee->pos, pos) >= Square(buildDistance + curReclaimee->buildeeRadius) || !inBuildStance)
		return false;

	if (fCommand.GetID() == CMD_WAIT) {
		StopBuild();
		return true;
	}

	owner->ScriptDecloak(curReclaimee, nullptr);

	if (!curReclaimee->AddBuildPower(owner, -reclaimSpeed))
		return true;

	CreateNanoParticle(curReclaimee->midPos, curReclaimee->radius * 0.7f, true, (reclaimingUnit && curReclaimee->team != team));
	return true;
}

bool CBuilderBehaviour::UpdateResurrect(const Command& fCommand)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& pos = owner->pos;
	const auto team = owner->team;
	const auto allyteam = owner->allyteam;
	const auto inBuildStance = owner->inBuildStance;
	//CBuilderCAI* cai = static_cast<CBuilderCAI*>(commandAI);
	CFeature* curResurrectee = curResurrect;

	if (curResurrectee == nullptr || f3SqDist(curResurrectee->pos, pos) >= Square(buildDistance + curResurrectee->buildeeRadius) || !inBuildStance)
		return false;

	if (fCommand.GetID() == CMD_WAIT) {
		StopBuild();
		return true;
	}

	if (curResurrectee->udef == nullptr) {
		StopBuild(true);
		return true;
	}

	if ((modInfo.reclaimMethod != 1) && (curResurrectee->reclaimLeft < 1)) {
		// this corpse has been reclaimed a little, need to restore
		// its resources before we can let the player resurrect it
		curResurrectee->AddBuildPower(owner, resurrectSpeed);
		return true;
	}

	const UnitDef* resurrecteeDef = curResurrectee->udef;

	// corpse has been restored, begin resurrection
	const float step = resurrectSpeed / resurrecteeDef->buildTime;

	const bool resurrectAllowed = eventHandler.AllowFeatureBuildStep(owner, curResurrectee, step);
	const bool canExecResurrect = (resurrectAllowed && owner->UseEnergy(resurrecteeDef->cost.energy * step * modInfo.resurrectEnergyCostFactor));

	if (canExecResurrect) {
		curResurrectee->resurrectProgress += step;
		curResurrectee->resurrectProgress = std::min(curResurrectee->resurrectProgress, 1.0f);

		CreateNanoParticle(curResurrectee->midPos, curResurrectee->radius * 0.7f, gsRNG.NextInt(2));
	}

	if (curResurrectee->resurrectProgress < 1.0f)
		return true;

	if (!curResurrectee->deleteMe) {
		// resurrect finished and we are the first
		curResurrectee->UnBlock();

		UnitLoadParams resurrecteeParams = {resurrecteeDef, owner, curResurrectee->pos, ZeroVector, -1, team, curResurrectee->buildFacing, false, false};
		CUnit* resurrectee = unitLoader->LoadUnit(resurrecteeParams);

		assert(resurrecteeDef == resurrectee->unitDef);
		resurrectee->SetSoloBuilder(owner, resurrecteeDef);
		resurrectee->SetHeading(curResurrectee->heading, !resurrectee->upright && resurrectee->IsOnGround(), false, 0.0f);

		for (const int resurrecterID: CBuilderCaches::resurrecters) {
		 	CUnit* resurrecter = unitHandler.GetUnit(resurrecterID);
			//CBuilder* resurrecter = static_cast<CBuilder*>(unitHandler.GetUnit(resurrecterID));
			CCommandAI* resurrecterCAI = resurrecter->commandAI;

			if (resurrecterCAI->commandQue.empty())
				continue;

			Command& c = resurrecterCAI->commandQue.front();

			if (c.GetID() != CMD_RESURRECT || c.GetNumParams() != 1)
				continue;

			if ((c.GetParam(0) - unitHandler.MaxUnits()) != curResurrectee->id)
				continue;

			if (!teamHandler.Ally(allyteam, resurrecter->allyteam))
				continue;

			// all units that were rezzing shall assist the repair too
			CBuilderBehaviour* resurrecterAI = resurrecter->GetBehaviour<CBuilderBehaviour>();
			resurrecterAI->lastResurrected = resurrectee->id;

			// prevent FinishCommand from removing this command when the
			// feature is deleted, since it is needed to start the repair
			// (WTF!)
			c.SetParam(0, INT_MAX / 2);
		}

		// this takes one simframe to do the deletion
		featureHandler.DeleteFeature(curResurrectee);
	}

	StopBuild(true);
	return true;
}

bool CBuilderBehaviour::UpdateCapture(const Command& fCommand)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto inBuildStance = owner->inBuildStance;
	const auto& pos = owner->pos;
	const auto& unitDef = owner->unitDef;
	const auto team = owner->team;
	CUnit* curCapturee = curCapture;

	if (curCapturee == nullptr || f3SqDist(curCapturee->pos, pos) >= Square(buildDistance + curCapturee->buildeeRadius) || !inBuildStance)
		return false;

	if (fCommand.GetID() == CMD_WAIT) {
		StopBuild();
		return true;
	}

	if (curCapturee->team == team) {
		StopBuild(true);
		return true;
	}

	const float captureMagicNumber = (150.0f + (curCapturee->buildTime / captureSpeed) * (curCapturee->health + curCapturee->maxHealth) / curCapturee->maxHealth * 0.4f);
	const float captureProgressStep = 1.0f / captureMagicNumber;
	const float captureProgressTemp = std::min(curCapturee->captureProgress + captureProgressStep, 1.0f);

	const float captureFraction = captureProgressTemp - curCapturee->captureProgress;
	const float energyUseScaled = curCapturee->cost.energy * captureFraction * modInfo.captureEnergyCostFactor;

	const bool buildStepAllowed = (eventHandler.AllowUnitBuildStep(owner, curCapturee, captureProgressStep));
	const bool captureStepAllowed = (eventHandler.AllowUnitCaptureStep(owner, curCapturee, captureProgressStep));
	const bool canExecCapture = (buildStepAllowed && captureStepAllowed && owner->UseEnergy(energyUseScaled));

	if (!canExecCapture)
		return true;

	curCapturee->captureProgress += captureProgressStep;
	curCapturee->captureProgress = std::min(curCapturee->captureProgress, 1.0f);

	CreateNanoParticle(curCapturee->midPos, curCapturee->radius * 0.7f, false, true);

	if (curCapturee->captureProgress < 1.0f)
		return true;

	if (!curCapturee->ChangeTeam(team, CUnit::ChangeCaptured)) {
		// capture failed
		if (team == gu->myTeam) {
			LOG_L(L_WARNING, "%s: Capture failed, unit type limit reached", unitDef->humanName.c_str());
			eventHandler.LastMessagePosition(pos);
		}
	}

	curCapturee->captureProgress = 0.0f;
	StopBuild(true);
	return true;
}



void CBuilderBehaviour::UpdatePre()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto beingBuilt = owner->beingBuilt;
	const CCommandAI* cai = owner->commandAI;
	//const CBuilderCAI* cai = static_cast<CBuilderCAI*>(commandAI);

	const CCommandQueue& cQueue = cai->commandQue;
	const Command& fCommand = (!cQueue.empty())? cQueue.front(): Command(CMD_STOP);

	bool updated = false;

	CBaseBuilderBehaviour::UpdatePre(); //nanoPieceCache.Update();

	if (!beingBuilt && !owner->IsStunned()) {
		updated = updated || UpdateTerraform(fCommand);
		updated = updated || AssistTerraform(fCommand);
		updated = updated || UpdateBuild(fCommand);
		updated = updated || UpdateReclaim(fCommand);
		updated = updated || UpdateResurrect(fCommand);
		updated = updated || UpdateCapture(fCommand);
	}
}


void CBuilderBehaviour::SlowUpdate()
{
  	RECOIL_DETAILED_TRACY_ZONE;
	if (terraforming) {
		constexpr int tsr = TERRA_SMOOTHING_RADIUS;
		mapDamage->RecalcArea(tx1 - tsr, tx2 + tsr, tz1 - tsr, tz2 + tsr);
	}

	CBaseBuilderBehaviour::SlowUpdate();
}


void CBuilderBehaviour::SetRepairTarget(CUnit* target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (target == curBuild)
		return;

	StopBuild(false);
	owner->TempHoldFire(CMD_REPAIR);

	curBuild = target;
	AddDeathDependence(curBuild, DEPENDENCE_BUILD);

	if (!target->groundLevelled) {
		// resume levelling the ground
		tx1 = (int)std::max(0.0f, (target->pos.x - (target->xsize * 0.5f * SQUARE_SIZE)) / SQUARE_SIZE);
		tz1 = (int)std::max(0.0f, (target->pos.z - (target->zsize * 0.5f * SQUARE_SIZE)) / SQUARE_SIZE);
		tx2 = std::min(mapDims.mapx, tx1 + target->xsize);
		tz2 = std::min(mapDims.mapy, tz1 + target->zsize);

		terraformCenter = target->pos;
		terraformRadius = (tx1 - tx2) * SQUARE_SIZE;
		terraformType = Terraform_Building;
		terraforming = true;
	}

	ScriptStartBuilding(target->pos, false);
}


void CBuilderBehaviour::SetReclaimTarget(CSolidObject* target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (dynamic_cast<CFeature*>(target) != nullptr && !static_cast<CFeature*>(target)->def->reclaimable)
		return;

	CUnit* recUnit = dynamic_cast<CUnit*>(target);

	if (recUnit != nullptr && !recUnit->unitDef->reclaimable)
		return;

	if (curReclaim == target || owner == target)
		return;

	StopBuild(false);
	owner->TempHoldFire(CMD_RECLAIM);

	reclaimingUnit = (recUnit != nullptr);
	curReclaim = target;

	AddDeathDependence(curReclaim, DEPENDENCE_RECLAIM);
	ScriptStartBuilding(target->pos, false);
}


void CBuilderBehaviour::SetResurrectTarget(CFeature* target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (curResurrect == target || target->udef == nullptr)
		return;

	StopBuild(false);
	owner->TempHoldFire(CMD_RESURRECT);

	curResurrect = target;

	AddDeathDependence(curResurrect, DEPENDENCE_RESURRECT);
	ScriptStartBuilding(target->pos, false);
}


void CBuilderBehaviour::SetCaptureTarget(CUnit* target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (target == curCapture)
		return;

	StopBuild(false);
	owner->TempHoldFire(CMD_CAPTURE);

	curCapture = target;

	AddDeathDependence(curCapture, DEPENDENCE_CAPTURE);
	ScriptStartBuilding(target->pos, false);
}


void CBuilderBehaviour::StartRestore(float3 centerPos, float radius)
{
	RECOIL_DETAILED_TRACY_ZONE;
	StopBuild(false);
	owner->TempHoldFire(CMD_RESTORE);

	terraforming = true;
	terraformType = Terraform_Restore;
	terraformCenter = centerPos;
	terraformRadius = radius;

	tx1 = (int)max((float)0,(centerPos.x-radius)/SQUARE_SIZE);
	tx2 = (int)min((float)mapDims.mapx,(centerPos.x+radius)/SQUARE_SIZE);
	tz1 = (int)max((float)0,(centerPos.z-radius)/SQUARE_SIZE);
	tz2 = (int)min((float)mapDims.mapy,(centerPos.z+radius)/SQUARE_SIZE);

	float tcost = 0.0f;
	const float* curHeightMap = readMap->GetCornerHeightMapSynced();
	const float* orgHeightMap = readMap->GetOriginalHeightMapSynced();

	for (int z = tz1; z <= tz2; z++) {
		for (int x = tx1; x <= tx2; x++) {
			float delta = orgHeightMap[z * mapDims.mapxp1 + x] - curHeightMap[z * mapDims.mapxp1 + x];
			tcost += math::fabs(delta);
		}
	}
	myTerraformLeft = tcost;

	ScriptStartBuilding(centerPos, false);
}


void CBuilderBehaviour::StopBuild(bool callScript)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& script = owner->script;
	if (curBuild != nullptr)
		DeleteDeathDependence(curBuild, DEPENDENCE_BUILD);
	if (curReclaim != nullptr)
		DeleteDeathDependence(curReclaim, DEPENDENCE_RECLAIM);
	if (helpTerraform != nullptr)
		DeleteDeathDependence(helpTerraform, DEPENDENCE_TERRAFORM);
	if (curResurrect != nullptr)
		DeleteDeathDependence(curResurrect, DEPENDENCE_RESURRECT);
	if (curCapture != nullptr)
		DeleteDeathDependence(curCapture, DEPENDENCE_CAPTURE);

	curBuild = nullptr;
	curReclaim = nullptr;
	helpTerraform = nullptr;
	curResurrect = nullptr;
	curCapture = nullptr;

	if (terraforming) {
		constexpr int tsr = TERRA_SMOOTHING_RADIUS;
		mapDamage->RecalcArea(tx1 - tsr, tx2 + tsr, tz1 - tsr, tz2 + tsr);
	}

	terraforming = false;

	if (callScript)
		script->StopBuilding();

	owner->SetHoldFire(false);
}


bool CBuilderBehaviour::StartBuild(BuildInfo& buildInfo, CFeature*& feature, bool& inWaitStance, bool& limitReached)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto allyteam = owner->allyteam;
	const auto team = owner->team;
	const CUnit* prvBuild = curBuild;

	StopBuild(false);
	owner->TempHoldFire(-1);

	buildInfo.pos = CGameHelper::Pos2BuildPos(buildInfo, true);

	auto isBuildeeFloating = [](const BuildInfo& buildInfo) {
		if (buildInfo.def->RequireMoveDef()) {
			MoveDef* md = moveDefHandler.GetMoveDefByPathType(buildInfo.def->pathType);
			return (md->FloatOnWater());
		} else {
			return (buildInfo.def->floatOnWater);
		}
	};

	// Units that cannot be underwater need their build checks kept above water or else collision detections will
	// produce the wrong results.
	if (isBuildeeFloating(buildInfo))
		buildInfo.pos.y = (buildInfo.pos.y < 0.f) ? 0.f : buildInfo.pos.y;

	// Pass -1 as allyteam to behave like we have maphack.
	// This is needed to prevent building on top of cloaked stuff.
	const CGameHelper::BuildSquareStatus tbs = CGameHelper::TestUnitBuildSquare(buildInfo, feature, -1, true);

	switch (tbs) {
		case CGameHelper::BUILDSQUARE_OPEN:
			break;

		case CGameHelper::BUILDSQUARE_BLOCKED:
		case CGameHelper::BUILDSQUARE_OCCUPIED: {
			const CUnit* u = nullptr;

			const int2 mins = CSolidObject::GetMapPosStatic(buildInfo.pos, buildInfo.GetXSize(), buildInfo.GetZSize());
			const int2 maxs = mins + int2(buildInfo.GetXSize(), buildInfo.GetZSize());

			for (int z = mins.y; z < maxs.y; ++z) {
				for (int x = mins.x; x < maxs.x; ++x) {
					const CGroundBlockingObjectMap::BlockingMapCell& cell = groundBlockingObjectMap.GetCellUnsafeConst(float3{
						static_cast<float>(x * SQUARE_SIZE),
						0.0f,
						static_cast<float>(z * SQUARE_SIZE) }
					);

					// look for any blocking assistable buildee at build.pos
					for (size_t i = 0, n = cell.size(); i < n; i++) {
						const CUnit* cu = dynamic_cast<const CUnit*>(cell[i]);

						if (cu == nullptr)
							continue;
						if (allyteam != cu->allyteam)
							return false; // Enemy units that block always block the cell
						if (!CanAssistUnit(cu, buildInfo.def))
							continue;

						u = cu;
						goto out; //lol
					}
				}
			}

			out:
			// <pos> might map to a non-blocking portion
			// of the buildee's yardmap, fallback check
			if (u == nullptr)
				u = CGameHelper::GetClosestFriendlyUnit(nullptr, buildInfo.pos, buildDistance, allyteam);

			if (u != nullptr) {
				if (CanAssistUnit(u, buildInfo.def)) {
					// StopBuild sets this to false, fix it here if picking up the same buildee again
					terraforming = (u == prvBuild && u->terraformLeft > 0.0f);

					AddDeathDependence(curBuild = const_cast<CUnit*>(u), DEPENDENCE_BUILD);
					ScriptStartBuilding(u->pos, false);
					return true;
				}

				// let BuggerOff handle this case (TODO: non-landed aircraft should not count)
				if (buildInfo.FootPrintOverlap(u->pos, u->GetFootPrint(SQUARE_SIZE * 0.5f)))
					return false;
			}
		} return false;

		case CGameHelper::BUILDSQUARE_RECLAIMABLE:
			// caller should handle this
			return false;
	}

	// at this point we know the builder is going to create a new unit, bail if at the limit
	if ((limitReached = (unitHandler.NumUnitsByTeamAndDef(team, buildInfo.def->id) >= buildInfo.def->maxThisUnit)))
		return false;

	if ((inWaitStance = !ScriptStartBuilding(buildInfo.pos, true)))
		return false;

	const UnitDef* buildeeDef = buildInfo.def;
	const UnitLoadParams buildeeParams = {buildeeDef, owner, buildInfo.pos, ZeroVector, -1, team, buildInfo.buildFacing, true, false};

	CUnit* buildee = unitLoader->LoadUnit(buildeeParams);

	// floating structures don't terraform the seabed
	const bool buildeeOnWater = (buildee->FloatOnWater() && buildee->IsInWater());
	const bool allowTerraform = (!mapDamage->Disabled() && buildeeDef->levelGround);
	const bool  skipTerraform = (buildeeOnWater || buildeeDef->IsAirUnit() || !buildeeDef->IsImmobileUnit());

	if (!allowTerraform || skipTerraform) {
		// skip the terraforming job
		buildee->terraformLeft = 0.0f;
		buildee->groundLevelled = true;
	} else {
		tx1 = (int)std::max(0.0f, (buildee->pos.x - (buildee->xsize * 0.5f * SQUARE_SIZE)) / SQUARE_SIZE);
		tz1 = (int)std::max(0.0f, (buildee->pos.z - (buildee->zsize * 0.5f * SQUARE_SIZE)) / SQUARE_SIZE);
		tx2 = std::min(mapDims.mapx, tx1 + buildee->xsize);
		tz2 = std::min(mapDims.mapy, tz1 + buildee->zsize);

		buildee->terraformLeft = CalculateBuildTerraformCost(buildInfo);
		buildee->groundLevelled = false;

		terraforming    = true;
		terraformType   = Terraform_Building;
		terraformRadius = (tx2 - tx1) * SQUARE_SIZE;
		terraformCenter = buildee->pos;
	}

	// pass the *builder*'s udef for checking canBeAssisted; if buildee
	// happens to be a non-assistable factory then it would also become
	// impossible to *construct* with multiple builders
	buildee->SetSoloBuilder(owner, owner->unitDef);
	AddDeathDependence(curBuild = buildee, DEPENDENCE_BUILD);

	// if the ground is not going to be terraformed the buildee would
	// 'pop' to the correct height over the (un-flattened) terrain on
	// completion, so put it there to begin with
	curBuild->moveType->SlowUpdate();
	return true;
}


float CBuilderBehaviour::CalculateBuildTerraformCost(BuildInfo& buildInfo)
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3& buildPos = buildInfo.pos;

	float tcost = 0.0f;
	const float* curHeightMap = readMap->GetCornerHeightMapSynced();
	const float* orgHeightMap = readMap->GetOriginalHeightMapSynced();

	for (int z = tz1; z <= tz2; z++) {
		for (int x = tx1; x <= tx2; x++) {
			const int idx = z * mapDims.mapxp1 + x;
			float delta = buildPos.y - curHeightMap[idx];
			float cost;
			if (delta > 0) {
				cost = max(3.0f, curHeightMap[idx] - orgHeightMap[idx] + delta * 0.5f);
			} else {
				cost = max(3.0f, orgHeightMap[idx] - curHeightMap[idx] - delta * 0.5f);
			}
			tcost += math::fabs(delta) * cost;
		}
	}

	return tcost;
}


void CBuilderBehaviour::DependentDied(CObject* o)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (o == curBuild) {
		curBuild = nullptr;
		StopBuild();
	}
	if (o == curReclaim) {
		curReclaim = nullptr;
		StopBuild();
	}
	if (o == helpTerraform) {
		helpTerraform = nullptr;
		StopBuild();
	}
	if (o == curResurrect) {
		curResurrect = nullptr;
		StopBuild();
	}
	if (o == curCapture) {
		curCapture = nullptr;
		StopBuild();
	}
	CBaseBuilderBehaviour::DependentDied(o);
}


bool CBuilderBehaviour::ScriptStartBuilding(float3 pos, bool silent)
{
	auto& script = owner->script;
	const auto inBuildStance = owner->inBuildStance;
	const auto& unitDef = owner->unitDef;
	if (script->HasStartBuilding()) {
		const auto& midPos = owner->midPos;
		const auto& frontdir = owner->frontdir;
		const auto& updir = owner->updir;
		const auto& heading = owner->heading;

		const float3 wantedDir = (pos - midPos).Normalize();
		const float h = GetHeadingFromVectorF(wantedDir.x, wantedDir.z);
		const float p = math::asin(wantedDir.dot(updir));
		const float pitch = math::asin(frontdir.dot(updir));

		// clamping p - pitch not needed, range of asin is -PI/2..PI/2,
		// so max difference between two asin calls is PI.
		// FIXME: convert CSolidObject::heading to radians too.
		script->StartBuilding(ClampRad(h - heading * TAANG2RAD), p - pitch);
	}

	if ((!silent || inBuildStance) && owner->IsInLosForAllyTeam(gu->myAllyTeam))
		Channels::General->PlayRandomSample(unitDef->sounds.build, pos);

	return inBuildStance;
}

/*
bool CBuilderBehaviour::ScriptStartBuilding(float3 pos, bool silent)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (script->HasStartBuilding()) {
		const float3 wantedDir = (pos - midPos).Normalize();
		const float h = GetHeadingFromVectorF(wantedDir.x, wantedDir.z);
		const float p = math::asin(wantedDir.dot(updir));
		const float pitch = math::asin(frontdir.dot(updir));

		// clamping p - pitch not needed, range of asin is -PI/2..PI/2,
		// so max difference between two asin calls is PI.
		// FIXME: convert CSolidObject::heading to radians too.
		script->StartBuilding(ClampRad(h - heading * TAANG2RAD), p - pitch);
	}

	if ((!silent || inBuildStance) && IsInLosForAllyTeam(gu->myAllyTeam))
		Channels::General->PlayRandomSample(unitDef->sounds.build, pos);

	return inBuildStance;
}*/


void CBuilderBehaviour::HelpTerraform(CBuilderBehaviour* unit)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (helpTerraform == unit)
		return;

	StopBuild(false);

	helpTerraform = unit;

	AddDeathDependence(helpTerraform, DEPENDENCE_TERRAFORM);
	ScriptStartBuilding(unit->terraformCenter, false);
}


