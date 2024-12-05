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
	virtual CGadget* Create() {return nullptr;};
};

template <typename T>
class Factory : public CGadgetFactory
{
	public:
		Factory(const char* gname, int gpriority=19991) {
			name = gname;
			priority = gpriority;
		}
		CGadget* Create() override {
			return new T(name, priority);
		}
		const char *GetName() override {
			return name;
		}
		const char *name;
		int priority;
};

class CGadget : public CEventClient {
public:
	CGadget(const char *name, int priority);
	virtual ~CGadget() {}

	void Disable();
};

#endif // GADGET_H
