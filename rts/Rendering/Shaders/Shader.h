/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef SPRING_SHADER_HDR
#define SPRING_SHADER_HDR

#include <algorithm>
#include <functional>
#include <string>
#include <memory>
#include <vector>

#include "ShaderStates.h"
#include "Lua/LuaOpenGLUtils.h"
#include "System/UnorderedMap.hpp"
#include "System/StringHash.h"
#include "System/Cpp11Compat.hpp"
#include "Rendering/GL/VertexArrayTypes.h"

struct fast_hash
{
	size_t operator()(const int a) const
	{
		return a;
	}
};

namespace Shader {
	struct IShaderObject {
	public:
		IShaderObject(unsigned int shType, const std::string& shSrcFile, const std::string& shSrcDefs = "")
			: objID(0)
			, type(shType)
			, logReporting(true)
			, valid(false)
			, reloadRequested(false)
			, srcFile(shSrcFile)
			, rawDefStrs(shSrcDefs) {
		}

		virtual ~IShaderObject() {}

		virtual void Compile() {}
		virtual void Release() {}

		void SetLogReporting(bool b) { logReporting = b; }

		bool ReloadFromDisk();
		bool IsValid() const { return valid; }
		void SetReloadComplete() { reloadRequested = false; }
		bool IsReloadRequested() const { return reloadRequested;  }

		unsigned int GetObjID() const { return objID; }
		unsigned int GetType() const { return type; }
		unsigned int GetHash() const;

		const std::string& GetLog() const { return log; }

		void SetDefinitions(const std::string& defs) { modDefStrs = defs; }
		std::string GetShaderSource(const std::string& fileName);
	protected:
		unsigned int objID;
		unsigned int type;

		bool logReporting;
		bool valid;
		bool reloadRequested;

		std::string srcFile;
		std::string srcText;
		std::string rawDefStrs; // set via constructor only, constant
		std::string modDefStrs; // set on reload from changed flags
		std::string log;
	};

	struct NullShaderObject: public Shader::IShaderObject {
	public:
		NullShaderObject(unsigned int shType, const std::string& shSrcFile) : IShaderObject(shType, shSrcFile) {}
	};

	struct ARBShaderObject: public Shader::IShaderObject {
	public:
		ARBShaderObject(unsigned int, const std::string&, const std::string& shSrcDefs = "");
		void Compile();
		void Release();
	};

	struct GLSLShaderObject: public Shader::IShaderObject {
	public:
		GLSLShaderObject(unsigned int, const std::string&, const std::string& shSrcDefs = "");

		struct CompiledShaderObject {
			CompiledShaderObject() : id(0), valid(false) {}

			unsigned int id;
			bool      valid;
			std::string log;
		};

		/// @brief Returns a GLSL shader object in an unqiue pointer that auto deletes that instance.
		///        Quote of GL docs: If a shader object is deleted while it is attached to a program object,
		///        it will be flagged for deletion, and deletion will not occur until glDetachShader is called
		///        to detach it from all program objects to which it is attached.
		typedef std::unique_ptr<CompiledShaderObject, std::function<void(CompiledShaderObject* so)>> CompiledShaderObjectUniquePtr;
		CompiledShaderObjectUniquePtr CompileShaderObject();
	};

	struct IProgramObject;
	struct ShaderEnabledToken {
		ShaderEnabledToken(IProgramObject* prog_);

		ShaderEnabledToken(ShaderEnabledToken&& rhs) noexcept { *this = std::move(rhs); }
		ShaderEnabledToken& operator=(ShaderEnabledToken&& rhs) noexcept { std::swap(prog, rhs.prog); return *this; }

		ShaderEnabledToken(const ShaderEnabledToken&) = delete;
		ShaderEnabledToken& operator=(const ShaderEnabledToken& rhs) = delete;

		~ShaderEnabledToken();
	private:
		IProgramObject* prog = nullptr;
	};

	struct IProgramObject {
	public:
		IProgramObject(const std::string& poName);
		virtual ~IProgramObject() {}

