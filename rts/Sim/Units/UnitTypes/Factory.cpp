/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "Factory.h"
#include "Game/GameHelper.h"
#include "Game/WaitCommandsAI.h"
#include "Map/Ground.h"
#include "Map/ReadMap.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "Sim/Misc/ModInfo.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/MoveTypes/MoveDefHandler.h"
#include "Sim/MoveTypes/MoveMath/MoveMath.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Units/Scripts/UnitScript.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/FactoryCAI.h"
#include "Sim/Units/CommandAI/MobileCAI.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitLoader.h"
#include "System/EventHandler.h"
#include "System/Matrix44f.h"
#include "System/SpringMath.h"
#include "System/creg/DefTypes.h"
#include "System/Sound/ISoundChannels.h"
#include "Sim/Units/Scripts/CobInstance.h"

#include "Game/GlobalUnsynced.h"

#include "System/Misc/TracyDefs.h"

CR_BIND_DERIVED(CFactory, CBuilding, )
CR_REG_METADATA(CFactory, (
	CR_MEMBER(buildSpeed),
	CR_MEMBER(buildDistance),
	CR_MEMBER(reclaimSpeed),
	CR_MEMBER(range3D),

	CR_MEMBER(boOffset),
	CR_MEMBER(boRadius),
	CR_MEMBER(boRelHeading),
	CR_MEMBER(boSherical),
	CR_MEMBER(boForced),
	CR_MEMBER(boPerform),

	CR_MEMBER(lastBuildUpdateFrame),
	CR_MEMBER(curBuildDef),
	CR_MEMBER(curBuild),
	CR_MEMBER(finishedBuildCommand),
	CR_MEMBER(nanoPieceCache)
))

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CFactory::CFactory()
	: CBuilding()
	, buildSpeed(100.0f)
	, reclaimSpeed(100)
	, range3D(true)
	, buildDistance(16)
	, boOffset(0.0f) //can't set here
	, boRadius(0.0f) //can't set here
	, boRelHeading(0)
	, boSherical(true)
	, boForced(true)
	, boPerform(true)
	, curBuild(nullptr)
	, curBuildDef(nullptr)
	, lastBuildUpdateFrame(-1)
{ }

void CFactory::KillUnit(CUnit* attacker, bool selfDestruct, bool reclaimed, int weaponDefID)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (curBuild != nullptr) {
		curBuild->KillUnit(nullptr, false, true, -CSolidObject::DAMAGE_FACTORY_KILLED);
		curBuild = nullptr;
	}

	CUnit::KillUnit(attacker, selfDestruct, reclaimed, weaponDefID);
}

void CFactory::PreInit(const UnitLoadParams& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	unitDef = params.unitDef;
	range3D = unitDef->buildRange3D;
	buildSpeed = unitDef->buildSpeed / GAME_SPEED;
	buildDistance = (params.unitDef)->buildDistance;
	reclaimSpeed   = INV_GAME_SPEED * unitDef->reclaimSpeed;

	CBuilding::PreInit(params);

	//radius is defined after CUnit::PreInit()
	boOffset = radius * 0.5f;
	boRadius = radius * 0.5f;
}



float3 CFactory::CalcBuildPos(int buildPiece)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float3 relBuildPos = script->GetPiecePos((buildPiece < 0)? script->QueryBuildInfo() : buildPiece);
	const float3 absBuildPos = this->GetObjectSpacePos(relBuildPos);
	return absBuildPos;
}



