/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "CGadget.h"
#include "System/EventHandler.h"


CGadget::CGadget(const char *name, int priority)
	: CEventClient(name, priority, false)
{
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
}

void CGadget::Disable()
{
	eventHandler.RemoveClient(this);
}
