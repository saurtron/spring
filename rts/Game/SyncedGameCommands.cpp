/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */
#include "SyncedGameCommands.h"

#include "Action.h"
#include "Game.h"
#include "GlobalUnsynced.h"
#include "InMapDraw.h"
#include "SelectedUnitsHandler.h"
#include "SyncedActionExecutor.h"
#ifdef _WIN32
#  include "winerror.h" // TODO someone on windows (MinGW? VS?) please check if this is required
#endif

#include "Game/Players/Player.h"
#include "Game/Players/PlayerHandler.h"
#include "Lua/LuaGaia.h"
#include "Lua/LuaRules.h"
#include "Lua/LuaUI.h"
#include "Net/GameServer.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Misc/ModInfo.h"
#include "Sim/Projectiles/ExplosionGenerator.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitLoader.h"
#include "Sim/Units/Unit.h"
#include "System/EventHandler.h"
#include "System/FileSystem/SimpleParser.h"
#include "System/Log/ILog.h"
#include "System/SafeUtil.h"

#include "Custom/CGadgetHandler.h"

#include <string>
#include <vector>
#include <stdexcept>


namespace { // prevents linking problems in case of duplicate symbols

class CheatActionExecutor : public ISyncedActionExecutor {
public:
	CheatActionExecutor() : ISyncedActionExecutor(
		"Cheat",
		"Enables/Disables cheating, which is required "
		"for a lot of other commands to be usable"
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		InverseOrSetBool(gs->cheatEnabled, action.GetArgs());
		LogSystemStatus("Cheating", gs->cheatEnabled);
		return true;
	}
};


class NoHelpActionExecutor : public ISyncedActionExecutor {
public:
	NoHelpActionExecutor() : ISyncedActionExecutor("NoHelp", "Enables/Disables widgets (LuaUI control)") {
	}

	bool Execute(const SyncedAction& action) const final {
		InverseOrSetBool(gs->noHelperAIs, action.GetArgs());
		selectedUnitsHandler.PossibleCommandChange(nullptr);
		LogSystemStatus("LuaUI control", gs->noHelperAIs);
		return true;
	}
};


class NoSpecDrawActionExecutor : public ISyncedActionExecutor {
public:
	NoSpecDrawActionExecutor() : ISyncedActionExecutor("NoSpecDraw", "Allows/Disallows spectators to draw on the map") {
	}