void CFactory::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	nanoPieceCache.Update();

	if (beingBuilt) {
		// factory is under construction, cannot build anything yet
		CUnit::Update();

		// this can happen if we started being reclaimed *while* building a
		// unit, in which case our buildee can either be allowed to finish
		// construction (by assisting builders) or has to be killed --> the
		// latter is easier
		if (curBuild != nullptr)
			StopBuild(true);

		return;
	}


	if (curBuildDef != nullptr) {
		// if there is a unit blocking the factory's exit while
		// open and already in build-stance, StartBuild returns
		// early whereas while *closed* (!open) a blockee causes
		// CanOpenYard to return false so the Activate callin is
		// never called
		// the radius can not be too large or assisting (mobile)
		// builders around the factory will be disturbed by this
		if ((gs->frameNum & (UNIT_SLOWUPDATE_RATE >> 1)) == 0 && boPerform) {
			float3 boDir = (boRelHeading == 0) ? static_cast<float3>(frontdir) : GetVectorFromHeading((heading + boRelHeading) % SPRING_MAX_HEADING);
			CGameHelper::BuggerOff(pos + boDir * boOffset, boRadius, boSherical, boForced, team, this);
		}

		if (!yardOpen && !IsStunned()) {
			if (groundBlockingObjectMap.CanOpenYard(this)) {
				groundBlockingObjectMap.OpenBlockingYard(this); // set yardOpen
				script->Activate(); // set buildStance

				// make sure the idle-check does not immediately trigger
				// (scripts have 7 seconds to set inBuildStance to true)
				lastBuildUpdateFrame = gs->frameNum;
			}
		}

		if (yardOpen && inBuildStance && !IsStunned()) {
			StartBuild(curBuildDef);
		}
	}

	if (curBuild != nullptr) {
		UpdateBuild(curBuild);
		FinishBuild(curBuild);
	} else if (!IsStunned()) {
		const CFactoryCAI* cai = static_cast<CFactoryCAI*>(commandAI);
		const CCommandQueue& cQueue = cai->commandQue;
		const Command& fCommand = (!cQueue.empty())? cQueue.front(): Command(CMD_STOP);

		bool updated = false;

		//updated = updated || UpdateTerraform(fCommand);
		//updated = updated || AssistTerraform(fCommand);
		//updated = updated || UpdateBuild(fCommand);
		updated = updated || UpdateReclaim(fCommand);
		//updated = updated || UpdateResurrect(fCommand);
		//updated = updated || UpdateCapture(fCommand);
	}

	const bool wantClose = (!IsStunned() && yardOpen && (gs->frameNum >= (lastBuildUpdateFrame + GAME_SPEED * (UNIT_SLOWUPDATE_RATE >> 1))));
	const bool closeYard = (wantClose && curBuild == nullptr && groundBlockingObjectMap.CanCloseYard(this));

	if (closeYard) {
		// close the factory after inactivity
		groundBlockingObjectMap.CloseBlockingYard(this);
		script->Deactivate();
	}

	CBuilding::Update();
}

bool CFactory::UpdateReclaim(const Command& fCommand)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// AddBuildPower can invoke StopBuild indirectly even if returns true
	// and reset curReclaim to null (which would crash CreateNanoParticle)
	CSolidObject* curReclaimee = curReclaim;

	if (curReclaimee == nullptr || f3SqDist(curReclaimee->pos, pos) >= Square(buildDistance + curReclaimee->buildeeRadius) || !inBuildStance)
		return false;

	if (fCommand.GetID() == CMD_WAIT) {
		StopBuild();
		return true;
	}

	ScriptDecloak(curReclaimee, nullptr);

	if (!curReclaimee->AddBuildPower(this, -reclaimSpeed))
		return true;

	CreateNanoParticle(curReclaimee->midPos, curReclaimee->radius * 0.7f, true, (reclaimingUnit && curReclaimee->team != team));
	return true;
}

