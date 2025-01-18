/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cassert>

#include "BuilderBehaviourAI.h"
#include "BuilderCmdBehaviourAI.h"
#include "ExternalAI/EngineOutHandler.h"
#include "Game/GameHelper.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Map/Ground.h"
#include "Map/MapDamage.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Misc/Team.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/Units/Behaviour/BuilderBehaviour.h"
#include "Sim/Units/Behaviour/BuilderCmdBehaviour.h"
#include "Sim/Units/Behaviour/FactoryBehaviour.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/CommandAI/BuilderCaches.h"
#include "System/SpringMath.h"
#include "System/StringUtil.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/Log/ILog.h"
#include "System/creg/STL_Map.h"

#include "System/Misc/TracyDefs.h"

template CBuilderBehaviourAI* CCommandAI::GetBehaviourAI<CBuilderBehaviourAI>() const;

CR_BIND_DERIVED(CBuilderBehaviourAI, CBaseBuilderBehaviourAI , )

CR_REG_METADATA(CBuilderBehaviourAI , (
	CR_MEMBER(ownerBuilder),
	CR_MEMBER(building),
	CR_MEMBER(range3D),
	CR_IGNORED(build),
	CR_IGNORED(buildOptions),

	CR_MEMBER(cachedRadiusId),
	CR_MEMBER(cachedRadius),

	CR_MEMBER(buildRetries),
	CR_MEMBER(randomCounter),

	CR_POSTLOAD(PostLoad),
	CR_PREALLOC(GetPreallocContainer)
))


CBuilderBehaviourAI::CBuilderBehaviourAI():
	CBaseBuilderBehaviourAI(),
	ownerBuilder(nullptr),
	building(false),
	cachedRadiusId(0),
	cachedRadius(0),
	buildRetries(0),
	randomCounter(0),
	range3D(true)
{
}

CBuilderBehaviourAI::CBuilderBehaviourAI(CUnit* owner):
	CBaseBuilderBehaviourAI(owner),
	building(false),
	cachedRadiusId(0),
	cachedRadius(0),
	buildRetries(0),
	randomCounter(0),
	range3D(owner->unitDef->buildRange3D)
{
	ownerBuilder = owner->GetBehaviour<CBuilderBehaviour>();
	auto& possibleCommands = owner->commandAI->possibleCommands;

	for (const auto& bi: owner->unitDef->buildOptions) {
		const std::string& name = bi.second;
		const UnitDef* ud = unitDefHandler->GetUnitDefByName(name);

		if (ud == nullptr) {
			string errmsg = "MOD ERROR: loading ";
			errmsg += name.c_str();
			errmsg += " for ";
			errmsg += owner->unitDef->name;
			throw content_error(errmsg);
		}

		{
			SCommandDescription c;

			c.id   = -ud->id; //build options are always negative
			c.type = CMDTYPE_ICON_BUILDING;

			c.action    = "buildunit_" + StringToLower(ud->name);
			c.name      = name;
			c.mouseicon = c.name;
			c.tooltip   = GetUnitDefBuildOptionToolTip(ud, c.disabled = (ud->maxThisUnit <= 0));

			buildOptions.insert(c.id);
			possibleCommands.push_back(commandDescriptionCache.GetPtr(std::move(c)));
		}
	}

	unitHandler.AddBuilderCAI(this);
}

CBuilderBehaviourAI::~CBuilderBehaviourAI()
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBuilderCaches::RemoveUnitFromReclaimers(owner);
	CBuilderCaches::RemoveUnitFromFeatureReclaimers(owner);
	CBuilderCaches::RemoveUnitFromResurrecters(owner);
	unitHandler.RemoveBuilderCAI(this);
}

void CBuilderBehaviourAI::PostLoad()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& possibleCommands = owner->commandAI->possibleCommands;
	const auto& commandQue = owner->commandAI->commandQue;
	for (const SCommandDescription* cd: possibleCommands) {
		if (cd->id < 0)
			buildOptions.insert(cd->id);
	}
	if (commandQue.empty())
		return;

	ownerBuilder = owner->GetBehaviour<CBuilderBehaviour>();

	const Command& c = commandQue.front();

	if (buildOptions.find(c.GetID()) != buildOptions.end()) {
		build.Parse(c);
		build.pos = CGameHelper::Pos2BuildPos(build, true);
	}
}