	bool Execute(const SyncedAction& action) const final {
		bool allowSpecMapDrawing = inMapDrawer->GetSpecMapDrawingAllowed();
		InverseOrSetBool(allowSpecMapDrawing, action.GetArgs(), true);
		inMapDrawer->SetSpecMapDrawingAllowed(allowSpecMapDrawing);
		return true;
	}
};


class GodModeActionExecutor : public ISyncedActionExecutor {
public:
	GodModeActionExecutor() : ISyncedActionExecutor(
		"GodMode",
		"Enables/Disables god-mode, which allows all players "
		"(even spectators) to control all units (even during "
		"replays, which will DESYNC them)",
		true
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		const std::string& args = action.GetArgs();

		if (args.empty()) {
			gs->godMode = GODMODE_MAX_VAL - gs->godMode;
		} else {
			gs->godMode = std::clamp(atoi(args.c_str()), 0, int(GODMODE_MAX_VAL));
		}

		CLuaUI::UpdateTeams();
		CPlayer::UpdateControlledTeams();

		switch (gs->godMode) {
			case               0: { LOG("[GodModeAction] god-mode disabled"              ); } break;
			case GODMODE_ATC_BIT: { LOG("[GodModeAction] god-mode enabled (allied teams)"); } break;
			case GODMODE_ETC_BIT: { LOG("[GodModeAction] god-mode enabled (enemy teams)" ); } break;
			case GODMODE_MAX_VAL: { LOG("[GodModeAction] god-mode enabled (all teams)"   ); } break;
		}

		return true;
	}
};


class GlobalLosActionExecutor : public ISyncedActionExecutor {
public:
	GlobalLosActionExecutor() : ISyncedActionExecutor(
		"GlobalLOS",
		"Enables/Disables global line-of-sight, which makes the whole map"
		" permanently visible to everyone or to a specific allyteam",
		true
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		const std::string& args = action.GetArgs();

		const unsigned int argAllyTeam = atoi(args.c_str());
		const unsigned int maxAllyTeam = teamHandler.ActiveAllyTeams();

		if (args.empty()) {
			for (unsigned int n = 0; n < maxAllyTeam; n++) {
				losHandler->FlipGlobalLOS(n);
			}

			LOG("[GlobalLosActionExecutor] global LOS toggled for all allyteams");
			return true;
		}
		if (argAllyTeam < maxAllyTeam) {
			losHandler->FlipGlobalLOS(argAllyTeam);

			LOG("[GlobalLosActionExecutor] global LOS toggled for allyteam %u", argAllyTeam);
			return true;
		}

		LOG("[GlobalLosActionExecutor] bad allyteam %u", argAllyTeam);
		return false;
	}
};


class NoCostActionExecutor : public ISyncedActionExecutor {
public:
	NoCostActionExecutor() : ISyncedActionExecutor(
		"NoCost",
		"Enables/Disables everything-for-free, which allows "
		"everyone to build everything for zero resource costs",
		true
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		bool isFree = unitDefHandler->GetNoCost();
		InverseOrSetBool(isFree, action.GetArgs());
		unitDefHandler->SetNoCost(isFree);
		LogSystemStatus("Everything-for-free (no resource costs for building)", isFree);
		return true;
	}
};


class GiveActionExecutor : public ISyncedActionExecutor {
public:
	GiveActionExecutor() : ISyncedActionExecutor(
		"Give",
		"Places one or multiple units of a single or multiple types "
		"on the map, instantly; by default belonging to your own team",
		true
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		// not for autohosts
		if (!playerHandler.IsValidPlayer(action.GetPlayerID()))
			return false;
		unitLoader->ParseAndExecuteGiveUnitsCommand(CSimpleParser::Tokenize(action.GetArgs(), 0), playerHandler.Player(action.GetPlayerID())->team);
		return true;
	}
};


class BaseDestroyActionExecutor : public ISyncedActionExecutor {
public:
	BaseDestroyActionExecutor(const std::string& command, const std::string& description, bool runDeathScript)
		: ISyncedActionExecutor(command, description, true), runDeathScript(runDeathScript) {}

	bool Execute(const SyncedAction& action) const {
		const std::vector<std::string>& args = CSimpleParser::Tokenize(action.GetArgs(), 0);
		if (args.size() == 0) {
			LOG_L(L_WARNING, "not enough arguments (\"/%s <unitID:int...>\")", this->GetCommand().c_str());
			return false;
		}

		LOG("[%s] unitIDs: %s", this->GetCommand().c_str(), action.GetArgs().c_str());
		for (const auto& it : args) {
			int unitId = StringToInt<int>(it);
			CUnit *unit = unitHandler.GetUnit(unitId);

			if (unit != nullptr) {
				unit->KillUnit(nullptr, false, !this->runDeathScript, -CSolidObject::DAMAGE_KILLED_CHEAT);
			} else {
				LOG("[%s] Wrong unitID: %i", this->GetCommand().c_str(), unitId);
			}
		}

		return true;
	}
private:
	bool runDeathScript;
};

class DestroyActionExecutor : public BaseDestroyActionExecutor {
public:
	DestroyActionExecutor() : BaseDestroyActionExecutor("Destroy", "Destroys one or multiple units by unitID immediately", true) {}
};


class RemoveActionExecutor : public BaseDestroyActionExecutor {
public:
	RemoveActionExecutor() : BaseDestroyActionExecutor("Remove", "Removes one or multiple units by unitID immediately, bypassing death sequence", false) {}
};


class NoSpectatorChatActionExecutor : public ISyncedActionExecutor {
public:
	NoSpectatorChatActionExecutor() : ISyncedActionExecutor("NoSpectatorChat", "Enables/Disables spectators to use the chat") {
	}

	bool Execute(const SyncedAction& action) const final {
		InverseOrSetBool(game->noSpectatorChat, action.GetArgs());
		LogSystemStatus("Spectators chat", !game->noSpectatorChat);
		return true;
	}
};


class ReloadCobActionExecutor : public ISyncedActionExecutor {
public:
	ReloadCobActionExecutor() : ISyncedActionExecutor("ReloadCOB", "Reloads COB scripts", true) {
	}