void CFactory::StartBuild(const UnitDef* buildeeDef) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (isDead)
		return;

	const float3& buildPos = CalcBuildPos(script->QueryBuildInfo());

	// wait until buildPos is no longer blocked (eg. by a previous buildee)
	//
	// it might rarely be the case that a unit got stuck inside the factory
	// or died right after completion and left some wreckage, but that is up
	// to players to fix
	if (groundBlockingObjectMap.GroundBlocked(buildPos, this))
		return;

	UnitLoadParams buildeeParams = {buildeeDef, this, buildPos, ZeroVector, -1, team, buildFacing, true, false};
	CUnit* buildee = unitLoader->LoadUnit(buildeeParams);

	if (!unitDef->canBeAssisted) {
		buildee->soloBuilder = this;
		buildee->AddDeathDependence(this, DEPENDENCE_BUILDER);
	}

	AddDeathDependence(buildee, DEPENDENCE_BUILD);
	script->StartBuilding();

	// set curBuildDef to NULL to indicate construction
	// has started, otherwise we would keep being called
	curBuild = buildee;
	curBuildDef = nullptr;

	if (losStatus[gu->myAllyTeam] & LOS_INLOS) {
		Channels::General->PlayRandomSample(unitDef->sounds.build, buildPos);
	}
}

void CFactory::UpdateBuild(CUnit* buildee) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (IsStunned())
		return;

	// factory not under construction and
	// nanolathing unit: continue building
	lastBuildUpdateFrame = gs->frameNum;

	// buildPiece is the rotating platform
	const int buildPiece = script->QueryBuildInfo();

	const float3& buildPos = CalcBuildPos(buildPiece);
	const CMatrix44f& buildPieceMat = script->GetPieceMatrix(buildPiece);

	// see CMatrix44f::CMatrix44f(const float3 pos, const float3 x, const float3 y, const float3 z)
	// frontdir.x, frontdir.z
	const int buildPieceHeading = GetHeadingFromVector(buildPieceMat[8], buildPieceMat[10]);
	const int buildFaceHeading = GetHeadingFromFacing(buildFacing);

	const CCommandQueue& queue = commandAI->commandQue;

	if (!queue.empty() && (queue.front().GetID() < 0)) {
		float3 buildeePos = buildPos;

		// note: basically StaticMoveType::SlowUpdate()
		if (buildee->FloatOnWater() && buildee->IsInWater())
			buildeePos.y = -buildee->moveType->GetWaterline();

		// rotate unit nanoframe with platform
		buildee->Move(buildeePos, false);
		buildee->SetHeading((-buildPieceHeading + buildFaceHeading) & (SPRING_CIRCLE_DIVS - 1), false, false, 0.0f);
	}

	if (!queue.empty() && (queue.front().GetID() == CMD_WAIT)) {
		buildee->AddBuildPower(this, 0.0f);
		return;
	}

	if (!buildee->AddBuildPower(this, buildSpeed))
		return;

	CreateNanoParticle(buildee->midPos, buildee->radius * 0.5f, false);
}

void CFactory::FinishBuild(CUnit* buildee) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (buildee->beingBuilt)
		return;
	if (unitDef->fullHealthFactory && buildee->health < buildee->maxHealth)
		return;

	bool isOurs = false;
	const CCommandQueue& queue = commandAI->commandQue;
	if (!queue.empty() && (queue.front().GetID() < 0)) {
		// assign buildee to same group as us
		if (GetGroup() != nullptr && buildee->GetGroup() != nullptr)
			buildee->SetGroup(GetGroup(), true);
		isOurs = true;

	}

	const CCommandAI* bcai = buildee->commandAI;
	// if not idle, the buildee already has user orders
	const bool buildeeIdle = (bcai->commandQue.empty());
	const bool buildeeMobile = (dynamic_cast<const CMobileCAI*>(bcai) != nullptr);

	if (isOurs && (buildeeIdle || buildeeMobile)) {
		AssignBuildeeOrders(buildee);
		waitCommandsAI.AddLocalUnit(buildee, this);
	}

	if (isOurs) {
		// inform our commandAI
		CFactoryCAI* factoryCAI = static_cast<CFactoryCAI*>(commandAI);
		factoryCAI->FactoryFinishBuild(finishedBuildCommand);

		eventHandler.UnitFromFactory(buildee, this, !buildeeIdle);
	}
	StopBuild(true);
}



