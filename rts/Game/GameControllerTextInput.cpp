/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cassert>
#include <SDL2/SDL_keyboard.h>

#include "GameControllerTextInput.h"
#include "Action.h"
#include "ConsoleHistory.h"
#include "Game.h"
#include "InMapDraw.h"
#include "WordCompletion.h"
#include "Game/UI/GuiHandler.h"
#include "Game/UI/KeyCodes.h"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "System/SpringMath.h"
#include "System/StringHash.h"
#include "System/StringUtil.h"
#include "System/Log/ILog.h"
#include "System/Platform/Clipboard.h"

#include "System/Misc/TracyDefs.h"

static constexpr float4 const  defColor(1.0f, 1.0f, 1.0f, 1.0f);
static constexpr float4 const allyColor(0.5f, 1.0f, 0.5f, 1.0f);
static constexpr float4 const specColor(1.0f, 1.0f, 0.5f, 1.0f);

GameControllerTextInput gameTextInput;

void GameControllerTextInput::ViewResize() {
	RECOIL_DETAILED_TRACY_ZONE;
	// inputTextSizeX and inputTextSizeY aren't actually used by anything
	// so we assume those values are bad, and we could simply ignore the X component
	// that said, the width of the SDL TextInputRect doesn't seem to matter either, as
	// it tends to be printed in groups of 10 characters.
	textEditingWindow.x =        inputTextPosX  * globalRendering->viewSizeX;
	textEditingWindow.y = (1.0 - inputTextPosY) * globalRendering->viewSizeY;
	// textEditingWindow.w = globalRendering->viewSizeX - textEditingWindow.y; // remaining width
	textEditingWindow.w = inputTextSizeX * globalRendering->viewSizeX;
	textEditingWindow.h = inputTextSizeY * globalRendering->viewSizeY;

	SDL_SetTextInputRect(&textEditingWindow);
}

void GameControllerTextInput::Draw() {
	RECOIL_DETAILED_TRACY_ZONE;
	if (!userWriting)
		return;

	const float fontScale = 1.0f; // TODO: make configurable again
	const float fontSize = fontScale * font->GetSize();

	const std::string userString = userPrompt + userInput + editText;

	if (drawTextCaret) {
		int curCaretIdx = writingPos + editingPos; // advanced by GetNextChar
		int prvCaretIdx = curCaretIdx;

		char32_t c = utf8::GetNextChar(userInput + editText, curCaretIdx);

		// make caret always visible
		if (c == 0)
			c = ' ';


		const float caretRelPos = fontSize * font->GetTextWidth(userString.substr(0, userPrompt.length() + prvCaretIdx)) * globalRendering->pixelX;
		const float caretHeight = fontSize * font->GetLineHeight() * globalRendering->pixelY;
		const float caretWidth  = fontSize * font->GetCharacterWidth(c) * globalRendering->pixelX;

		const float caretScrPos = inputTextPosX + caretRelPos;
		const float caretIllum = 0.5f * (1.0f + fastmath::sin(spring_now().toMilliSecsf() * 0.015f));

		glDisable(GL_TEXTURE_2D);
		glColor4f(caretIllum, caretIllum, caretIllum, 0.75f);
		glRectf(caretScrPos, inputTextPosY, caretScrPos + caretWidth, inputTextPosY + caretHeight);
		glEnable(GL_TEXTURE_2D);
	}

	// setup the color
	const float4* textColor = &defColor;

	if (userInput.length() < 2) {
		textColor = &defColor;
	} else if ((userInput.find_first_of("aA") == 0) && (userInput[1] == ':')) {
		textColor = &allyColor;
	} else if ((userInput.find_first_of("sS") == 0) && (userInput[1] == ':')) {
		textColor = &specColor;
	} else {
		textColor = &defColor;
	}

	// draw the text
	font->Begin();
	font->SetColors(textColor, nullptr);
	font->glPrint(inputTextPosX, inputTextPosY, fontSize, FONT_DESCENDER | (FONT_OUTLINE * guihandler->GetOutlineFonts()) | FONT_NORM, userString);
	font->End();
	font->SetColors(nullptr, nullptr);
}


int GameControllerTextInput::SetInputText(const std::string& utf8Text) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (!userWriting)
		return 0;

	const std::string text = ignoreNextChar ? utf8Text.substr(utf8::NextChar(utf8Text, 0)) : utf8Text;

	userInput.insert(writingPos = std::clamp <int> (writingPos, 0, userInput.length()), text);
	editText = "";

	writingPos += text.length();
	editingPos = 0;
	return 0;
}