		void LoadFromID(unsigned int id) {
			objID = id;
			valid = (id != 0 && Validate());
			bound = false;

			// not needed for pre-compiled programs
			shaderObjs.clear();
		}

		/// create the whole shader from a lua file
		bool LoadFromLua(const std::string& filename);

		virtual void BindAttribLocation(const std::string& name, uint32_t index) {}
		template<typename VAT>
		void BindAttribLocations();

		void SetLogReporting(bool b, bool shObjects = true);

		[[nodiscard]] ShaderEnabledToken EnableScoped() {
			return ShaderEnabledToken(this);
		}
		virtual void Enable();
		virtual void Disable();
		virtual void EnableRaw() {}
		virtual void DisableRaw() {}
		virtual void Link() = 0;
		virtual bool Validate() = 0;
		virtual void Release();
		virtual void Reload(bool reloadFromDisk, bool validate) = 0;
		/// attach single shader objects (vertex, frag, ...) to the program
		void AttachShaderObject(IShaderObject* so);
		bool RemoveShaderObject(GLenum soType);

		void SetReloadComplete() {
			for (auto so : shaderObjs)
				so->SetReloadComplete();
		}
		bool IsReloadRequested() const {
			bool reloadRequested = false;
			for (auto so : shaderObjs)
				reloadRequested |= so->IsReloadRequested();

			return reloadRequested;
		}

		bool IsBound() const { return bound; }
		bool IsValid() const { return valid; }
		bool IsShaderAttached(const IShaderObject* so) const {
			return (std::find(shaderObjs.begin(), shaderObjs.end(), so) != shaderObjs.end());
		}

		unsigned int GetObjID() const { return objID; }

		const std::string& GetName() const { return name; }
		const std::string& GetLog() const { return log; }

		const std::vector<IShaderObject*>& GetAttachedShaderObjs() const { return shaderObjs; }
		      std::vector<IShaderObject*>& GetAttachedShaderObjs()       { return shaderObjs; }

		void RecompileIfNeeded(bool validate);
		void PrintDebugInfo();

	public:
		/// new interface
		template<typename TV> inline void SetUniform(const char* name, TV v0) { SetUniform(GetUniformState(name), v0); }
		template<typename TV> inline void SetUniform(const char* name, TV v0, TV v1)  { SetUniform(GetUniformState(name), v0, v1); }
		template<typename TV> inline void SetUniform(const char* name, TV v0, TV v1, TV v2)  { SetUniform(GetUniformState(name), v0, v1, v2); }
		template<typename TV> inline void SetUniform(const char* name, TV v0, TV v1, TV v2, TV v3)  { SetUniform(GetUniformState(name), v0, v1, v2, v3); }

		template<typename TV> inline void SetUniform2v(const char* name, const TV* v) { SetUniform2v(GetUniformState(name), 1, v); }
		template<typename TV> inline void SetUniform3v(const char* name, const TV* v) { SetUniform3v(GetUniformState(name), 1, v); }
		template<typename TV> inline void SetUniform4v(const char* name, const TV* v) { SetUniform4v(GetUniformState(name), 1, v); }

		/// variants with count param
		template<typename TV> inline void SetUniform1v(const char* name, const GLsizei count, const TV* v) { SetUniform1v(GetUniformState(name), count, v); }
		template<typename TV> inline void SetUniform2v(const char* name, const GLsizei count, const TV* v) { SetUniform2v(GetUniformState(name), count, v); }
		template<typename TV> inline void SetUniform3v(const char* name, const GLsizei count, const TV* v) { SetUniform3v(GetUniformState(name), count, v); }
		template<typename TV> inline void SetUniform4v(const char* name, const GLsizei count, const TV* v) { SetUniform4v(GetUniformState(name), count, v); }

		template<typename TV> inline void SetUniformMatrix2x2(const char* name, bool transp, const TV* v) { SetUniformMatrix2x2(GetUniformState(name), transp, v); }
		template<typename TV> inline void SetUniformMatrix3x3(const char* name, bool transp, const TV* v) { SetUniformMatrix3x3(GetUniformState(name), transp, v); }
		template<typename TV> inline void SetUniformMatrix4x4(const char* name, bool transp, const TV* v) { SetUniformMatrix4x4(GetUniformState(name), transp, v); }

