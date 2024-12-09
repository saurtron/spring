--  file:    rml.lua
--  brief:   RmlUi Setup
--  author:  lov + ChrisFloofyKitsune
--
--  Copyright (C) 2024.
--  Licensed under the terms of the GNU GPL, v2 or later.

if (RmlGuard or not RmlUi) then
	return
end
RmlGuard = true

-- Load fonts
RmlUi.LoadFontFace("Fonts/FreeMonoBold.ttf", true)

-- Mouse Cursor Aliases
--[[
	These let standard CSS cursor names be used when doing styling.
	If a cursor set via RCSS does not have an alias, it is unchanged.
	CSS cursor list: https://developer.mozilla.org/en-US/docs/Web/CSS/cursor
	RmlUi documentation: https://mikke89.github.io/RmlUiDoc/pages/rcss/user_interface.html#cursor
]]

-- when "cursor: normal" is set via RCSS, "cursornormal" will be sent to the engine
RmlUi.SetMouseCursorAlias("default", 'cursornormal')
-- for command cursors, use the command name
-- RmlUi.SetMouseCursorAlias("pointer", 'Move')
