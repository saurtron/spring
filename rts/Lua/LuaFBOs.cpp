/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "LuaFBOs.h"

#include "LuaInclude.h"

#include "LuaHandle.h"
#include "LuaHashString.h"
#include "LuaUtils.h"

#include "LuaOpenGL.h"
#include "LuaRBOs.h"
#include "LuaTextures.h"

#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "fmt/format.h"

#include "System/Misc/TracyDefs.h"


/******************************************************************************
 * FBO
 * @module FBO
 * @see rts/Lua/LuaFBOs.cpp
******************************************************************************/

LuaFBOs::~LuaFBOs()
{
	RECOIL_DETAILED_TRACY_ZONE;
	for (const FBO* fbo: fbos) {
		glDeleteFramebuffersEXT(1, &fbo->id);
	}
}


/******************************************************************************/
/******************************************************************************/

bool LuaFBOs::PushEntries(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CreateMetatable(L);

	REGISTER_LUA_CFUNC(CreateFBO);
	REGISTER_LUA_CFUNC(DeleteFBO);
	REGISTER_LUA_CFUNC(IsValidFBO);
	REGISTER_LUA_CFUNC(ActiveFBO);
	REGISTER_LUA_CFUNC(RawBindFBO);

	if (GLEW_EXT_framebuffer_blit)
		REGISTER_LUA_CFUNC(BlitFBO);

	return true;
}


bool LuaFBOs::CreateMetatable(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	luaL_newmetatable(L, "FBO");
	HSTR_PUSH_CFUNC(L, "__gc",        meta_gc);
	HSTR_PUSH_CFUNC(L, "__index",     meta_index);
	HSTR_PUSH_CFUNC(L, "__newindex",  meta_newindex);
	lua_pop(L, 1);
	return true;
}


/******************************************************************************/
/******************************************************************************/

inline void CheckDrawingEnabled(lua_State* L, const char* caller)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!LuaOpenGL::IsDrawingEnabled(L)) {
		luaL_error(L, "%s(): OpenGL calls can only be used in Draw() "
		              "call-ins, or while creating display lists", caller);
	}
}


/******************************************************************************/
/******************************************************************************/

static GLenum GetBindingEnum(GLenum target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (target) {
		case GL_FRAMEBUFFER_EXT:      { return GL_FRAMEBUFFER_BINDING_EXT;      }
		case GL_DRAW_FRAMEBUFFER_EXT: { return GL_DRAW_FRAMEBUFFER_BINDING_EXT; }
		case GL_READ_FRAMEBUFFER_EXT: { return GL_READ_FRAMEBUFFER_BINDING_EXT; }
		default: {}
	}

	return 0;
}

static GLenum ParseAttachment(const std::string& name)
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (hashString(name.c_str())) {
		case hashString(  "depth"): { return GL_DEPTH_ATTACHMENT  ; } break;
		case hashString("stencil"): { return GL_STENCIL_ATTACHMENT; } break;
		case hashString("color0" ): { return GL_COLOR_ATTACHMENT0 ; } break;
		case hashString("color1" ): { return GL_COLOR_ATTACHMENT1 ; } break;
		case hashString("color2" ): { return GL_COLOR_ATTACHMENT2 ; } break;
		case hashString("color3" ): { return GL_COLOR_ATTACHMENT3 ; } break;
		case hashString("color4" ): { return GL_COLOR_ATTACHMENT4 ; } break;
		case hashString("color5" ): { return GL_COLOR_ATTACHMENT5 ; } break;
		case hashString("color6" ): { return GL_COLOR_ATTACHMENT6 ; } break;
		case hashString("color7" ): { return GL_COLOR_ATTACHMENT7 ; } break;
		case hashString("color8" ): { return GL_COLOR_ATTACHMENT8 ; } break;
		case hashString("color9" ): { return GL_COLOR_ATTACHMENT9 ; } break;
		case hashString("color10"): { return GL_COLOR_ATTACHMENT10; } break;
		case hashString("color11"): { return GL_COLOR_ATTACHMENT11; } break;
		case hashString("color12"): { return GL_COLOR_ATTACHMENT12; } break;
		case hashString("color13"): { return GL_COLOR_ATTACHMENT13; } break;
		case hashString("color14"): { return GL_COLOR_ATTACHMENT14; } break;
		case hashString("color15"): { return GL_COLOR_ATTACHMENT15; } break;
		default                   : {                               } break;
	}

	return 0;
}