float CBuilderBehaviourAI::GetBuildRange(const float targetRadius) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (ownerBuilder->buildDistance + targetRadius);
}

bool CBuilderBehaviourAI::IsInBuildRange(const CWorldObject* obj) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return IsInBuildRange(obj->pos, obj->buildeeRadius);
}

bool CBuilderBehaviourAI::IsInBuildRange(const float3& objPos, const float objRadius) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float immDistSqr = f3SqDist(owner->pos, objPos);
	const float buildDist = GetBuildRange(objRadius);

	return (immDistSqr <= (buildDist * buildDist));
}



inline bool CBuilderBehaviourAI::MoveInBuildRange(const CWorldObject* obj, const bool checkMoveTypeForFailed)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return MoveInBuildRange(obj->pos, obj->buildeeRadius, checkMoveTypeForFailed);
}

bool CBuilderBehaviourAI::MoveInBuildRange(const float3& objPos, float objRadius, const bool checkMoveTypeForFailed)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsInBuildRange(objPos, objRadius)) {
		// NOTE:
		//   ignore the fail-check if we are an aircraft, movetype code
		//   is unreliable wrt. setting it correctly and causes (landed)
		//   aircraft to discard orders
		const bool checkFailed = (checkMoveTypeForFailed && !owner->unitDef->IsAirUnit());
		// check if the AMoveType::Failed belongs to the same goal position
		const bool haveFailed = (owner->moveType->progressState == AMoveType::Failed && f3SqDist(owner->moveType->goalPos, objPos) > 1.0f);

		if (checkFailed && haveFailed) {
			// don't call SetGoal() it would reset moveType->progressState
			// and so later code couldn't check if the move command failed
			return false;
		}

		// too far away, start a move command
		SetGoal(objPos, owner->pos, GetBuildRange(objRadius) * 0.9f);
		return false;
	}

	if (owner->unitDef->IsAirUnit()) {
		StopMoveAndKeepPointing(objPos, GetBuildRange(objRadius) * 0.9f, false);
	} else {
		StopMoveAndKeepPointing(owner->moveType->goalPos, GetBuildRange(objRadius) * 0.9f, false);
	}

	return true;
}


bool CBuilderBehaviourAI::IsBuildPosBlocked(const BuildInfo& bi, const CUnit** nanoFrame) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	CFeature* feature = nullptr;
	CGameHelper::BuildSquareStatus status = CGameHelper::TestUnitBuildSquare(bi, feature, owner->allyteam, true);

	// buildjob is a feature and it is finished already
	if (feature != nullptr && bi.def->isFeature && bi.def->wreckName == feature->def->name)
		return true;

	// open area, reclaimable feature or movable unit
	if (status != CGameHelper::BUILDSQUARE_BLOCKED)
		return false;

	const CSolidObject* s = nullptr;
	const CUnit* u = nullptr;

	const int2 mins = CSolidObject::GetMapPosStatic(bi.pos, bi.GetXSize(), bi.GetZSize());
	const int2 maxs = mins + int2(bi.GetXSize(), bi.GetZSize());
	for (int z = mins.y; z < maxs.y; ++z) {
		for (int x = mins.x; x < maxs.x; ++x) {
			s = groundBlockingObjectMap.GroundBlocked(float3{
				static_cast<float>(x * SQUARE_SIZE),
				0.0f,
				static_cast<float>(z * SQUARE_SIZE) }
			);

			if (s == nullptr)
				continue;

			// just ourselves, does not count
			if (s == owner)
				continue;

			if (u = dynamic_cast<const CUnit*>(s), u == nullptr)
				continue;

			// figure out if object is soft- or hard-blocking
			if (u->beingBuilt) {
				// we can't or don't want assist finishing the nanoframe
				// if a mobile unit blocks the position, wait until it is
				// finished & moved
				auto* ownerCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
				if (!ownerCmd || !ownerCmd->CanAssistUnit(u, bi.def))
					continue;

				// unfinished nanoframe, assist it
				if (nanoFrame != nullptr && teamHandler.Ally(owner->allyteam, u->allyteam))
					*nanoFrame = u;

				return false; //be greedy here
			}
		}
	}

	// if a *unit* object is not present, then either
	// there is a feature or the terrain is unsuitable
	// (in the former case feature must be reclaimable)
	if (u == nullptr)
		return (feature == nullptr || !feature->def->reclaimable);

	// unit blocks the pos, can it move away?
	return (u->immobile);
}