unsigned int CFactory::QueueBuild(const UnitDef* buildeeDef, const Command& buildCmd)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(!beingBuilt);
	assert(buildeeDef != nullptr);

	if (curBuild != nullptr)
		return FACTORY_KEEP_BUILD_ORDER;
	if (unitHandler.NumUnitsByTeamAndDef(team, buildeeDef->id) >= buildeeDef->maxThisUnit)
		return FACTORY_SKIP_BUILD_ORDER;
	if (teamHandler.Team(team)->AtUnitLimit())
		return FACTORY_KEEP_BUILD_ORDER;

	const auto [allow, drop] = eventHandler.AllowUnitCreation(buildeeDef, this, nullptr);
	if (!allow)
		return drop ? FACTORY_SKIP_BUILD_ORDER : FACTORY_KEEP_BUILD_ORDER;

	finishedBuildCommand = buildCmd;
	curBuildDef = buildeeDef;

	// signal that the build-order was accepted (queued)
	return FACTORY_NEXT_BUILD_ORDER;
}

void CFactory::StopBuild(bool callScript)
{
	RECOIL_DETAILED_TRACY_ZONE;
	/*if (curBuild != nullptr)
		DeleteDeathDependence(curBuild, DEPENDENCE_BUILD);*/
	if (callScript)
		script->StopBuilding();

	if (curBuild) {
		// cancel a build-in-progress
		if (curBuild->beingBuilt) {
			AddMetal(curBuild->cost.metal * curBuild->buildProgress, false);
			curBuild->KillUnit(nullptr, false, true, -CSolidObject::DAMAGE_FACTORY_CANCEL);
		}
		DeleteDeathDependence(curBuild, DEPENDENCE_BUILD);
	}


	if (curReclaim != nullptr)
		DeleteDeathDependence(curReclaim, DEPENDENCE_RECLAIM);
	/*if (helpTerraform != nullptr)
		DeleteDeathDependence(helpTerraform, DEPENDENCE_TERRAFORM);
	if (curResurrect != nullptr)
		DeleteDeathDependence(curResurrect, DEPENDENCE_RESURRECT);*/
	if (curCapture != nullptr)
		DeleteDeathDependence(curCapture, DEPENDENCE_CAPTURE);

	curBuild = nullptr;
	curReclaim = nullptr;
	/*helpTerraform = nullptr;*/
	curResurrect = nullptr;
	curCapture = nullptr;
	curBuildDef = nullptr;

	/*if (terraforming) {
		constexpr int tsr = TERRA_SMOOTHING_RADIUS;
		mapDamage->RecalcArea(tx1 - tsr, tx2 + tsr, tz1 - tsr, tz2 + tsr);
	}

	terraforming = false;

	SetHoldFire(false);*/
}


void CFactory::DependentDied(CObject* o)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (o == curBuild) {
		curBuild = nullptr;
		StopBuild(true);
	}

	CUnit::DependentDied(o);
}



