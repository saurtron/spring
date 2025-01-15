/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _FACTORY_BEHAVIOUR_AI_H_
#define _FACTORY_BEHAVIOUR_AI_H_

#include "BehaviourAI.h"
#include "Sim/Units/CommandAI/CommandQueue.h"

#include <string>
#include "System/UnorderedMap.hpp"

class CUnit;
class CFeature;
struct Command;

class CFactoryBehaviourAI : public CBehaviourAI
{
public:
	CR_DECLARE(CFactoryBehaviourAI)

	CFactoryBehaviourAI(CUnit* owner);
	CFactoryBehaviourAI();

	int GetDefaultCmd(const CUnit* pointed, const CFeature* feature);
	virtual bool SlowUpdate() override;

	bool GiveCommandReal(const Command& c, bool fromSynced = true) override;

	void InsertBuildCommand(CCommandQueue::iterator& it, const Command& c);
	bool RemoveBuildCommand(CCommandQueue::iterator& it);

	void DecreaseQueueCount(const Command& c, int& buildOption);
	void FactoryFinishBuild(const Command& command);
	void ExecuteStop(Command& c);

	CCommandQueue newUnitCommands;

	spring::unordered_map<int, int> buildOptions;

private:
	void UpdateIconName(int id, const int& numQueued);
private:
	void FinishCommand();
};

#endif // _FACTORY_AI_H_
