/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "BaseBuilderBehaviourAI.h"
#include "ExternalAI/EngineOutHandler.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Game/GameHelper.h"
#include "Game/GlobalUnsynced.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/WaitCommandsAI.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Units/BuildInfo.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitLoader.h"
#include "Sim/Units/UnitDefHandler.h"
//#include "Sim/Units/UnitTypes/Factory.h"
#include "Sim/Units/Behaviour/FactoryBehaviour.h"
#include "System/Log/ILog.h"
#include "System/creg/STL_Map.h"
#include "System/StringUtil.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"

#include "System/Misc/TracyDefs.h"

template CBaseBuilderBehaviourAI* CCommandAI::GetBehaviourAI<CBaseBuilderBehaviourAI>() const;

CR_BIND_DERIVED(CBaseBuilderBehaviourAI, CBehaviourAI , )

CR_REG_METADATA(CBaseBuilderBehaviourAI, (
	CR_PREALLOC(GetPreallocContainer)
))

std::string CBaseBuilderBehaviourAI::GetUnitDefBuildOptionToolTip(const UnitDef* ud, bool disabled) {
	std::string tooltip;

	if (disabled) {
		tooltip = "\xff\xff\x22\x22" "DISABLED: " "\xff\xff\xff\xff";
	} else {
		tooltip = "Build: ";
	}

	tooltip += (ud->humanName + " - " + ud->tooltip);
	tooltip += ("\nHealth "      + FloatToString(ud->health,      "%.0f"));
	tooltip += ("\nMetal cost "  + FloatToString(ud->cost.metal,  "%.0f"));
	tooltip += ("\nEnergy cost " + FloatToString(ud->cost.energy, "%.0f"));
	tooltip += ("\nBuild time "  + FloatToString(ud->buildTime,   "%.0f"));

	return tooltip;
}



CBaseBuilderBehaviourAI::CBaseBuilderBehaviourAI(): CBehaviourAI()
{
}


CBaseBuilderBehaviourAI::CBaseBuilderBehaviourAI(CUnit* owner): CBehaviourAI(owner)
{
}