/******************************************************************************/
/******************************************************************************/

const LuaFBOs::FBO* LuaFBOs::GetLuaFBO(lua_State* L, int index)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return static_cast<FBO*>(LuaUtils::GetUserData(L, index, "FBO"));
}


/******************************************************************************/
/******************************************************************************/

void LuaFBOs::FBO::Init(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	index  = -1u;
	id     = 0;
	target = GL_FRAMEBUFFER_EXT;
	luaRef = LUA_NOREF;
	xsize = 0;
	ysize = 0;
	zsize = 0;
}


void LuaFBOs::FBO::Free(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (luaRef == LUA_NOREF)
		return;

	luaL_unref(L, LUA_REGISTRYINDEX, luaRef);
	luaRef = LUA_NOREF;

	glDeleteFramebuffersEXT(1, &id);
	id = 0;

	{
		// get rid of the userdatum
		LuaFBOs& activeFBOs = CLuaHandle::GetActiveFBOs(L);
		auto& fbos = activeFBOs.fbos;

		assert(index < fbos.size());
		assert(fbos[index] == this);

		fbos[index] = fbos.back();
		fbos[index]->index = index;
		fbos.pop_back();
	}
}


/******************************************************************************/
/******************************************************************************/

int LuaFBOs::meta_gc(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));
	fbo->Free(L);
	return 0;
}


int LuaFBOs::meta_index(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));

	if (fbo->luaRef == LUA_NOREF)
		return 0;

	// read the value from the ref table
	lua_rawgeti(L, LUA_REGISTRYINDEX, fbo->luaRef);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	return 1;
}


int LuaFBOs::meta_newindex(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));

	if (fbo->luaRef == LUA_NOREF)
		return 0;

	if (lua_israwstring(L, 2)) {
		const std::string& key = lua_tostring(L, 2);
		const GLenum type = ParseAttachment(key);

		if (type != 0) {
			GLint currentFBO;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);
			glBindFramebufferEXT(fbo->target, fbo->id);
			ApplyAttachment(L, 3, fbo, type);
			glBindFramebufferEXT(fbo->target, currentFBO);
		}
		else if (key == "drawbuffers") {
			GLint currentFBO;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);
			glBindFramebufferEXT(fbo->target, fbo->id);
			ApplyDrawBuffers(L, 3);
			glBindFramebufferEXT(fbo->target, currentFBO);
		}
		else if (key == "readbuffer") {
			GLint currentFBO;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);
			glBindFramebufferEXT(fbo->target, fbo->id);

			if (lua_isnumber(L, 3))
				glReadBuffer((GLenum)lua_toint(L, 3));

			glBindFramebufferEXT(fbo->target, currentFBO);
		}
		else if (key == "target") {
			return 0;// fbo->target = (GLenum)luaL_checkint(L, 3);
		}
	}

	// set the key/value in the ref table
	lua_rawgeti(L, LUA_REGISTRYINDEX, fbo->luaRef);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, 3);
	lua_rawset(L, -3);
	return 0;
}



/******************************************************************************/
/******************************************************************************/