float CBuilderBehaviourAI::GetBuildOptionRadius(const UnitDef* ud, int cmdId)
{
	RECOIL_DETAILED_TRACY_ZONE;
	float radius = cachedRadius;

	if (cachedRadiusId != cmdId) {
		radius = ud->GetModelRadius();
		cachedRadius = radius;
		cachedRadiusId = cmdId;
	}

	return radius;
}


void CBuilderBehaviourAI::CancelRestrictedUnit()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (owner->team == gu->myTeam) {
		LOG_L(L_WARNING, "%s: Build failed, unit type limit reached", owner->unitDef->humanName.c_str());
		eventHandler.LastMessagePosition(owner->pos);
	}
	StopMoveAndFinishCommand();
}


bool CBuilderBehaviourAI::GiveCommandReal(const Command& c, bool fromSynced)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& nonQueingCommands = owner->commandAI->nonQueingCommands;
	if (!owner->commandAI->AllowedCommand(c, fromSynced))
		return true;

	// don't guard yourself
	if ((c.GetID() == CMD_GUARD) &&
	    (c.GetNumParams() == 1) && ((int)c.GetParam(0) == owner->id)) {
		return true;
	}

	// stop building/reclaiming/... if the new command is not queued, i.e. replaces our current activity
	// FIXME should happen just before CMobileCAI::GiveCommandReal? (the new cmd can still be skipped!)
	if ((c.GetID() != CMD_WAIT) && !(c.GetOpts() & SHIFT_KEY)) {
		if (nonQueingCommands.find(c.GetID()) == nonQueingCommands.end()) {
			building = false;
			ownerBuilder->StopBuild();
		}
	}

	if (buildOptions.find(c.GetID()) != buildOptions.end()) {
		if (c.GetNumParams() < 3)
			return true;

		BuildInfo bi;
		bi.pos = c.GetPos(0);

		if (c.GetNumParams() == 4)
			bi.buildFacing = abs((int)c.GetParam(3)) % NUM_FACINGS;

		bi.def = unitDefHandler->GetUnitDefByID(-c.GetID());
		bi.pos = CGameHelper::Pos2BuildPos(bi, true);

		// We are a static building, check if the buildcmd is in range
		if (!owner->unitDef->canmove) {
			if (!IsInBuildRange(bi.pos, GetBuildOptionRadius(bi.def, c.GetID())))
				return true;
		}

		const CUnit* nanoFrame = nullptr;

		// check if the buildpos is blocked
		if (IsBuildPosBlocked(bi, &nanoFrame))
			return true;

		// if it is a nanoframe help to finish it
		if (nanoFrame != nullptr) {
			Command c2(CMD_REPAIR, c.GetOpts() | INTERNAL_ORDER, nanoFrame->id);
			CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
			cai->GiveCommandReal(c2, fromSynced);
			cai->GiveCommandReal(c, fromSynced);
			return true;
		}
	} else {
		if (c.GetID() < 0)
			return true;
	}
	return false;
}


bool CBuilderBehaviourAI::SlowUpdate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& commandQue = owner->commandAI->commandQue;
	if (gs->paused) // Commands issued may invoke SlowUpdate when paused
		return true;

	if (commandQue.empty()) {
		//CMobileCAI::SlowUpdate();
		return false;
	}

	if (owner->beingBuilt || owner->IsStunned())
		return true;

	Command& c = commandQue.front();

	// TODO: rethink how to integrate SlowUpdate
	auto* builderCmdAI = owner->commandAI->GetBehaviourAI<CBuilderCmdBehaviourAI>();
	if (builderCmdAI != nullptr && builderCmdAI->OutOfImmobileRange(c)) {
		FinishCommand();
		return true;
	}

	if (c.GetID() == CMD_STOP) {
		ExecuteStop(c);
		return true;
	}
	if (builderCmdAI != nullptr) {
		switch (c.GetID()) {
			case CMD_REPAIR:    { builderCmdAI->ExecuteRepair(c);    return true; }
			case CMD_CAPTURE:   { builderCmdAI->ExecuteCapture(c);   return true; }
			case CMD_GUARD:     { builderCmdAI->ExecuteGuard(c);     return true; }
			case CMD_RECLAIM:   { builderCmdAI->ExecuteReclaim(c);   return true; }
			case CMD_RESURRECT: { builderCmdAI->ExecuteResurrect(c); return true; }
			case CMD_PATROL:    { builderCmdAI->ExecutePatrol(c);    return true; }
			case CMD_FIGHT:     { builderCmdAI->ExecuteFight(c);     return true; }
			case CMD_RESTORE:   { builderCmdAI->ExecuteRestore(c);   return true; }
		}
	}
	if (c.GetID() < 0) {
		ExecuteBuildCmd(c);
		return true;
	}

	return false;
}