	bool Execute(const SyncedAction& action) const final {
		game->ReloadCOB(action.GetArgs(), action.GetPlayerID());
		return true;
	}
};


class ReloadCegsActionExecutor : public ISyncedActionExecutor {
public:
	ReloadCegsActionExecutor() : ISyncedActionExecutor("ReloadCEGs", "Reloads CEG scripts", true) {
	}

	bool Execute(const SyncedAction& action) const final {
		explGenHandler.ReloadGenerators(action.GetArgs());
		return true;
	}
};


class DevLuaActionExecutor : public ISyncedActionExecutor {
public:
	DevLuaActionExecutor() : ISyncedActionExecutor(
		"DevLua",
		"Enables/Disables Lua dev-mode (can cause desyncs if enabled)",
		true
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		bool devMode = CLuaHandle::GetDevMode();
		InverseOrSetBool(devMode, action.GetArgs());
		CLuaHandle::SetDevMode(devMode);
		LogSystemStatus("Lua dev-mode (can cause desyncs if enabled)", devMode);
		return true;
	}
};


class EditDefsActionExecutor : public ISyncedActionExecutor {
public:
	EditDefsActionExecutor() : ISyncedActionExecutor("EditDefs",
			"Allows/Disallows editing of unit-, feature- and weapon-defs"
			" through Lua", true) {}

	bool Execute(const SyncedAction& action) const final {
		InverseOrSetBool(gs->editDefsEnabled, action.GetArgs());
		LogSystemStatus("Unit-, Feature- & Weapon-Def editing", gs->editDefsEnabled);
		return true;
	}
};



template<class LuaSyncedHandler> static void ExecuteSyncedLuaAction(
	LuaSyncedHandler*& handler,
	const SyncedAction& action,
	const char* luaName
) {
	const std::string& cmd = action.GetCmd();
	const std::string& arg = action.GetArgs();

	const char* msgs[] = {
		"synced %s scripts require cheating to %s",
		"cannot execute /%s %s before first gameframe",
		"%s %s callins %s",
	};

	if (arg == "reload" || arg == "enable") {
		if (!gs->cheatEnabled || gs->PreSimFrame()) {
			LOG_L(L_WARNING, msgs[gs->cheatEnabled], cmd.c_str(), arg.c_str());
			return;
		}

		if (handler != nullptr && arg == "enable") {
			LOG_L(L_WARNING, "%s is already loaded", luaName);
			return;
		}

		// NB: also returns true if new handler loads but is freed again due to invalidity
		LuaSyncedHandler::ReloadHandler();

		if (handler != nullptr) {
			LOG("%s loaded", luaName);
		} else {
			LOG_L(L_ERROR, "%s loading failed", luaName);
		}

		return;
	}

	if (arg == "disable") {
		if (!gs->cheatEnabled || gs->PreSimFrame()) {
			LOG_L(L_WARNING, msgs[gs->cheatEnabled], cmd.c_str(), arg.c_str());
			return;
		}

		LuaSyncedHandler::FreeHandler();

		LOG("%s disabled", luaName);
		return;
	}

	if (arg == "scallins" || arg == "ucallins") {
		CLuaHandle* slh = &handler->syncedLuaHandle;
		CLuaHandle* ulh = &handler->unsyncedLuaHandle;
		CLuaHandle*  lh = (arg[0] == 's')? slh: ulh;

		constexpr const char* types[] = {"unsynced",  "synced"};
		constexpr const char* modes[] = {"disabled", "enabled"};

		if (!gs->cheatEnabled || gs->PreSimFrame()) {
			LOG_L(L_WARNING, msgs[gs->cheatEnabled], cmd.c_str(), arg.c_str());
			return;
		}

		if (eventHandler.HasClient(lh)) {
			eventHandler.RemoveClient(lh);
		} else {
			eventHandler.AddClient(lh);
		}

		LOG(msgs[2], luaName, types[lh == slh], modes[eventHandler.HasClient(lh)]);
		return;
	}

	if (arg == "reloadunsynced") {
		bool success = handler->ReloadUnsynced();

		if (success) {
			LOG("unsynced %s loaded", luaName);
		} else {
			LOG_L(L_ERROR, "loading unsynced %s failed", luaName);
		}

		return;
	}

	// not a special arg, forward
	if (handler != nullptr) {
		handler->GotChatMsg(arg, action.GetPlayerID());
		return;
	}

	LOG("%s is not loaded", luaName);
}

class LuaRulesActionExecutor : public ISyncedActionExecutor {
public:
	LuaRulesActionExecutor() : ISyncedActionExecutor(
		"LuaRules",
		"Allows reloading or disabling LuaRules, and"
		" to send a chat message to LuaRules scripts"
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		// NOTE:
		//   previously only the host player (ID == 0) was allowed to issue these actions
		//   prior to some server changes they worked even in demos with that restriction,
		//   but this is no longer the case so we now let any player execute them (for MP
		//   it does not matter who does so since they are not meant to be used there ITFP
		//   and no less sync-safe)
		ExecuteSyncedLuaAction<CLuaRules>(luaRules, action, "LuaRules");
		return true;
	}
};


class LuaGaiaActionExecutor : public ISyncedActionExecutor {
public:
	LuaGaiaActionExecutor() : ISyncedActionExecutor(
		"LuaGaia",
		"Allows reloading or disabling LuaGaia, and"
		" to send a chat message to LuaGaia scripts"
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		if (!gs->useLuaGaia)
			return false;

		ExecuteSyncedLuaAction<CLuaGaia>(luaGaia, action, "LuaGaia");
		return true;
	}
};


class DesyncActionExecutor : public ISyncedActionExecutor {
public:
	DesyncActionExecutor() : ISyncedActionExecutor(
		"Desync",
		"Allows creating an artificial desync of the local "
		"client with the rest of the participating hosts",
		true
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		ASSERT_SYNCED(gu->myPlayerNum * 123.0f);
		ASSERT_SYNCED(gu->myPlayerNum * 123);
		ASSERT_SYNCED((short)(gu->myPlayerNum * 123 + 123));
		//ASSERT_SYNCED(float3(gu->myPlayerNum, gu->myPlayerNum, gu->myPlayerNum));

		// Command comming from the server won't match any of the client IDs.
		int actionPlayerID = (action.GetPlayerID()==SERVER_PLAYER) ? 0 : action.GetPlayerID();

		for (int i = unitHandler.MaxUnits() - 1; i >= 0; --i) {
			CUnit* u = unitHandler.GetUnit(i);

			if (u == nullptr)
				continue;

			if (actionPlayerID == gu->myPlayerNum) {
				++u->midPos.x; // and desync...
				++u->midPos.x;
			} else {
				// execute the same amount of flops on any other player,
				// but do not desync (it is a NOP)
				++u->midPos.x;
				--u->midPos.x;
			}
			break;
		}
		LOG_L(L_ERROR, "Desyncing in frame %d.", gs->frameNum);
		return true;
	}
};


class AtmActionExecutor : public ISyncedActionExecutor {
public:
	AtmActionExecutor() : ISyncedActionExecutor("Atm", "Gives the specified amount (default 1000) of each resource to the issuing player's team", true) {
	}