bool LuaFBOs::AttachObject(
	const char* funcName,
	lua_State* L,
	int index,
	FBO* fbo,
	GLenum attachID,
	GLenum attachTarget,
	GLenum attachLevel
) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (lua_isnil(L, index)) {
		// nil object
		glFramebufferTexture2DEXT(fbo->target, attachID, GL_TEXTURE_2D, 0, 0);
		glFramebufferRenderbufferEXT(fbo->target, attachID, GL_RENDERBUFFER_EXT, 0);
		return true;
	}
	if (lua_israwstring(L, index)) {
		// custom texture
		const LuaTextures& textures = CLuaHandle::GetActiveTextures(L);
		const LuaTextures::Texture* tex = textures.GetInfo(lua_tostring(L, index));

		if (tex == nullptr)
			return false;

		if (attachTarget == 0)
			attachTarget = tex->target;

		try {
			AttachObjectTexTarget(funcName, fbo->target, attachTarget, tex->id, attachID, attachLevel);
		}
		catch (const opengl_error& e) {
			luaL_error(L, "%s", e.what());
		}


		fbo->xsize = tex->xsize;
		fbo->ysize = tex->ysize;
		fbo->zsize = tex->zsize;
		fbo->SetAttachmentFormat(attachID, tex->format);
		return true;
	}

	// render buffer object
	const LuaRBOs::RBO* rbo = static_cast<LuaRBOs::RBO*>(LuaUtils::GetUserData(L, index, "RBO"));

	if (rbo == nullptr)
		return false;

	if (attachTarget == 0)
		attachTarget = rbo->target;

	glFramebufferRenderbufferEXT(fbo->target, attachID, attachTarget, rbo->id);

	fbo->xsize = rbo->xsize;
	fbo->ysize = rbo->ysize;
	fbo->zsize = 0; //RBO can't be 3D or CUBE_MAP
	fbo->SetAttachmentFormat(attachID, rbo->format);
	return true;
}

void LuaFBOs::AttachObjectTexTarget(const char* funcName, GLenum fboTarget, GLenum texTarget, GLuint texId, GLenum attachID, GLenum attachLevel)
{
	RECOIL_DETAILED_TRACY_ZONE;
	//  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, tex.target, texID, 0);
	switch (texTarget)
	{
	case GL_TEXTURE_1D:
		glFramebufferTexture1DEXT(fboTarget, attachID, texTarget, texId, attachLevel);
		break;
	case GL_TEXTURE_2D:
		glFramebufferTexture2DEXT(fboTarget, attachID, texTarget, texId, attachLevel);
		break;
	case GL_TEXTURE_2D_MULTISAMPLE:
		glFramebufferTexture2DEXT(fboTarget, attachID, texTarget, texId, 0);
		break;
	case GL_TEXTURE_2D_ARRAY: [[fallthrough]];
	case GL_TEXTURE_CUBE_MAP: [[fallthrough]];
	case GL_TEXTURE_3D: {
		if (!GLEW_VERSION_3_2) {
			throw opengl_error(fmt::format("[LuaFBO::{}] Using of the attachment target {} requires OpenGL >= 3.2", funcName, texTarget));
		}

		glFramebufferTexture(fboTarget, attachID, texId, attachLevel); //attach the whole texture
	} break;
	default: {
		throw opengl_error(fmt::format("[LuaFBO::{}] Incorrect texture attach target {}", funcName, texTarget));
	} break;

	}
}


bool LuaFBOs::ApplyAttachment(
	lua_State* L,
	int index,
	FBO* fbo,
	const GLenum attachID
) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (attachID == 0)
		return false;

	if (!lua_istable(L, index))
		return AttachObject(__func__, L, index, fbo, attachID);

	const int table = (index < 0) ? index : (lua_gettop(L) + index + 1);

	GLenum target = 0;
	GLint  level  = 0;

	lua_rawgeti(L, table, 2);
	if (lua_isnumber(L, -1))
		target = (GLenum)lua_toint(L, -1);
	lua_pop(L, 1);

	lua_rawgeti(L, table, 3);
	if (lua_isnumber(L, -1))
		level = (GLint)lua_toint(L, -1);
	lua_pop(L, 1);

	lua_rawgeti(L, table, 1);
	const bool success = AttachObject(__func__, L, -1, fbo, attachID, target, level);
	lua_pop(L, 1);

	return success;
}


bool LuaFBOs::ApplyDrawBuffers(lua_State* L, int index)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (lua_isnumber(L, index)) {
		glDrawBuffer((GLenum)lua_toint(L, index));
		return true;
	}
	if (lua_istable(L, index) && GLEW_ARB_draw_buffers) {
		int buffers[32] = {GL_NONE};
		const int count = LuaUtils::ParseIntArray(L, index, buffers, sizeof(buffers) / sizeof(buffers[0]));

		glDrawBuffersARB(count, reinterpret_cast<const GLenum*>(&buffers[0]));
		return true;
	}

	return false;
}


