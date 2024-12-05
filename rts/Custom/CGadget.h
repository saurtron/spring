/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GADGET_H
#define GADGET_H

#include "System/EventClient.h"

class CGadget;

class CGadgetFactory
{
	public:
	virtual ~CGadgetFactory() {}
	virtual const char* GetName() {return "";}
	virtual CGadget* Create(int priority=0) {return nullptr;};
};

template <typename T>
class Factory : public CGadgetFactory
{
	public:
		Factory(const char* gname, int gpriority=19991, bool gsynced=false) {
			name = gname;
			priority = gpriority;
			synced = gsynced;
		}
		CGadget* Create(int gpriority=0) override {

			return new T(name, (gpriority > 0) ? gpriority : priority, synced);
		}
		const char *GetName() override {
			return name;
		}
		const char *name;
		int priority;
		int synced;
};

class CGadget : public CEventClient {
public:
	CGadget(const char *name, int priority, bool synced);
	virtual ~CGadget() {}

	void Enable();
	void Disable();
	bool enabled;
};

#endif // GADGET_H
