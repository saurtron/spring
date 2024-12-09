/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "InfoConsole.h"
#include "GuiHandler.h"
#include "Rendering/Fonts/glFont.h"
#include "System/EventHandler.h"
#include "System/SafeUtil.h"
#include "System/Config/ConfigHandler.h"
#include "System/Log/LogSinkHandler.h"

#include "System/Misc/TracyDefs.h"

static constexpr int IC_BORDER = 7;

CONFIG(int, InfoMessageTime).defaultValue(10).description("Time until old messages disappear from the ingame console.");
CONFIG(std::string, InfoConsoleGeometry).defaultValue("0.26 0.96 0.41 0.205");

CInfoConsole* infoConsole = nullptr;

alignas(CInfoConsole) static std::byte infoConsoleMem[sizeof(CInfoConsole)];




void CInfoConsole::InitStatic() {
	RECOIL_DETAILED_TRACY_ZONE;
	assert(infoConsole == nullptr);
	infoConsole = new (infoConsoleMem) CInfoConsole();
}

void CInfoConsole::KillStatic() {
	RECOIL_DETAILED_TRACY_ZONE;
	assert(infoConsole != nullptr);
	spring::SafeDestruct(infoConsole);
	std::fill(std::begin(infoConsoleMem), std::end(infoConsoleMem), std::byte{0});
}


void CInfoConsole::Init()
{
	RECOIL_DETAILED_TRACY_ZONE;
	maxLines = 1;
	newLines = 0;

	msgPosIndx = 0;
	numPosMsgs = 0;

	rawId = 0;
	lifetime = configHandler->GetInt("InfoMessageTime");

	xpos = 0.0f;
	ypos = 0.0f;
	width = 0.0f;
	height = 0.0f;

	fontScale = 1.0f;
	fontSize = fontScale * smallFont->GetSize();;

	const std::string geo = configHandler->GetString("InfoConsoleGeometry");
	const int vars = sscanf(geo.c_str(), "%f %f %f %f",
	                        &xpos, &ypos, &width, &height);
	if (vars != 4) {
		xpos = 0.26f;
		ypos = 0.96f;
		width = 0.41f;
		height = 0.205f;
	}

	enabled = (width != 0.0f && height != 0.0f);
	inited = true;


	logSinkHandler.AddSink(this);
	eventHandler.AddClient(this);

	rawLines.clear();
	infoLines.clear();
	tmpInfoLines.clear();

	lastMsgPositions.fill(ZeroVector);
}

void CInfoConsole::Kill()
{
	RECOIL_DETAILED_TRACY_ZONE;
	logSinkHandler.RemoveSink(this);
	eventHandler.RemoveClient(this);

	inited = false;
}


void CInfoConsole::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!enabled)
		return;
	if (smallFont == nullptr)
		return;

	// infoConsole exists before guiHandler does, but is never drawn during that period
	assert(guihandler != nullptr);

	{
		// make copies s.t. mutex is not locked during glPrint calls
		std::lock_guard<decltype(infoConsoleMutex)> scoped_lock(infoConsoleMutex);

		if (infoLines.empty())
			return;

		drawInfoLines.clear();
		drawInfoLines.insert(drawInfoLines.cend(), infoLines.cbegin(), infoLines.cbegin() + std::min(infoLines.size(), maxLines)); 
	}

	smallFont->Begin();
	smallFont->SetColors(); // default

	const uint32_t fontOptions = FONT_NORM | FONT_OUTLINE;
	const float fontHeight = fontSize * smallFont->GetLineHeight() * globalRendering->pixelY;

	float curX = xpos + IC_BORDER * globalRendering->pixelX;
	float curY = ypos - IC_BORDER * globalRendering->pixelY;

	for (size_t i = 0, n = drawInfoLines.size(); i < n; ++i) {
		smallFont->glPrint(curX, curY -= fontHeight, fontSize, fontOptions, drawInfoLines[i].text);
	}

	smallFont->End();
}