void CFactory::SendToEmptySpot(CUnit* unit)
{
	RECOIL_DETAILED_TRACY_ZONE;
	constexpr int numSteps = 100;

	const float searchRadius = radius * 4.0f + unit->radius * 4.0f;
	const float searchAngle = math::PI / (numSteps * 0.5f);

	const float3 exitPos = pos + frontdir * (radius + unit->radius);
	const float3 tempPos = pos + frontdir * searchRadius;

	float3 foundPos = tempPos;
	MoveTypes::CheckCollisionQuery colliderInfo(unit);

	for (int i = 0; i < numSteps; ++i) {
		const float a = searchRadius * math::cos(i * searchAngle);
		const float b = searchRadius * math::sin(i * searchAngle);

		float3 testPos = pos + frontdir * a + rightdir * b;

		if (!testPos.IsInBounds())
			continue;
		// don't pick spots behind the factory, because
		// units will want to path through it when open
		// (which slows down production)
		if ((testPos - pos).dot(frontdir) < 0.0f)
			continue;

		testPos.y = CGround::GetHeightAboveWater(testPos.x, testPos.z);

		if (!quadField.NoSolidsExact(testPos, unit->radius * 1.5f, 0xFFFFFFFF, CSolidObject::CSTATE_BIT_SOLIDOBJECTS))
			continue;
		if (unit->moveDef != nullptr) {
			colliderInfo.UpdateElevationForPos(testPos);
			if (!unit->moveDef->TestMoveSquare(colliderInfo, testPos, ZeroVector, true, true))
				continue;
		}

		foundPos = testPos;
		break;
	}

	if (foundPos == tempPos) {
		// no empty spot found, pick one randomly so units do not pile up even more
		// also make sure not to loop forever if we happen to be facing a map border
		foundPos.y = 0.0f;

		for (int i = 0; i < numSteps; ++i) {
			const float x = gsRNG.NextFloat() * numSteps;
			const float a = searchRadius * math::cos(x * searchAngle);
			const float b = searchRadius * math::sin(x * searchAngle);

			foundPos.x = pos.x + frontdir.x * a + rightdir.x * b;
			foundPos.z = pos.z + frontdir.z * a + rightdir.z * b;

			if (!foundPos.IsInBounds())
				continue;
			if ((foundPos - pos).dot(frontdir) < 0.0f)
				continue;

			if (unit->moveDef != nullptr) {
				colliderInfo.UpdateElevationForPos(foundPos);
				if (!unit->moveDef->TestMoveSquare(colliderInfo, foundPos, ZeroVector, true, true))
					continue;
			}

			break;
		}

		foundPos.y = CGround::GetHeightAboveWater(foundPos.x, foundPos.z);
	}

	// first queue a temporary waypoint outside the factory
	// (otherwise units will try to turn before exiting when
	// foundPos lies behind exit and cause jams / get stuck)
	// we assume this temporary point is not itself blocked,
	// unlike the second for which we do call TestMoveSquare
	//
	// NOTE:
	//   MobileCAI::AutoGenerateTarget inserts a _third_
	//   command when |foundPos - tempPos| >= 100 elmos,
	//   because MobileCAI::FinishCommand only updates
	//   lastUserGoal for non-internal orders --> the
	//   final order given here should not be internal
	//   (and should also be more than CMD_CANCEL_DIST
	//   elmos distant from foundPos)
	//
	if (!unit->unitDef->canfly && exitPos.IsInBounds())
		unit->commandAI->GiveCommand(Command(CMD_MOVE, SHIFT_KEY, exitPos));

	// second actual empty-spot waypoint
	unit->commandAI->GiveCommand(Command(CMD_MOVE, SHIFT_KEY, foundPos));
}

