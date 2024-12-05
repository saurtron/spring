/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "CGadget.h"
#include "System/EventHandler.h"


CGadget::CGadget(const char *name, int priority, bool synced)
	: CEventClient(name, priority, synced)
	, enabled(false)
{
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
	enabled = true;
}

void CGadget::Enable()
{
	if (!enabled) {
		eventHandler.AddClient(this);
		enabled = true;
	}
}

void CGadget::Disable()
{
	if (enabled) {
		eventHandler.RemoveClient(this);
		enabled = false;
	}
}