void CInfoConsole::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::lock_guard<decltype(infoConsoleMutex)> scoped_lock(infoConsoleMutex);

	// pop old messages after timeout
	while (infoLines.size() > 0 && infoLines.front().timeout <= spring_gettime())
		infoLines.pop_front();

	if (smallFont == nullptr)
		return;

	for (const auto& til : tmpInfoLines) {
		if (til.timeout <= spring_gettime())
			continue;

		const std::string wrappedText = smallFont->Wrap(til.text, fontSize, (width * globalRendering->viewSizeX) - (2 * IC_BORDER));
		const auto& newLines = smallFont->SplitIntoLines(toustring(wrappedText));
		for (const auto& nl : newLines)
			infoLines.emplace_back(nl, til.timeout);
	}
	tmpInfoLines.clear();

	if (infoLines.empty())
		return;

	// if we have more lines then we can show, remove the oldest one,
	// and make sure the others are shown long enough
	const float  maxHeight = (height * globalRendering->viewSizeY) - (IC_BORDER * 2);
	const float fontHeight = smallFont->GetLineHeight();

	// height=0 will likely be the case on HEADLESS only
	maxLines = (fontHeight > 0.0f)? math::floor(maxHeight / (fontSize * fontHeight)): 1;

	for (size_t i = infoLines.size(); i > maxLines; i--) {
		infoLines.pop_front();
	}
}

void CInfoConsole::PushNewLinesToEventHandler()
{
	RECOIL_DETAILED_TRACY_ZONE;
	{
		std::lock_guard<decltype(infoConsoleMutex)> scoped_lock(infoConsoleMutex);

		if (newLines == 0)
			return;

		const size_t lineCount = rawLines.size();
		const size_t startLine = lineCount - std::min(lineCount, newLines);

		tmpLines.clear();
		tmpLines.reserve(lineCount - startLine);

		for (size_t i = startLine; i < lineCount; i++) {
			tmpLines.push_back(rawLines[i]);
		}

		newLines = 0;
	}

	for (const RawLine& rawLine: tmpLines) {
		eventHandler.AddConsoleLine(rawLine.text, rawLine.section, rawLine.level);
	}
}


size_t CInfoConsole::GetRawLines(std::vector<RawLine>& lines)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::lock_guard<decltype(infoConsoleMutex)> scoped_lock(infoConsoleMutex);

	const size_t numNewLines = newLines;

	lines.clear();
	lines.assign(rawLines.begin(), rawLines.end());

	return (newLines = 0, numNewLines);
}


void CInfoConsole::RecordLogMessage(int level, const std::string& section, const std::string& message)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::lock_guard<decltype(infoConsoleMutex)> scoped_lock(infoConsoleMutex);

	if (section == prvSection && message == prvMessage)
		return;

	newLines += (newLines < maxRawLines);

	if (rawLines.size() > maxRawLines)
		rawLines.pop_front();

	rawLines.emplace_back(prvMessage = message, prvSection = section, level, rawId++);

	if (smallFont == nullptr)
		return;

	tmpInfoLines.emplace_back(message, spring_gettime() + spring_secs(lifetime));
}


void CInfoConsole::LastMessagePosition(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// reset index to head when a new msg comes in
	msgPosIndx  = numPosMsgs % lastMsgPositions.size();
	numPosMsgs += 1;

	lastMsgPositions[msgPosIndx] = pos;
}

const float3& CInfoConsole::GetMsgPos(const float3& defaultPos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (numPosMsgs == 0)
		return defaultPos;

	const float3& p = lastMsgPositions[msgPosIndx];

	// cycle to previous position
	msgPosIndx += (std::min(numPosMsgs, lastMsgPositions.size()) - 1);
	msgPosIndx %= std::min(numPosMsgs, lastMsgPositions.size());

	return p;
}