		template<typename TV> void SetFlag(const char* key, TV val) { shaderFlags.Set(key, val); }
		template<typename TV> bool GetFlag(const char* key, TV& val) { return shaderFlags.Get(key, val); }


		/// old interface
		virtual void SetUniformTarget(int) {} //< only needed for ARB, for GLSL uniforms of vertex & frag shader are accessed in the same space
		virtual void SetUniformLocation(const std::string&) {}

		virtual void SetUniform1i(int idx, int   v0) = 0;
		virtual void SetUniform2i(int idx, int   v0, int   v1) = 0;
		virtual void SetUniform3i(int idx, int   v0, int   v1, int   v2) = 0;
		virtual void SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) = 0;
		virtual void SetUniform1f(int idx, float v0) = 0;
		virtual void SetUniform2f(int idx, float v0, float v1) = 0;
		virtual void SetUniform3f(int idx, float v0, float v1, float v2) = 0;
		virtual void SetUniform4f(int idx, float v0, float v1, float v2, float v3) = 0;

		virtual void SetUniform2iv(int idx, const int*   v) = 0;
		virtual void SetUniform3iv(int idx, const int*   v) = 0;
		virtual void SetUniform4iv(int idx, const int*   v) = 0;
		virtual void SetUniform2fv(int idx, const float* v) = 0;
		virtual void SetUniform3fv(int idx, const float* v) = 0;
		virtual void SetUniform4fv(int idx, const float* v) = 0;

		/// variants with count param
		virtual void SetUniform1iv(int idx, GLsizei count, const int*   v) = 0;
		virtual void SetUniform2iv(int idx, GLsizei count, const int*   v) = 0;
		virtual void SetUniform3iv(int idx, GLsizei count, const int*   v) = 0;
		virtual void SetUniform4iv(int idx, GLsizei count, const int*   v) = 0;
		virtual void SetUniform1fv(int idx, GLsizei count, const float* v) = 0;
		virtual void SetUniform2fv(int idx, GLsizei count, const float* v) = 0;
		virtual void SetUniform3fv(int idx, GLsizei count, const float* v) = 0;
		virtual void SetUniform4fv(int idx, GLsizei count, const float* v) = 0;

		virtual void SetUniformMatrix2fv(int idx, bool transp, const float* v) {}
		virtual void SetUniformMatrix3fv(int idx, bool transp, const float* v) {}
		virtual void SetUniformMatrix4fv(int idx, bool transp, const float* v) {}

	public:
		/// interface to auto-bind textures with the shader
		void AddTextureBinding(const int texUnit, const std::string& luaTexName);
		void BindTextures() const;

	protected:
		/// internal
		virtual void SetUniform(UniformState* uState, int   v0) { SetUniform1i(uState->GetLocation(), v0); }
		virtual void SetUniform(UniformState* uState, float v0) { SetUniform1f(uState->GetLocation(), v0); }
		virtual void SetUniform(UniformState* uState, int   v0, int   v1) { SetUniform2i(uState->GetLocation(), v0, v1); }
		virtual void SetUniform(UniformState* uState, float v0, float v1) { SetUniform2f(uState->GetLocation(), v0, v1); }
		virtual void SetUniform(UniformState* uState, int   v0, int   v1, int   v2) { SetUniform3i(uState->GetLocation(), v0, v1, v2); }
		virtual void SetUniform(UniformState* uState, float v0, float v1, float v2) { SetUniform3f(uState->GetLocation(), v0, v1, v2); }
		virtual void SetUniform(UniformState* uState, int   v0, int   v1, int   v2, int   v3) { SetUniform4i(uState->GetLocation(), v0, v1, v2, v3); }
		virtual void SetUniform(UniformState* uState, float v0, float v1, float v2, float v3) { SetUniform4f(uState->GetLocation(), v0, v1, v2, v3); }
		
