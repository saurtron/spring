/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GADGETHANDLER_H
#define GADGETHANDLER_H

#include "System/UnorderedMap.hpp"

#include "CGadget.h"

class CGadgetHandler
{
public:
	CGadgetHandler();
	bool IsGadgetEnabled(const char* name);
	bool EnableGadget(const char* name, bool enable, int priority=0);

	void EnableAll(bool enable);
	void AddFactory(CGadgetFactory* fact);

	/*std::vector<CGadget*> gadgets;
	std::vector<CGadgetFactory*> gadgetFactories;*/
	spring::unordered_map<std::string, CGadget*> gadgets;
	spring::unordered_map<std::string, CGadgetFactory*> gadgetFactories;
};

extern CGadgetHandler gadgetHandler;

#endif /* GADGETHANDLER_H */
