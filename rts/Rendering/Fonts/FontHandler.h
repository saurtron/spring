/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _FONT_HANDLER_H
#define _FONT_HANDLER_H

#include "System/Misc/NonCopyable.h"

class CFontHandler : public spring::noncopyable
{
public:
	CFontHandler();
	bool Init(bool fullInit);
	void Kill();

	bool disableOldColorIndicators = false;
	int maxFontTries = 0;
	int maxPinnedFonts = 0;
	bool allowColorFonts = 0;
	bool useFontConfigLib = false;
	bool searchSystemFonts = true;
	bool searchFontAttributes = true;
	bool searchApplySubstitutions = true;
};

extern CFontHandler fontHandler;


#endif // _FONT_HANDLER_H
