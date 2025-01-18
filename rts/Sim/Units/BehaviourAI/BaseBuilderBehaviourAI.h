/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _BASE_BUILDER_BEHAVIOUR_AI_H_
#define _BASE_BUILDER_BEHAVIOUR_AI_H_

#include "BehaviourAI.h"
#include "Sim/Units/CommandAI/CommandQueue.h"

#include <string>
//#include "System/UnorderedMap.hpp"

class CUnit;
struct UnitDef;

class CBaseBuilderBehaviourAI : public CBehaviourAI
{
public:
	CR_DECLARE(CBaseBuilderBehaviourAI)

	CBaseBuilderBehaviourAI(CUnit* owner);
	CBaseBuilderBehaviourAI();
	virtual ~CBaseBuilderBehaviourAI() {};
	static std::string GetUnitDefBuildOptionToolTip(const UnitDef* ud, bool disabled);
	virtual int GetDefaultCmd(const CUnit* pointed, const CFeature* feature) { throw std::runtime_error("not implemented"); return 0; };
};

#endif // _BASE_BUILDER_BEHAVIOUR_AI_H_