	bool Execute(const SyncedAction& action) const final {
		const std::string& args = action.GetArgs();

		const int team = playerHandler.Player(action.GetPlayerID())->team;
		const float amount = (args.empty())? 1000: std::max(0, std::atoi(args.c_str()));
		teamHandler.Team(team)->AddResources(amount);
		return true;
	}
};


class TakeActionExecutor : public ISyncedActionExecutor {
public:
	TakeActionExecutor() : ISyncedActionExecutor(
		"Take",
		"Transfers all units of allied teams without any "
		"active players to the team of the issuing player"
	) {
	}

	bool Execute(const SyncedAction& action) const final {
		const CPlayer* actionPlayer = playerHandler.Player(action.GetPlayerID());

		if (actionPlayer->spectator && !gs->cheatEnabled)
			return false;

		if (!game->playing)
			return true;

		for (int a = 0; a < teamHandler.ActiveTeams(); ++a) {
			if (!teamHandler.AlliedTeams(a, actionPlayer->team))
				continue;

			bool hasPlayer = false;

			for (int b = 0; b < playerHandler.ActivePlayers(); ++b) {
				const CPlayer* teamPlayer = playerHandler.Player(b);

				if (!teamPlayer->active) continue;
				if (teamPlayer->spectator) continue;
				if (teamPlayer->team != a) continue;

				hasPlayer = true;
				break;
			}

			if (!hasPlayer)
				teamHandler.Team(a)->GiveEverythingTo(actionPlayer->team);
		}

		return true;
	}
};


class SkipActionExecutor : public ISyncedActionExecutor {
public:
	SkipActionExecutor() : ISyncedActionExecutor("Skip", "Fast-forwards to a given frame, or stops fast-forwarding") {
	}