void CFactory::AssignBuildeeOrders(CUnit* unit) {
	RECOIL_DETAILED_TRACY_ZONE;
	CCommandAI* unitCAI = unit->commandAI;
	CCommandQueue& unitCmdQue = unitCAI->commandQue;

	const CFactoryCAI* factoryCAI = static_cast<CFactoryCAI*>(commandAI);
	const CCommandQueue& factoryCmdQue = factoryCAI->newUnitCommands;

	if (factoryCmdQue.empty() && unitCmdQue.empty()) {
		SendToEmptySpot(unit);
		return;
	}

	Command c(CMD_MOVE);

	if (!unit->unitDef->canfly && modInfo.insertBuiltUnitMoveCommand) {
		// HACK: when a factory has a rallypoint set far enough away
		// to trigger the non-admissable path estimators, we want to
		// avoid units getting stuck inside by issuing them an extra
		// move-order. However, this order can *itself* cause the PF
		// system to consider the path blocked if the extra waypoint
		// falls within the factory's confines, so use a wide berth.
		const float3 fpSize = {unitDef->xsize * SQUARE_SIZE * 0.5f, 0.0f, unitDef->zsize * SQUARE_SIZE * 0.5f};
		const float3 fpMins = {unit->pos.x - fpSize.x, 0.0f, unit->pos.z - fpSize.z};
		const float3 fpMaxs = {unit->pos.x + fpSize.x, 0.0f, unit->pos.z + fpSize.z};

		float3 tmpVec;
		float3 tmpPos;

		for (int i = 0, k = 2 * (math::fabs(frontdir.z) > math::fabs(frontdir.x)); i < 128; i++) {
			tmpVec = frontdir * radius * (2.0f + i * 0.5f);
			tmpPos = unit->pos + tmpVec;

			if ((tmpPos[k] < fpMins[k]) || (tmpPos[k] > fpMaxs[k]))
				break;
		}

		c.PushPos(tmpPos.cClampInBounds());
	} else {
		// dummy rallypoint for aircraft
		c.PushPos(unit->pos);
	}

	if (unitCmdQue.empty()) {
		if (modInfo.insertBuiltUnitMoveCommand) {
			unitCAI->GiveCommand(c);
		}

		// copy factory orders for new unit
		for (auto ci = factoryCmdQue.begin(); ci != factoryCmdQue.end(); ++ci) {
			Command c = *ci;
			c.SetOpts(c.GetOpts() | SHIFT_KEY);

			if (c.GetID() == CMD_MOVE) {
				float xjit = gsRNG.NextFloat() * math::TWOPI;
				float zjit = gsRNG.NextFloat() * math::TWOPI;

				const float3 p1 = c.GetPos(0);
				const float3 p2 = float3(p1.x + xjit, p1.y, p1.z + zjit);

				// apply a small amount of random jitter to move commands
				// such that new units do not all share the same goal-pos
				// and start forming a "trail" back to the factory exit
				c.SetPos(0, p2);
			}

			unitCAI->GiveCommand(c);
		}
	} else if (modInfo.insertBuiltUnitMoveCommand) {
		unitCmdQue.push_front(c);
	}
}



bool CFactory::ChangeTeam(int newTeam, ChangeType type)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!CBuilding::ChangeTeam(newTeam, type))
		return false;

	if (curBuild)
		curBuild->ChangeTeam(newTeam, type);

	return true;
}


void CFactory::CreateNanoParticle(const float3& goal, float radius, bool inverse, bool highPriority)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int modelNanoPiece = nanoPieceCache.GetNanoPiece(script);

	if (!localModel.Initialized() || !localModel.HasPiece(modelNanoPiece))
		return;

	const float3 relNanoFirePos = localModel.GetRawPiecePos(modelNanoPiece);
	const float3 nanoPos = this->GetObjectSpacePos(relNanoFirePos);

	// unsynced
	projectileHandler.AddNanoParticle(nanoPos, goal, unitDef, team, radius, inverse, highPriority);
}

void CFactory::SetRepairTarget(CUnit* target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (target == curBuild)
		return;

	StopBuild(false);
	TempHoldFire(CMD_REPAIR);

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


void CFactory::SetReclaimTarget(CSolidObject* target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (dynamic_cast<CFeature*>(target) != nullptr && !static_cast<CFeature*>(target)->def->reclaimable)
		return;

	CUnit* recUnit = dynamic_cast<CUnit*>(target);

	if (recUnit != nullptr && !recUnit->unitDef->reclaimable)
		return;

	if (curReclaim == target || this == target)
		return;

	StopBuild(false);
	TempHoldFire(CMD_RECLAIM);

	reclaimingUnit = (recUnit != nullptr);
	curReclaim = target;

	AddDeathDependence(curReclaim, DEPENDENCE_RECLAIM);

	ScriptStartBuilding(target->pos, false);
}

bool CFactory::ScriptStartBuilding(float3 pos, bool silent)
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
}


bool CFactory::CanAssistUnit(const CUnit* u, const UnitDef* def) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!unitDef->canAssist)
		return false;

	return ((def == nullptr || u->unitDef == def) && u->beingBuilt && (u->buildProgress < 1.0f) && (u->soloBuilder == nullptr || u->soloBuilder == this));
}


bool CFactory::CanRepairUnit(const CUnit* u) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!unitDef->canRepair)
		return false;
	if (u->beingBuilt)
		return false;
	if (u->health >= u->maxHealth)
		return false;

	return (u->unitDef->repairable);
}
