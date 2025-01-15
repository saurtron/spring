/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "CommandDrawer.h"
#include "LineDrawer.h"
#include "Game/GameHelper.h"
#include "Game/UI/CommandColors.h"
#include "Game/WaitCommandsAI.h"
#include "Map/Ground.h"
#include "Rendering/GL/glExtra.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/CommandAI/Command.h"
#include "Sim/Units/CommandAI/CommandQueue.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/AirCAI.h"
#include "Sim/Units/BehaviourAI/BuilderBehaviourAI.h"
#include "Sim/Units/BehaviourAI/FactoryBehaviourAI.h"
#include "Sim/Units/CommandAI/MobileCAI.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitDefHandler.h"
#include "System/SpringMath.h"
#include "System/Log/ILog.h"

static const CUnit* GetTrackableUnit(const CUnit* caiOwner, const CUnit* cmdUnit)
{
	if (cmdUnit == nullptr)
		return nullptr;
	if ((cmdUnit->losStatus[caiOwner->allyteam] & (LOS_INLOS | LOS_INRADAR)) == 0)
		return nullptr;

	return cmdUnit;
}

CommandDrawer* CommandDrawer::GetInstance() {
	// luaQueuedUnitSet gets cleared each frame, so this is fine wrt. reloading
	static CommandDrawer drawer;
	return &drawer;
}



void CommandDrawer::Draw(const CCommandAI* cai, int queueDrawDepth) const {
	// note: {Air,Builder}CAI inherit from MobileCAI, so test that last
	if ((dynamic_cast<const     CAirCAI*>(cai)) != nullptr) {     DrawAirCAICommands(static_cast<const     CAirCAI*>(cai), queueDrawDepth); return; }
	if (cai->GetBehaviourAI<CBuilderBehaviourAI>() != nullptr) { DrawBuilderCAICommands(cai, queueDrawDepth); return; }
	if (cai->GetBehaviourAI<CFactoryBehaviourAI>() != nullptr) { DrawFactoryCAICommands(cai, queueDrawDepth); return; }
	if ((dynamic_cast<const  CMobileCAI*>(cai)) != nullptr) {  DrawMobileCAICommands(static_cast<const  CMobileCAI*>(cai), queueDrawDepth); return; }

	DrawCommands(cai, queueDrawDepth);
}



void CommandDrawer::AddLuaQueuedUnit(const CUnit* unit, int queueDrawDepth) {
	// needs to insert by id, pointers can become dangling
	luaQueuedUnitSet.insert({ unit->id, queueDrawDepth });
}

void CommandDrawer::DrawLuaQueuedUnitSetCommands() const
{
	if (luaQueuedUnitSet.empty())
		return;

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	lineDrawer.Configure(cmdColors.UseColorRestarts(),
	                     cmdColors.UseRestartColor(),
	                     cmdColors.restart,
	                     cmdColors.RestartAlpha());
	lineDrawer.SetupLineStipple();

	glEnable(GL_BLEND);
	glBlendFunc((GLenum)cmdColors.QueuedBlendSrc(),
	            (GLenum)cmdColors.QueuedBlendDst());

	glLineWidth(cmdColors.QueuedLineWidth());

	for (const auto& [unitID, qDrawDepth] : luaQueuedUnitSet) {
		const CUnit* unit = unitHandler.GetUnit(unitID);

		if (unit == nullptr || unit->commandAI == nullptr)
			continue;

		Draw(unit->commandAI, qDrawDepth);
	}

	glLineWidth(1.0f);
	glEnable(GL_DEPTH_TEST);
}