inline void LuaFBOs::SetActiveFBO(lua_State* L, GLenum target, const LuaFBOs::FBO* fbo) {
	LuaFBOs& fbos = CLuaHandle::GetActiveFBOs(L);
	if (target == GL_DRAW_FRAMEBUFFER_EXT || target == GL_FRAMEBUFFER_EXT)
		fbos.activeDrawFBO = fbo;
	if (target == GL_READ_FRAMEBUFFER_EXT || target == GL_FRAMEBUFFER_EXT)
		fbos.activeReadFBO = fbo;
}


inline LuaFBOs::TempActiveFBO::TempActiveFBO(lua_State* L, GLenum target, const LuaFBOs::FBO* newFBO)
:	fbos(CLuaHandle::GetActiveFBOs(L))
{
	drawFBO = fbos.activeDrawFBO;
	readFBO = fbos.activeReadFBO;
	if (target == GL_DRAW_FRAMEBUFFER_EXT || target == GL_FRAMEBUFFER_EXT)
		fbos.activeDrawFBO = newFBO;
	if (target == GL_READ_FRAMEBUFFER_EXT || target == GL_FRAMEBUFFER_EXT)
		fbos.activeReadFBO = newFBO;
}

inline LuaFBOs::TempActiveFBO::~TempActiveFBO() {
	fbos.activeDrawFBO = drawFBO;
	fbos.activeReadFBO = readFBO;
}


/******************************************************************************/
/******************************************************************************/

/***
 * @table attachment
 * attachment ::= luaTex or `RBO.rbo` or nil or { luaTex [, num target [, num level ] ] }
 */

/***
 * User Data FBO
 * @table fbo
 * @tparam attachment depth
 * @tparam attachment stencil
 * @tparam attachment color0
 * @tparam attachment color1
 * @tparam attachment color2
 * @tparam attachment colorn
 * @tparam attachment color15
 * @tparam table drawbuffers `{ GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT3_EXT, ..}`
 * @tparam table readbuffer `GL_COLOR_ATTACHMENT0_EXT`
 */

/***
 * @function gl.CreateFBO
 * @param data
 * @tparam attachment data.depth
 * @tparam attachment data.stencil
 * @tparam attachment data.color0
 * @tparam attachment data.color1
 * @tparam attachment data.color2
 * @tparam attachment data.colorn
 * @tparam attachment data.color15
 * @tparam table data.drawbuffers `{ GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT3_EXT, ..}`
 */
int LuaFBOs::CreateFBO(lua_State* L)
{
	FBO fbo;
	fbo.Init(L);

	const int table = 1;
/*
	if (lua_istable(L, table)) {
		lua_getfield(L, table, "target");
		if (lua_isnumber(L, -1)) {
			fbo.target = (GLenum)lua_toint(L, -1);
		} else {
			lua_pop(L, 1);
		}
	}
*/
	const GLenum bindTarget = GetBindingEnum(fbo.target);
	if (bindTarget == 0)
		return 0;

	// maintain a lua table to hold RBO references
 	lua_newtable(L);
	fbo.luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
	if (fbo.luaRef == LUA_NOREF)
		return 0;

	GLint currentFBO;
	glGetIntegerv(bindTarget, &currentFBO);

	glGenFramebuffersEXT(1, &fbo.id);
	glBindFramebufferEXT(fbo.target, fbo.id);


	FBO* fboPtr = static_cast<FBO*>(lua_newuserdata(L, sizeof(FBO)));
	*fboPtr = fbo;

	luaL_getmetatable(L, "FBO");
	lua_setmetatable(L, -2);

	// parse the initialization table
	if (lua_istable(L, table)) {
		for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
			if (lua_israwstring(L, -2)) {
				const std::string& key = lua_tostring(L, -2);
				const GLenum type = ParseAttachment(key);
				if (type != 0) {
					ApplyAttachment(L, -1, fboPtr, type);
				}
				else if (key == "drawbuffers") {
					ApplyDrawBuffers(L, -1);
				}
			}
		}
	}

	// revert to the old fbo
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, currentFBO);

	if (fboPtr->luaRef != LUA_NOREF) {
		LuaFBOs& activeFBOs = CLuaHandle::GetActiveFBOs(L);
		auto& fbos = activeFBOs.fbos;

		fbos.push_back(fboPtr);
		fboPtr->index = fbos.size() - 1;
	}

	return 1;
}