		virtual void SetUniform1v(UniformState* uState, GLsizei count, const int*   v) { SetUniform1iv(uState->GetLocation(), count, v); }
		virtual void SetUniform1v(UniformState* uState, GLsizei count, const float* v) { SetUniform1fv(uState->GetLocation(), count, v); }
		virtual void SetUniform2v(UniformState* uState, GLsizei count, const int*   v) { SetUniform2iv(uState->GetLocation(), count, v); }
		virtual void SetUniform2v(UniformState* uState, GLsizei count, const float* v) { SetUniform2fv(uState->GetLocation(), count, v); }
		virtual void SetUniform3v(UniformState* uState, GLsizei count, const int*   v) { SetUniform3iv(uState->GetLocation(), count, v); }
		virtual void SetUniform3v(UniformState* uState, GLsizei count, const float* v) { SetUniform3fv(uState->GetLocation(), count, v); }
		virtual void SetUniform4v(UniformState* uState, GLsizei count, const int*   v) { SetUniform4iv(uState->GetLocation(), count, v); }
		virtual void SetUniform4v(UniformState* uState, GLsizei count, const float* v) { SetUniform4fv(uState->GetLocation(), count, v); }

		virtual void SetUniformMatrix2x2(UniformState* uState, bool transp, const float* m) { SetUniformMatrix2fv(uState->GetLocation(), transp, m); }
		virtual void SetUniformMatrix3x3(UniformState* uState, bool transp, const float* m) { SetUniformMatrix3fv(uState->GetLocation(), transp, m); }
		virtual void SetUniformMatrix4x4(UniformState* uState, bool transp, const float* m) { SetUniformMatrix4fv(uState->GetLocation(), transp, m); }

	protected:
		int GetUniformLocation(const std::string& name) { return GetUniformState(name)->GetLocation(); }
		UniformState* GetUniformState(const std::string& name) {
			const auto hash = hashString(name.c_str()); // never compiletime const (std::string is never a literal)
			const auto iter = uniformStates.find(hash);
			if (iter != uniformStates.end())
				return &iter->second;
			return GetNewUniformState(name.c_str());
		}
		UniformState* GetUniformState(const char* name) {
			// (when inlined) hash might be compiletime const cause of constexpr of hashString
			// WARNING: Cause of a bug in gcc, you _must_ assign the constexpr to a var before
			//          passing it to a function. I.e. foo.find(hashString(name)) would always
			//          be runtime evaluated (even when `name` is a literal)!
			const auto hash = hashString(name);
			const auto iter = uniformStates.find(hash);
			if (iter != uniformStates.end())
				return &iter->second;
			return GetNewUniformState(name);
		}

	private:
		virtual int GetUniformLoc(const char* name) = 0;
		virtual int GetUniformType(const int idx) = 0;

		UniformState* GetNewUniformState(const char* name);
	protected:
		std::string name;
		std::string log;

		unsigned int objID;

		bool logReporting;
		bool valid;
		bool bound;

		std::vector<IShaderObject*> shaderObjs;

		ShaderFlags shaderFlags;