void CommandDrawer::DrawCommands(const CCommandAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		if (std::distance(commandQue.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawAirCAICommands(const CAirCAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		if (std::distance(commandQue.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_FIGHT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;

			case CMD_ATTACK: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_AREA_ATTACK: {
				const float3& endPos = ci->GetPos(0);

				lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.attack);
				lineDrawer.Break(endPos, cmdColors.attack);

				glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.attack }, cmdCircleResolution);

				lineDrawer.RestartWithColor(cmdColors.attack);
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawBuilderCAICommands(const CCommandAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CBuilderBehaviourAI* builderCAI = cai->GetBehaviourAI<CBuilderBehaviourAI>();
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		if (std::distance(commandQue.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		if (cmdID < 0) {
			if (builderCAI->buildOptions.find(cmdID) != builderCAI->buildOptions.end()) {
				BuildInfo bi;

				if (!bi.Parse(*ci))
					continue;

				cursorIcons.AddBuildIcon(cmdID, bi.pos, owner->team, bi.buildFacing);
				lineDrawer.DrawLine(bi.pos, cmdColors.build);

				// draw metal extraction range
				if (bi.def->extractRange > 0.0f) {
					lineDrawer.Break(bi.pos, cmdColors.build);
					glSurfaceCircle(bi.pos, bi.def->extractRange, { cmdColors.rangeExtract }, 40.0f);
					lineDrawer.Restart();
				}
			}
			continue;
		}

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_FIGHT:{
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_RESTORE: {
				const float3& endPos = ci->GetPos(0);

				lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.restore);
				lineDrawer.Break(endPos, cmdColors.restore);

				glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.restore }, cmdCircleResolution);

				lineDrawer.RestartWithColor(cmdColors.restore);
			} break;

			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_RECLAIM:
			case CMD_RESURRECT: {
				const float* color = (cmdID == CMD_RECLAIM) ? cmdColors.reclaim
				                                             : cmdColors.resurrect;
				if (ci->GetNumParams() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, color);
					lineDrawer.Break(endPos, color);

					glSurfaceCircle(endPos, ci->GetParam(3), { color }, cmdCircleResolution);

					lineDrawer.RestartWithColor(color);
				} else {
					assert(ci->GetParam(0) >= 0.0f);

					const unsigned int id = std::max(0.0f, ci->GetParam(0));

					if (id >= unitHandler.MaxUnits()) {
						const CFeature* feature = featureHandler.GetFeature(id - unitHandler.MaxUnits());

						if (feature != nullptr)
							lineDrawer.DrawLineAndIcon(cmdID, feature->GetObjDrawMidPos(), color);

					} else {
						const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(id));

						if (unit != nullptr && unit != owner)
							lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), color);

					}
				}
			} break;

			case CMD_REPAIR:
			case CMD_CAPTURE: {
				const float* color = (ci->GetID() == CMD_REPAIR) ? cmdColors.repair: cmdColors.capture;

				if (ci->GetNumParams() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, color);
					lineDrawer.Break(endPos, color);

					glSurfaceCircle(endPos, ci->GetParam(3), { color }, cmdCircleResolution);

					lineDrawer.RestartWithColor(color);
				} else {
					if (ci->GetNumParams() >= 1) {
						const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

						if (unit != nullptr)
							lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), color);

					}
				}
			} break;

			case CMD_LOAD_ONTO: {
				const CUnit* unit = unitHandler.GetUnitUnsafe(ci->GetParam(0));
				lineDrawer.DrawLineAndIcon(cmdID, unit->pos, cmdColors.load);
			} break;
			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(ci->GetID());
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawFactoryCAICommands(const CCommandAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CFactoryBehaviourAI* factoryCAI = cai->GetBehaviourAI<CFactoryBehaviourAI>();
	const CCommandQueue& commandQue = cai->commandQue;
	const CCommandQueue& newUnitCommands = factoryCAI->newUnitCommands;

	if (queueDrawDepth <= 0)
		queueDrawDepth = newUnitCommands.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	if (!commandQue.empty() && (commandQue.front().GetID() == CMD_WAIT))
		DrawWaitIcon(commandQue.front());

	for (auto ci = newUnitCommands.begin(); ci != newUnitCommands.end(); ++ci) {
		if (std::distance(newUnitCommands.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0) + UpVector * 3.0f, cmdColors.move);
			} break;
			case CMD_FIGHT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0) + UpVector * 3.0f, cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0) + UpVector * 3.0f, cmdColors.patrol);
			} break;

			case CMD_ATTACK: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}

		if ((cmdID < 0) && (ci->GetNumParams() >= 3)) {
			BuildInfo bi;

			if (!bi.Parse(*ci))
				continue;

			cursorIcons.AddBuildIcon(cmdID, bi.pos, owner->team, bi.buildFacing);
			lineDrawer.DrawLine(bi.pos, cmdColors.build);

			// draw metal extraction range
			if (bi.def->extractRange > 0.0f) {
				lineDrawer.Break(bi.pos, cmdColors.build);
				glSurfaceCircle(bi.pos, bi.def->extractRange, { cmdColors.rangeExtract }, 40.0f);
				lineDrawer.Restart();
			}
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawMobileCAICommands(const CMobileCAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;
			case CMD_FIGHT: {
				if (ci->GetNumParams() >= 3)
					lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);

			} break;

			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				}

				if (ci->GetNumParams() >= 3) {
					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_LOAD_ONTO: {
				const CUnit* unit = unitHandler.GetUnitUnsafe(ci->GetParam(0));
				lineDrawer.DrawLineAndIcon(cmdID, unit->pos, cmdColors.load);
			} break;

			case CMD_LOAD_UNITS: {
				if (ci->GetNumParams() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.load);
					lineDrawer.Break(endPos, cmdColors.load);

					glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.load }, cmdCircleResolution);

					lineDrawer.RestartWithColor(cmdColors.load);
				} else {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.load);

				}
			} break;

			case CMD_UNLOAD_UNITS: {
				if (ci->GetNumParams() == 5) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.unload);
					lineDrawer.Break(endPos, cmdColors.unload);

					glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.unload }, cmdCircleResolution);

					lineDrawer.RestartWithColor(cmdColors.unload);
				}
			} break;

			case CMD_UNLOAD_UNIT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.unload);
			} break;
			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}