	bool Execute(const SyncedAction& action) const final {
		if (action.GetArgs().find_first_of("start") == 0) {
			std::istringstream buf(action.GetArgs().substr(6));
			int targetFrame;
			buf >> targetFrame;
			game->StartSkip(targetFrame);
			LOG("Skipping to frame %i", targetFrame);
		}
		else if (action.GetArgs() == "end") {
			game->EndSkip();
			LOG("Skip finished");
		} else {
			LOG_L(L_WARNING, "/%s: wrong syntax", GetCommand().c_str());
		}
		return true;
	}
};

class EngineGadgetsActionExecutor : public ISyncedActionExecutor {
public:
	EngineGadgetsActionExecutor() : ISyncedActionExecutor("EngineGadgets", "Enable engine gadgets", true) {
	}

	bool Execute(const SyncedAction& action) const final {
		auto args = CSimpleParser::Tokenize(action.GetArgs());

		if (args.size() == 0) {
			LOG_L(L_WARNING, "/%s: need to specify on/off", GetCommand().c_str());
			return false;
		}
		bool enabled = StringToBool(args[0]);

		gadgetHandler.EnableAll(enabled);

		LogSystemStatus("engine gadgets", enabled);
		return true;
	}
};

} // namespace (unnamed)




void SyncedGameCommands::AddDefaultActionExecutors()
{
	if (!actionExecutors.empty())
		return;

	AddActionExecutor(AllocActionExecutor<CheatActionExecutor>());
	AddActionExecutor(AllocActionExecutor<NoHelpActionExecutor>());
	AddActionExecutor(AllocActionExecutor<NoSpecDrawActionExecutor>());
	AddActionExecutor(AllocActionExecutor<GodModeActionExecutor>());
	AddActionExecutor(AllocActionExecutor<GlobalLosActionExecutor>());
	AddActionExecutor(AllocActionExecutor<NoCostActionExecutor>());
	AddActionExecutor(AllocActionExecutor<GiveActionExecutor>());
	AddActionExecutor(AllocActionExecutor<DestroyActionExecutor>());
	AddActionExecutor(AllocActionExecutor<RemoveActionExecutor>());
	AddActionExecutor(AllocActionExecutor<NoSpectatorChatActionExecutor>());
	AddActionExecutor(AllocActionExecutor<ReloadCobActionExecutor>());
	AddActionExecutor(AllocActionExecutor<ReloadCegsActionExecutor>());
	AddActionExecutor(AllocActionExecutor<EngineGadgetsActionExecutor>());
	AddActionExecutor(AllocActionExecutor<DevLuaActionExecutor>());
	AddActionExecutor(AllocActionExecutor<EditDefsActionExecutor>());
	AddActionExecutor(AllocActionExecutor<LuaRulesActionExecutor>());
	AddActionExecutor(AllocActionExecutor<LuaGaiaActionExecutor>());
	AddActionExecutor(AllocActionExecutor<DesyncActionExecutor>());
	AddActionExecutor(AllocActionExecutor<AtmActionExecutor>());
	if (modInfo.allowTake)
		AddActionExecutor(AllocActionExecutor<TakeActionExecutor>());

	AddActionExecutor(AllocActionExecutor<SkipActionExecutor>());
}


alignas(SyncedGameCommands) static std::byte sgcSingletonMem[sizeof(SyncedGameCommands)];

void SyncedGameCommands::CreateInstance() {
	SyncedGameCommands*& singleton = GetInstance();

	if (singleton != nullptr)
		return;

	singleton = new (sgcSingletonMem) SyncedGameCommands();
}

void SyncedGameCommands::DestroyInstance(bool reload) {
	SyncedGameCommands*& singleton = GetInstance();

	// executors should be inaccessible in between reloads
	if (reload)
		return;

	spring::SafeDestruct(singleton);
	std::memset(sgcSingletonMem, 0, sizeof(sgcSingletonMem));
}