void CBuilderBehaviourAI::FinishCommand()
{
	RECOIL_DETAILED_TRACY_ZONE;
	buildRetries = 0;
	//CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	//cai->FinishCommand();
}


void CBuilderBehaviourAI::ExecuteStop(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	building = false;
	ownerBuilder->StopBuild();
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	cai->ExecuteStop(c);
}


void CBuilderBehaviourAI::ExecuteBuildCmd(Command& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& inCommand = owner->commandAI->inCommand;
	if (buildOptions.find(c.GetID()) == buildOptions.end())
		return;

	if (!inCommand) {
		BuildInfo bi;

		// note:
		//   need at least 3 parameters or BuildInfo will fail to parse
		//   this usually indicates a malformed command inserted by Lua
		//   (most common with patrolling pseudo-factory hubs)
		if (!bi.Parse(c)) {
			StopMoveAndFinishCommand();
			return;
		}

		#if 1
		// snap build-position to multiples of SQUARE_SIZE
		bi.pos.x = math::floor(c.GetParam(0) / SQUARE_SIZE) * SQUARE_SIZE;
		bi.pos.z = math::floor(c.GetParam(2) / SQUARE_SIZE) * SQUARE_SIZE;
		#endif

		CFeature* f = nullptr;
		CGameHelper::TestUnitBuildSquare(bi, f, owner->allyteam, true);

		if (f != nullptr) {
			// TODO REFACTOR: maybe move this to method inside builderCmdAI and make ReclaimFeature private again
			auto* builderCmdAI = owner->commandAI->GetBehaviourAI<CBuilderCmdBehaviourAI>();
			if (builderCmdAI != nullptr && (!bi.def->isFeature || bi.def->wreckName != f->def->name)) {
				builderCmdAI->ReclaimFeature(f);
			} else {
				StopMoveAndFinishCommand();
			}
			return;
		}

		// <build> is never parsed (except in PostLoad) so just copy it
		build = bi;
		inCommand = true;
	}

	assert(build.def != nullptr);
	assert(build.def->id == -c.GetID() && build.def->id != 0);

	float objRadius = build.def->buildeeBuildRadius;
	if (objRadius < 0.f) {
		auto* model = build.def->LoadModel();
		objRadius = std::max(0.f, model->radius);
	}

	if (building) {
		// keep moving until 3D distance to buildPos is LEQ our buildDistance
		MoveInBuildRange(build.pos, objRadius);

		auto* ownerCmd = owner->GetBehaviour<CBuilderCmdBehaviour>();
		if (ownerBuilder->curBuild == nullptr && (!ownerCmd || !ownerCmd->terraforming)) {
			building = false;
			StopMoveAndFinishCommand();
		}

		return;
	}

	// keep moving until 3D distance to buildPos is LEQ our buildDistance
	if (MoveInBuildRange(build.pos = CGameHelper::Pos2BuildPos(build, true), objRadius, true)) {
		if (IsBuildPosBlocked(build)) {
			StopMoveAndFinishCommand();
			return;
		}

		const auto [allow, drop] = eventHandler.AllowUnitCreation(build.def, owner, &build);
		if (!allow) {
			if (drop)
				StopMoveAndFinishCommand();
			return;
		}

		if (teamHandler.Team(owner->team)->AtUnitLimit())
			return;

		CFeature* f = nullptr;

		bool inWaitStance = false;
		bool limitReached = false;

		if (ownerBuilder->StartBuild(build, f, inWaitStance, limitReached) || (++buildRetries > 30)) {
			building = true;
			return;
		}

		// we can't reliably check if the unit-limit has been reached until
		// the builder has reached the construction site, which is somewhat
		// annoying (since greyed-out icons can still be clicked, etc)
		if (limitReached) {
			CancelRestrictedUnit();
			StopMove();
			return;
		}

		auto* builderCmdAI = owner->commandAI->GetBehaviourAI<CBuilderCmdBehaviourAI>();
		if (f != nullptr && builderCmdAI != nullptr && (!build.def->isFeature || build.def->wreckName != f->def->name)) {
			inCommand = false;
			builderCmdAI->ReclaimFeature(f);
			return;
		}

		if (!inWaitStance) {
			const float xhalf = (((build.buildFacing & 1) == 0) ? build.def->xsize : build.def->zsize)* 0.5f * SQUARE_SIZE;
			const float zhalf = (((build.buildFacing & 1) == 1) ? build.def->xsize : build.def->zsize)* 0.5f * SQUARE_SIZE;

			const float3 mins{build.pos.x - xhalf, build.pos.y, build.pos.z - zhalf};
			const float3 maxs{build.pos.x + xhalf, build.pos.y, build.pos.z + zhalf};

			CGameHelper::BuggerOffRectangle(mins, maxs, true, owner->team, nullptr);

			NonMoving();
			return;
		}

		return;
	}

	if (owner->moveType->progressState == AMoveType::Failed) {
		if (++buildRetries > 5) {
			StopMoveAndFinishCommand();
			return;
		}
	}

	// we are on the way to the buildpos, meanwhile it can happen
	// that another builder already finished our buildcmd or blocked
	// the buildpos with another building (skip our buildcmd then)
	if ((++randomCounter % 5) == 0) {
		if (IsBuildPosBlocked(build)) {
			StopMoveAndFinishCommand();
			return;
		}
	}
}