/***
 * @function gl.DeleteFBO
 * This doesn't delete the attached objects!
 * @tparam fbo fbo
 */
int LuaFBOs::DeleteFBO(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (lua_isnil(L, 1))
		return 0;

	FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));
	fbo->Free(L);
	return 0;
}


/***
 * @function gl.IsValidFBO
 * @tparam fbo fbo
 * @number[opt] target
 * @treturn bool valid
 * @treturn ?number status
 */
int LuaFBOs::IsValidFBO(lua_State* L)
{
	if (lua_isnil(L, 1) || !lua_isuserdata(L, 1)) {
		lua_pushboolean(L, false);
		return 1;
	}

	const FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));

	if ((fbo->id == 0) || (fbo->luaRef == LUA_NOREF)) {
		lua_pushboolean(L, false);
		return 1;
	}

	const GLenum target = (GLenum)luaL_optint(L, 2, fbo->target);
	const GLenum bindTarget = GetBindingEnum(target);

	if (bindTarget == 0) {
		lua_pushboolean(L, false);
		return 1;
	}

	GLint currentFBO;
	glGetIntegerv(bindTarget, &currentFBO);

	glBindFramebufferEXT(target, fbo->id);
	const GLenum status = glCheckFramebufferStatusEXT(target);
	glBindFramebufferEXT(target, currentFBO);

	lua_pushboolean(L, (status == GL_FRAMEBUFFER_COMPLETE_EXT));
	lua_pushnumber(L, status);
	return 2;
}


/***
 * @function gl.ActiveFBO
 * @tparam fbo fbo
 * @number[opt] target
 * @bool[opt] identities
 * @func[opt] lua_function
 * @param[opt] arg1
 * @param[opt] arg2
 * @param[opt] argn
 */
int LuaFBOs::ActiveFBO(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CheckDrawingEnabled(L, __func__);
	
	const FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));

	if (fbo->id == 0)
		return 0;

	int funcIndex = 2;

	// target and matrix manipulation options
	GLenum target = fbo->target;
	if (lua_israwnumber(L, funcIndex)) {
		target = (GLenum)lua_toint(L, funcIndex);
		funcIndex++;
	}

	bool identities = false;

	if (lua_isboolean(L, funcIndex)) {
		identities = lua_toboolean(L, funcIndex);
		funcIndex++;
	}

	if (!lua_isfunction(L, funcIndex))
		luaL_error(L, "Incorrect arguments to gl.ActiveFBO()");

	const GLenum bindTarget = GetBindingEnum(target);

	if (bindTarget == 0)
		return 0;

	glPushAttrib(GL_VIEWPORT_BIT);
	glViewport(0, 0, fbo->xsize, fbo->ysize);
	if (identities) {
		glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
	}

	GLint currentFBO = 0;
	glGetIntegerv(bindTarget, &currentFBO);
	glBindFramebufferEXT(target, fbo->id);

	auto const tempActiveFBO = TempActiveFBO(L, target, fbo);

	const int error = lua_pcall(L, (lua_gettop(L) - funcIndex), 0, 0);

	glBindFramebufferEXT(target, currentFBO);
	if (identities) {
		glMatrixMode(GL_PROJECTION); glPopMatrix();
		glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	}
	glPopAttrib();

	if (error != 0) {
		LOG_L(L_ERROR, "gl.ActiveFBO: error(%i) = %s", error, lua_tostring(L, -1));
		lua_error(L);
	}

	return 0;
}


/**
 *gl.RawBindFBO
 *
 * ( nil [, number target = GL_FRAMEBUFFER_EXT ] [, number rawFboId = 0] ) -> nil (Bind default or specified via rawFboId numeric id of FBO)
 * ( fbo [, number target = fbo.target ] ) -> number previouslyBoundRawFboId
 */
int LuaFBOs::RawBindFBO(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	//CheckDrawingEnabled(L, __func__);

	if (lua_isnil(L, 1)) {
		const GLenum target = luaL_optinteger(L, 2, GL_FRAMEBUFFER_EXT);

		// revert to default or specified FB
		glBindFramebufferEXT(target, luaL_optinteger(L, 3, 0));

		SetActiveFBO(L, target, nullptr);

		return 0;
	}

	const FBO* fbo = static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));

	if (fbo->id == 0)
		return 0;

	const GLenum target = luaL_optinteger(L, 2, fbo->target);

	GLint currentFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);
	glBindFramebufferEXT(target, fbo->id);

	SetActiveFBO(L, target, fbo);

	lua_pushnumber(L, currentFBO);
	return 1;
}


