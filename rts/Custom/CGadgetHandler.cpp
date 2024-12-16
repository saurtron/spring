/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cassert>

#include "CGadgetHandler.h"

#include "System/Log/ILog.h"
#include "BuilderRangeCheck.h"
#include "GuardRemove.h"
#include "UnitImmobileBuilder.h"
#include "RemoveStop.h"
#include "OnlyTargetCategory.h"

CGadgetHandler gadgetHandler;

CGadgetHandler::CGadgetHandler()
{
	AddFactory(new Factory<BuilderRangeCheck>("BuilderRangeCheck", 19991, false));
	AddFactory(new Factory<GuardRemove>("GuardRemove", 19992, false));
	AddFactory(new Factory<UnitImmobileBuilder>("UnitImmobileBuilder", 19990, false));
	AddFactory(new Factory<RemoveStop>("RemoveStop", 19993, true));
	AddFactory(new Factory<OnlyTargetCategory>("OnlyTargetCategory", 19993, true));
}

void CGadgetHandler::EnableAll(bool enable)
{
	LOG_L(L_WARNING, "[%s] enabling all engine gadgets!", __func__);
	for(auto iter: gadgetFactories) {
		if (enable && !IsGadgetEnabled(iter.first.c_str())) {
			CGadgetFactory *fact = iter.second;
			gadgets[iter.first] = fact->Create();
			gadgets[iter.first]->EnableEvents();
			LOG_L(L_WARNING, "[%s] enabled %s", __func__, iter.first.c_str());
		}
		else if (!enable && IsGadgetEnabled(iter.first.c_str())) {
			CGadgetFactory *fact = iter.second;
			CGadget *gadget = gadgets[iter.first];
			gadgets.erase(iter.first);
			delete gadget;
		}
	}
}

void CGadgetHandler::AddFactory(CGadgetFactory* fact)
{
	gadgetFactories[fact->GetName()] = fact;
}

bool CGadgetHandler::HasGadget(const char* name)
{
	return gadgetFactories.contains(name);
}

bool CGadgetHandler::IsGadgetEnabled(const char* name)
{
	return gadgets.contains(name) && gadgets[name]->IsEnabled();
}

bool CGadgetHandler::EnableGadget(const char* name, bool enable, int priority)
{
	if (enable && !IsGadgetEnabled(name) ) {
		CGadgetFactory *fact = gadgetFactories[name];
		if (fact) {
			gadgets[name] = fact->Create(priority);
			gadgets[name]->EnableEvents();
			return true;
		} else {
			LOG_L(L_ERROR, "[%s] no gadget factory for %s", __func__, name);
		}
	} else if (!enable && IsGadgetEnabled(name)) {
		CGadget *gadget = gadgets[name];
		gadgets.erase(name);
		delete gadget;
		return true;
	}
	LOG_L(L_DEBUG, "[%s] bad enable state for %s", __func__, name);
	return false;
}