int CBuilderBehaviourAI::GetDefaultCmd(const CUnit* pointed, const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (pointed != nullptr) {
		if (!teamHandler.Ally(gu->myAllyTeam, pointed->allyteam)) {
			if (owner->unitDef->canAttack && (owner->maxRange > 0.0f))
				return CMD_ATTACK;

			if (owner->unitDef->canReclaim && pointed->unitDef->reclaimable)
				return CMD_RECLAIM;
		} else if (auto* ownerCmd = owner->GetBehaviour<CBuilderCmdBehaviour>()) {
			const bool canAssistPointed = ownerCmd->CanAssistUnit(pointed);
			const bool canRepairPointed = ownerCmd->CanRepairUnit(pointed);

			if (canAssistPointed)
				return CMD_REPAIR;
			if (canRepairPointed)
				return CMD_REPAIR;

			if (pointed->CanTransport(owner))
				return CMD_LOAD_ONTO;
			if (owner->unitDef->canGuard)
				return CMD_GUARD;
		}
	}

	if (feature != nullptr) {
		if (owner->unitDef->canResurrect && feature->udef != nullptr)
			return CMD_RESURRECT;

		if (owner->unitDef->canReclaim && feature->def->reclaimable)
			return CMD_RECLAIM;
	}

	return CMD_MOVE;
}


bool CBuilderBehaviourAI::BuggerOff(const float3& pos, float radius) {
	RECOIL_DETAILED_TRACY_ZONE;
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	if (owner->unitDef->IsStaticBuilderUnit())
		return true;
	return false;
}

void CBuilderBehaviourAI::StopMoveAndFinishCommand() {
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	cai->StopMoveAndFinishCommand();
}

void CBuilderBehaviourAI::StopMove() {
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	cai->StopMove();
}

void CBuilderBehaviourAI::SetGoal(const float3& pos, const float3& curPos, float goalRadius) {
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	cai->SetGoal(pos, curPos, goalRadius);
}

void CBuilderBehaviourAI::SetGoal(const float3& pos, const float3& curPos, float goalRadius, float speed) {
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	cai->SetGoal(pos, curPos, goalRadius, speed);
}

void CBuilderBehaviourAI::NonMoving() {
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	cai->NonMoving();
}

void CBuilderBehaviourAI::StopMoveAndKeepPointing(const float3& p, const float r, bool b) {
	CMobileCAI* cai = static_cast<CMobileCAI*>(owner->commandAI);
	return cai->StopMoveAndKeepPointing(p, r, b);
}