bool GameControllerTextInput::SendPromptInput() {
	RECOIL_DETAILED_TRACY_ZONE;
	if (userWriting)
		return false;
	if (!userChatting)
		return false;

	std::string msg = userInput;
	std::string pfx;

	if ((userInput.find_first_of("aAsS") == 0) && (userInput[1] == ':')) {
		pfx = userInput.substr(0, 2);
		msg = userInput.substr(2);
	}
	if ((msg[0] == '/') && (msg[1] == '/'))
		msg = msg.substr(1);

	userInput = pfx + msg;
	return true;
}

bool GameControllerTextInput::SendLabelInput() {
	RECOIL_DETAILED_TRACY_ZONE;
	if (userWriting)
		return false;

	if (userInput.size() > 200) {
		// avoid troubles with long lines
		userInput = userInput.substr(0, 200);
		writingPos = (int)userInput.length();
	}

	inMapDrawer->SendWaitingInput(userInput);
	return true;
}


void GameControllerTextInput::PasteClipboard() {
	RECOIL_DETAILED_TRACY_ZONE;
	const CClipboard clipboard;
	const std::string text = clipboard.GetContents();

	userInput.insert(writingPos, text);
	writingPos += text.length();
}


bool GameControllerTextInput::HandleChatCommand(int key, const std::string& command) {
	RECOIL_DETAILED_TRACY_ZONE;
	switch (hashString(command.c_str())) {
		case hashString("chatswitchall"): {
			if ((userInput.find_first_of("aAsS") == 0) && (userInput[1] == ':')) {
				userInput = userInput.substr(2);
				writingPos = std::max(0, writingPos - 2);
			}

			userInputPrefix = "";
			return true;
		} break;

		case hashString("chatswitchally"): {
			if ((userInput.find_first_of("aA") == 0) && (userInput[1] == ':')) {
				// already in ally chat, toggle it off
				userInput = userInput.substr(2);
				userInputPrefix = "";
				writingPos = std::max(0, writingPos - 2);
			}
			else if ((userInput.find_first_of("sS") == 0) && (userInput[1] == ':')) {
				// already in spec chat, switch to ally chat
				userInput[0] = 'a';
				userInputPrefix = "a:";
			} else {
				userInput = "a:" + userInput;
				userInputPrefix = "a:";
				writingPos += 2;
			}

			return true;
		} break;

		case hashString("chatswitchspec"): {
			if ((userInput.find_first_of("sS") == 0) && (userInput[1] == ':')) {
				// already in spec chat, toggle it off
				userInput = userInput.substr(2);
				userInputPrefix = "";
				writingPos = std::max(0, writingPos - 2);
			}
			else if ((userInput.find_first_of("aA") == 0) && (userInput[1] == ':')) {
				// already in ally chat, switch to spec chat
				userInput[0] = 's';
				userInputPrefix = "s:";
			} else {
				userInput = "s:" + userInput;
				userInputPrefix = "s:";
				writingPos += 2;
			}

			return true;
		} break;
	}

	// unknown chat-command
	return false;
}