/******************************************************************************/

/*** needs `GLEW_EXT_framebuffer_blit`
 *
 * @function gl.BlitFBO
 * @number x0Src
 * @number y0Src
 * @number x1Src
 * @number y1Src
 * @number x0Dst
 * @number y0Dst
 * @number x1Dst
 * @number y1Dst
 * @number[opt=GL_COLOR_BUFFER_BIT] mask
 * @number[opt=GL_NEAREST] filter
 */
/*** needs `GLEW_EXT_framebuffer_blit`
 *
 * @function gl.BlitFBO
 * @tparam fbo fboSrc
 * @number x0Src
 * @number y0Src
 * @number x1Src
 * @number y1Src
 * @tparam fbo fboDst
 * @number x0Dst
 * @number y0Dst
 * @number x1Dst
 * @number y1Dst
 * @number[opt=GL_COLOR_BUFFER_BIT] mask
 * @number[opt=GL_NEAREST] filter
 */
int LuaFBOs::BlitFBO(lua_State* L)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (lua_israwnumber(L, 1)) {
		const GLint x0Src = (GLint)luaL_checknumber(L, 1);
		const GLint y0Src = (GLint)luaL_checknumber(L, 2);
		const GLint x1Src = (GLint)luaL_checknumber(L, 3);
		const GLint y1Src = (GLint)luaL_checknumber(L, 4);

		const GLint x0Dst = (GLint)luaL_checknumber(L, 5);
		const GLint y0Dst = (GLint)luaL_checknumber(L, 6);
		const GLint x1Dst = (GLint)luaL_checknumber(L, 7);
		const GLint y1Dst = (GLint)luaL_checknumber(L, 8);

		const GLbitfield mask = (GLbitfield)luaL_optint(L, 9, GL_COLOR_BUFFER_BIT);
		const GLenum filter = (GLenum)luaL_optint(L, 10, GL_NEAREST);

		glBlitFramebufferEXT(x0Src, y0Src, x1Src, y1Src,  x0Dst, y0Dst, x1Dst, y1Dst,  mask, filter);
		return 0;
	}

	const FBO* fboSrc = (lua_isnil(L, 1))? nullptr: static_cast<FBO*>(luaL_checkudata(L, 1, "FBO"));
	const FBO* fboDst = (lua_isnil(L, 6))? nullptr: static_cast<FBO*>(luaL_checkudata(L, 6, "FBO"));

	// if passed a non-nil arg, userdatum buffer must always be valid
	// otherwise the default framebuffer is substituted as its target
	if (fboSrc != nullptr && fboSrc->id == 0)
		return 0;
	if (fboDst != nullptr && fboDst->id == 0)
		return 0;

	const GLint x0Src = (GLint)luaL_checknumber(L, 2);
	const GLint y0Src = (GLint)luaL_checknumber(L, 3);
	const GLint x1Src = (GLint)luaL_checknumber(L, 4);
	const GLint y1Src = (GLint)luaL_checknumber(L, 5);

	const GLint x0Dst = (GLint)luaL_checknumber(L, 7);
	const GLint y0Dst = (GLint)luaL_checknumber(L, 8);
	const GLint x1Dst = (GLint)luaL_checknumber(L, 9);
	const GLint y1Dst = (GLint)luaL_checknumber(L, 10);

	const GLbitfield mask = (GLbitfield)luaL_optint(L, 11, GL_COLOR_BUFFER_BIT);
	const GLenum filter = (GLenum)luaL_optint(L, 12, GL_NEAREST);

	GLint currentFBO;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);

	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, (fboSrc == nullptr)? 0: fboSrc->id);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, (fboDst == nullptr)? 0: fboDst->id);

	glBlitFramebufferEXT(x0Src, y0Src, x1Src, y1Src,  x0Dst, y0Dst, x1Dst, y1Dst,  mask, filter);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, currentFBO);
	return 0;
}


/******************************************************************************/
/******************************************************************************/