void CommandDrawer::DrawWaitIcon(const Command& cmd) const
{
	waitCommandsAI.AddIcon(cmd, lineDrawer.GetLastPos());
}

void CommandDrawer::DrawDefaultCommand(const Command& c, const CUnit* owner) const
{
	// TODO add Lua callin perhaps, for more elaborate needs?
	const CCommandColors::DrawData* dd = cmdColors.GetCustomCmdData(c.GetID());

	if (dd == nullptr)
		return;

	switch (c.GetNumParams()) {
		case  0: { return; } break;
		case  1: {         } break;
		case  2: {         } break;
		default: {
			const float3 endPos = c.GetPos(0) + UpVector * 3.0f;

			if (!dd->showArea || (c.GetNumParams() < 4)) {
				lineDrawer.DrawLineAndIcon(dd->cmdIconID, endPos, dd->color);
			} else {
				lineDrawer.DrawLineAndIcon(dd->cmdIconID, endPos, dd->color);
				lineDrawer.Break(endPos, dd->color);
				glSurfaceCircle(endPos, c.GetParam(3), { dd->color }, cmdCircleResolution);
				lineDrawer.RestartWithColor(dd->color);
			}

			return;
		}
	}

	// allow a second param (ignored here) for custom commands
	const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(c.GetParam(0)));

	if (unit == nullptr)
		return;

	lineDrawer.DrawLineAndIcon(dd->cmdIconID, unit->GetObjDrawErrorPos(owner->allyteam), dd->color);
}