// can only be called by CGame (via ConsumePressedKey)
bool GameControllerTextInput::HandleEditCommand(int keyCode, int scanCode, const std::string& command) {
	switch (hashString(command.c_str())) {
		case hashString("edit_return"): {
			userWriting = false;
			writingPos = 0;

			if (userChatting) {
				std::string cmd;

				if ((userInput.find_first_of("aAsS") == 0) && (userInput[1] == ':')) {
					cmd = userInput.substr(2);
				} else {
					cmd = userInput;
				}

				if (game->ProcessCommandText(keyCode, scanCode, cmd)) {
					// execute an action
					gameConsoleHistory.AddLine(cmd);
					ClearInput();
				}
			}

			SDL_StopTextInput();
			return true;
		} break;

		case hashString("edit_escape"): {
			if (userChatting || inMapDrawer->IsWantLabel()) {
				if (userChatting)
					gameConsoleHistory.AddLine(userInput);

				userWriting = false;

				ClearInput();
				inMapDrawer->SetWantLabel(false);
			}

			SDL_StopTextInput();
			return true;
		} break;

		case hashString("edit_complete"): {
			std::string head = userInput.substr(0, writingPos);
			std::string tail = userInput.substr(writingPos);

			// NB: sets head to the first partial match
			const std::vector<std::string>& partials = wordCompletion.Complete(head);

			userInput = head + tail;
			writingPos = head.length();

			if (!partials.empty()) {
				std::string msg;
				for (const std::string& match: partials) {
					msg.append("  ");
					msg.append(match);
				}
				LOG("%s", msg.c_str());
			}

			return true;
		} break;


		case hashString("edit_backspace"): {
			if (!userInput.empty() && (writingPos > 0)) {
				const int prev = utf8::PrevChar(userInput, writingPos);
				userInput.erase(prev, writingPos - prev);
				writingPos = prev;
			}

			return true;
		} break;

		case hashString("edit_delete"): {
			if (!userInput.empty() && (writingPos < (int)userInput.size()))
				userInput.erase(writingPos, utf8::CharLen(userInput, writingPos));

			return true;
		} break;


		case hashString("edit_home"): {
			writingPos = 0;
			return true;
		} break;

		case hashString("edit_end"): {
			writingPos = userInput.length();
			return true;
		} break;


		case hashString("edit_prev_char"): {
			writingPos = utf8::PrevChar(userInput, writingPos);
			return true;
		} break;

		case hashString("edit_next_char"): {
			writingPos = utf8::NextChar(userInput, writingPos);
			return true;
		} break;


		case hashString("edit_prev_word"): {
			//TODO: does not seem to work correctly with utf-8
			// prev word
			const char* s = userInput.c_str();
			int p = writingPos;
			while ((p > 0) && !isalnum(s[p - 1])) { p--; }
			while ((p > 0) &&  isalnum(s[p - 1])) { p--; }
			writingPos = p;
			return true;
		} break;

		case hashString("edit_next_word"): {
			// TODO: does not seem to work correctly with utf-8
			const size_t len = userInput.length();
			const char* s = userInput.c_str();
			size_t p = writingPos;
			while ((p < len) && !isalnum(s[p])) { p++; }
			while ((p < len) &&  isalnum(s[p])) { p++; }
			writingPos = p;
			return true;
		} break;


		case hashString("edit_prev_line"): {
			if (userChatting) {
				userInput = gameConsoleHistory.PrevLine(userInput);
				writingPos = (int)userInput.length();
				return true;
			}
		} break;

		case hashString("edit_next_line"): {
			if (userChatting) {
				userInput = gameConsoleHistory.NextLine(userInput);
				writingPos = (int)userInput.length();
				return true;
			}
		} break;
	}

	// unknown edit-command
	return false;
}


bool GameControllerTextInput::HandlePasteCommand(int key, const std::string& rawLine) {
	RECOIL_DETAILED_TRACY_ZONE;
	// we cannot use extra commands because tokenization strips multiple
	// spaces or even trailing spaces, the text should be copied verbatim
	const std::string pastecommand = "pastetext ";

	if (rawLine.length() > pastecommand.length()) {
		userInput.insert(writingPos, rawLine.substr(pastecommand.length(), rawLine.length() - pastecommand.length()));
		writingPos += (rawLine.length() - pastecommand.length());
	} else {
		PasteClipboard();
	}

	return true;
}

bool GameControllerTextInput::CheckHandlePasteCommand(const std::string& rawLine) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (!userWriting)
		return false;

	return (HandlePasteCommand(0, rawLine));
}


bool GameControllerTextInput::ProcessKeyPressAction(int keyCode, int scanCode, const Action& action) {
	RECOIL_DETAILED_TRACY_ZONE;
	assert(userWriting);

	if (action.command == "pastetext")
		return (HandlePasteCommand(keyCode, action.rawline));

	if (action.command.find("edit_") == 0)
		return (HandleEditCommand(keyCode, scanCode, action.command));

	if (action.command.find("chatswitch") == 0)
		return (HandleChatCommand(keyCode, action.command));

	return false;
}


bool GameControllerTextInput::ConsumePressedKey(int keyCode, int scanCode, const std::vector<Action>& actions) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (!userWriting)
		return false;

	for (const Action& action: actions) {
		if (!ProcessKeyPressAction(keyCode, scanCode, action))
			continue;

		// the key was used, ignore it (ex: alt+a)
		ignoreNextChar = keyCodes.IsPrintable(keyCode);
		break;
	}

	return true;
}


bool GameControllerTextInput::ConsumeReleasedKey(int keyCode, int scanCode) const {
	RECOIL_DETAILED_TRACY_ZONE;
	if (!userWriting)
		return false;
	if (keyCodes.IsPrintable(keyCode))
		return true;

	return (keyCode == SDLK_RETURN || keyCode == SDLK_BACKSPACE || keyCode == SDLK_DELETE || keyCode == SDLK_HOME || keyCode == SDLK_END || keyCode == SDLK_RIGHT || keyCode == SDLK_LEFT);
}