	public:
		//using UniformStates = std::unordered_map<std::uint32_t, UniformState, fast_hash>; //nicer for debug
		using UniformStates = spring::unsynced_map<std::uint32_t, UniformState, fast_hash>;
		UniformStates uniformStates;
		spring::unsynced_map<int, LuaMatTexture> luaTextures;
		spring::unsynced_map<std::string, int> attribLocations;
	};


	struct NullProgramObject: public Shader::IProgramObject {
	public:
		NullProgramObject(const std::string& poName): IProgramObject(poName) {}

		void Enable() override {}
		void Disable() override {}
		void Release() override {}
		void Reload(bool reloadFromDisk, bool validate) override {}
		bool Validate() override { return true; }
		void Link() override {}

		int GetUniformLoc(const char* name) override { return -1; }
		int GetUniformType(const int idx) override { return -1; }

		void SetUniform1i(int idx, int   v0) override {}
		void SetUniform2i(int idx, int   v0, int   v1) override {}
		void SetUniform3i(int idx, int   v0, int   v1, int   v2) override {}
		void SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) override {}
		void SetUniform1f(int idx, float v0) override {}
		void SetUniform2f(int idx, float v0, float v1)  override {}
		void SetUniform3f(int idx, float v0, float v1, float v2) override {}
		void SetUniform4f(int idx, float v0, float v1, float v2, float v3) override {}

		void SetUniform2iv(int idx, const int*   v) override {}
		void SetUniform3iv(int idx, const int*   v) override {}
		void SetUniform4iv(int idx, const int*   v) override {}
		void SetUniform2fv(int idx, const float* v) override {}
		void SetUniform3fv(int idx, const float* v) override {}
		void SetUniform4fv(int idx, const float* v) override {}

		/// variants with count param
		void SetUniform1iv(int idx, GLsizei count, const int*   v) override {}
		void SetUniform2iv(int idx, GLsizei count, const int*   v) override {}
		void SetUniform3iv(int idx, GLsizei count, const int*   v) override {}
		void SetUniform4iv(int idx, GLsizei count, const int*   v) override {}
		void SetUniform1fv(int idx, GLsizei count, const float* v) override {}
		void SetUniform2fv(int idx, GLsizei count, const float* v) override {}
		void SetUniform3fv(int idx, GLsizei count, const float* v) override {}
		void SetUniform4fv(int idx, GLsizei count, const float* v) override {}
	};


	struct ARBProgramObject: public Shader::IProgramObject {
	public:
		ARBProgramObject(const std::string& poName);

		void Enable() override;
		void Disable() override;
		void Link() override;
		void Release() override { IProgramObject::Release(); }
		void Reload(bool reloadFromDisk, bool validate) override;
		bool Validate() override { return true; }

		int GetUniformLoc(const char* name) override { return -1; } // FIXME
		int GetUniformType(const int idx) override { return -1; }
		void SetUniformTarget(int target) override;
		int GetUnitformTarget();

		void SetUniform1i(int idx, int   v0) override;
		void SetUniform2i(int idx, int   v0, int   v1) override;
		void SetUniform3i(int idx, int   v0, int   v1, int   v2) override;
		void SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) override;
		void SetUniform1f(int idx, float v0) override;
		void SetUniform2f(int idx, float v0, float v1) override;
		void SetUniform3f(int idx, float v0, float v1, float v2) override;
		void SetUniform4f(int idx, float v0, float v1, float v2, float v3) override;

		void SetUniform2iv(int idx, const int*   v) override;
		void SetUniform3iv(int idx, const int*   v) override;
		void SetUniform4iv(int idx, const int*   v) override;
		void SetUniform2fv(int idx, const float* v) override;
		void SetUniform3fv(int idx, const float* v) override;
		void SetUniform4fv(int idx, const float* v) override;

		/// variants with count param
		void SetUniform1iv(int idx, GLsizei count, const int*   v) override { SetUniform1i(idx, v[0]); }
		void SetUniform2iv(int idx, GLsizei count, const int*   v) override { SetUniform2iv(idx, v); }
		void SetUniform3iv(int idx, GLsizei count, const int*   v) override { SetUniform3iv(idx, v); }
		void SetUniform4iv(int idx, GLsizei count, const int*   v) override { SetUniform4iv(idx, v); }
		void SetUniform1fv(int idx, GLsizei count, const float* v) override { SetUniform1f(idx, v[0]); }
		void SetUniform2fv(int idx, GLsizei count, const float* v) override { SetUniform2fv(idx, v); }
		void SetUniform3fv(int idx, GLsizei count, const float* v) override { SetUniform3fv(idx, v); }
		void SetUniform4fv(int idx, GLsizei count, const float* v) override { SetUniform4fv(idx, v); }

	private:
		int uniformTarget;
	};


	struct GLSLProgramObject: public Shader::IProgramObject {
	public:
		GLSLProgramObject(const std::string& poName);
		~GLSLProgramObject() override { Release(); }

		void BindAttribLocation(const std::string& name, uint32_t index) override;

		void Enable() override;
		void Disable() override { DisableRaw(); }
		void EnableRaw() override;
		void DisableRaw() override;
		void Link() override;
		bool Validate() override;
		void Release() override;
		void Reload(bool reloadFromDisk, bool validate) override;

	public:
		void SetUniformLocation(const std::string&) override;

		void SetUniform1i(int idx, int   v0) override;
		void SetUniform2i(int idx, int   v0, int   v1) override;
		void SetUniform3i(int idx, int   v0, int   v1, int   v2) override;
		void SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) override;
		void SetUniform1f(int idx, float v0) override;
		void SetUniform2f(int idx, float v0, float v1) override;
		void SetUniform3f(int idx, float v0, float v1, float v2) override;
		void SetUniform4f(int idx, float v0, float v1, float v2, float v3) override;

		void SetUniform2iv(int idx, const int*   v) override;
		void SetUniform3iv(int idx, const int*   v) override;
		void SetUniform4iv(int idx, const int*   v) override;
		void SetUniform2fv(int idx, const float* v) override;
		void SetUniform3fv(int idx, const float* v) override;
		void SetUniform4fv(int idx, const float* v) override;

		/// variants with count param
		void SetUniform1iv(int idx, GLsizei count, const int*   v) override;
		void SetUniform2iv(int idx, GLsizei count, const int*   v) override;
		void SetUniform3iv(int idx, GLsizei count, const int*   v) override;
		void SetUniform4iv(int idx, GLsizei count, const int*   v) override;
		void SetUniform1fv(int idx, GLsizei count, const float* v) override;
		void SetUniform2fv(int idx, GLsizei count, const float* v) override;
		void SetUniform3fv(int idx, GLsizei count, const float* v) override;
		void SetUniform4fv(int idx, GLsizei count, const float* v) override;

		void SetUniformMatrix2fv(int idx, bool transp, const float* v) override;
		void SetUniformMatrix3fv(int idx, bool transp, const float* v) override;
		void SetUniformMatrix4fv(int idx, bool transp, const float* v) override;

	private:
		int GetUniformType(const int idx) override;
		int GetUniformLoc(const char* name) override;

		void SetUniform(UniformState* uState, int   v0) override;
		void SetUniform(UniformState* uState, float v0) override;
		void SetUniform(UniformState* uState, int   v0, int   v1) override;
		void SetUniform(UniformState* uState, float v0, float v1) override;
		void SetUniform(UniformState* uState, int   v0, int   v1, int   v2) override;
		void SetUniform(UniformState* uState, float v0, float v1, float v2) override;
		void SetUniform(UniformState* uState, int   v0, int   v1, int   v2, int   v3) override;
		void SetUniform(UniformState* uState, float v0, float v1, float v2, float v3) override;

		/// variants with count param
		void SetUniform1v(UniformState* uState, GLsizei count, const int*   v) override;
		void SetUniform1v(UniformState* uState, GLsizei count, const float* v) override;
		void SetUniform2v(UniformState* uState, GLsizei count, const int*   v) override;
		void SetUniform2v(UniformState* uState, GLsizei count, const float* v) override;
		void SetUniform3v(UniformState* uState, GLsizei count, const int*   v) override;
		void SetUniform3v(UniformState* uState, GLsizei count, const float* v) override;
		void SetUniform4v(UniformState* uState, GLsizei count, const int*   v) override;
		void SetUniform4v(UniformState* uState, GLsizei count, const float* v) override;

		void SetUniformMatrix2x2(UniformState* uState, bool transp, const float*  v) override;
		void SetUniformMatrix3x3(UniformState* uState, bool transp, const float*  v) override;
		void SetUniformMatrix4x4(UniformState* uState, bool transp, const float*  v) override;

	private:
		std::vector<size_t> uniformLocs;
		unsigned int curSrcHash;
	};

	template<typename VAT>
	inline void IProgramObject::BindAttribLocations()
	{
		for (const AttributeDef& def : VAT::attributeDefs) {
			BindAttribLocation(def.name, def.index);
		}
	}

	extern NullShaderObject* nullShaderObject;
	extern NullProgramObject* nullProgramObject;
}

#endif