void CommandDrawer::DrawQuedBuildingSquares(const CCommandAI* cai) const
{
	const CCommandQueue& commandQue = cai->commandQue;
	const CBuilderBehaviourAI* builderCAI = cai->GetBehaviourAI<CBuilderBehaviourAI>();
	const auto& buildOptions = builderCAI->buildOptions;

	unsigned int  buildCommands = 0;
	unsigned int uwaterCommands = 0;

	for (const Command& c: commandQue) {
		if (buildOptions.find(c.GetID()) == buildOptions.end())
			continue;

		BuildInfo bi;

		if (!bi.Parse(c))
			continue;

		bi.pos = CGameHelper::Pos2BuildPos(bi, false);

		buildCommands += 1;
		uwaterCommands += (bi.pos.y < CGround::GetWaterLevel(bi.pos.x, bi.pos.z));
	}

	// worst case - 2 squares per building (when underwater) - 8 vertices * 3 floats
	std::vector<GLfloat>   quadVerts(buildCommands * 12);
	std::vector<GLfloat> uwquadVerts(buildCommands * 12); // underwater
	// 4 vertical lines
	std::vector<GLfloat> lineVerts(uwaterCommands * 24);
	// colors for lines
	std::vector<GLfloat> lineColors(uwaterCommands * 48);

	unsigned int   quadcounter = 0;
	unsigned int uwquadcounter = 0;
	unsigned int   linecounter = 0;

	for (const Command& c: commandQue) {
		if (buildOptions.find(c.GetID()) == buildOptions.end())
			continue;

		BuildInfo bi;

		if (!bi.Parse(c))
			continue;

		bi.pos = CGameHelper::Pos2BuildPos(bi, false);

		const float xsize = bi.GetXSize() * (SQUARE_SIZE >> 1);
		const float zsize = bi.GetZSize() * (SQUARE_SIZE >> 1);

		const float h = bi.pos.y;
		const float x1 = bi.pos.x - xsize;
		const float z1 = bi.pos.z - zsize;
		const float x2 = bi.pos.x + xsize;
		const float z2 = bi.pos.z + zsize;

		quadVerts[quadcounter++] = x1;
		quadVerts[quadcounter++] = h + 1;
		quadVerts[quadcounter++] = z1;
		quadVerts[quadcounter++] = x1;
		quadVerts[quadcounter++] = h + 1;
		quadVerts[quadcounter++] = z2;
		quadVerts[quadcounter++] = x2;
		quadVerts[quadcounter++] = h + 1;
		quadVerts[quadcounter++] = z2;
		quadVerts[quadcounter++] = x2;
		quadVerts[quadcounter++] = h + 1;
		quadVerts[quadcounter++] = z1;

		if (bi.pos.y >= 0.0f)
			continue;

		const float col[8] = {
			0.0f, 0.0f, 1.0f, 0.5f, // start color
			0.0f, 0.5f, 1.0f, 1.0f, // end color
		};

		uwquadVerts[uwquadcounter++] = x1;
		uwquadVerts[uwquadcounter++] = 0.0f;
		uwquadVerts[uwquadcounter++] = z1;
		uwquadVerts[uwquadcounter++] = x1;
		uwquadVerts[uwquadcounter++] = 0.0f;
		uwquadVerts[uwquadcounter++] = z2;
		uwquadVerts[uwquadcounter++] = x2;
		uwquadVerts[uwquadcounter++] = 0.0f;
		uwquadVerts[uwquadcounter++] = z2;
		uwquadVerts[uwquadcounter++] = x2;
		uwquadVerts[uwquadcounter++] = 0.0f;
		uwquadVerts[uwquadcounter++] = z1;

		for (int i = 0; i < 4; ++i) {
			std::copy(col, col + 8, lineColors.begin() + linecounter * 2 + i * 8);
		}

		lineVerts[linecounter++] = x1;
		lineVerts[linecounter++] = h;
		lineVerts[linecounter++] = z1;
		lineVerts[linecounter++] = x1;
		lineVerts[linecounter++] = 0.0f;
		lineVerts[linecounter++] = z1;

		lineVerts[linecounter++] = x2;
		lineVerts[linecounter++] = h;
		lineVerts[linecounter++] = z1;
		lineVerts[linecounter++] = x2;
		lineVerts[linecounter++] = 0.0f;
		lineVerts[linecounter++] = z1;

		lineVerts[linecounter++] = x2;
		lineVerts[linecounter++] = h;
		lineVerts[linecounter++] = z2;
		lineVerts[linecounter++] = x2;
		lineVerts[linecounter++] = 0.0f;
		lineVerts[linecounter++] = z2;

		lineVerts[linecounter++] = x1;
		lineVerts[linecounter++] = h;
		lineVerts[linecounter++] = z2;
		lineVerts[linecounter++] = x1;
		lineVerts[linecounter++] = 0.0f;
		lineVerts[linecounter++] = z2;
	}

	if (quadcounter > 0) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glVertexPointer(3, GL_FLOAT, 0, &quadVerts[0]);
		glDrawArrays(GL_QUADS, 0, quadcounter / 3);

		if (linecounter > 0) {
			glPushAttrib(GL_CURRENT_BIT);
			glColor4f(0.0f, 0.5f, 1.0f, 1.0f); // same as end color of lines
			glVertexPointer(3, GL_FLOAT, 0, &uwquadVerts[0]);
			glDrawArrays(GL_QUADS, 0, uwquadcounter / 3);
			glPopAttrib();

			glEnableClientState(GL_COLOR_ARRAY);
			glColorPointer(4, GL_FLOAT, 0, &lineColors[0]);
			glVertexPointer(3, GL_FLOAT, 0, &lineVerts[0]);
			glDrawArrays(GL_LINES, 0, linecounter / 3);
			glDisableClientState(GL_COLOR_ARRAY);
		}

		glDisableClientState(GL_VERTEX_ARRAY);
	}
}

