/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <string>
#include <vector>

#include "System/creg/creg_cond.h"
#include "System/float4.h"
#include "System/type2.h"
#include "System/UnorderedMap.hpp"
#include "Rendering/Textures/IAtlasAllocator.h"

/** @brief texture coordinates of an atlas subimage. */
//typedef float4 AtlasedTexture;

struct AtlasedTexture
{
	CR_DECLARE_STRUCT(AtlasedTexture)

	explicit AtlasedTexture() = default;
	explicit AtlasedTexture(const float4& f)
		: x(f.x)
		, y(f.y)
		, z(f.z)
		, w(f.w)
	{};
/*
	AtlasedTexture(AtlasedTexture&& f) noexcept { *this = std::move(f); }
	AtlasedTexture& operator= (AtlasedTexture&& f) = default;

	AtlasedTexture(const AtlasedTexture& f) { *this = f; }
	AtlasedTexture& operator= (const AtlasedTexture& f) = default;
*/
	bool operator==(const AtlasedTexture& rhs) const {
		if (this == &rhs)
			return true;

		float4 f0(x    , y    , z    , w    );
		float4 f1(rhs.x, rhs.y, rhs.z, rhs.w);
		return f0 == f1;
	}
	bool operator!=(const AtlasedTexture& rhs) const { return !(*this == rhs); }

	union {
		struct { float x, y, z, w; };
		struct { float x1, y1, x2, y2; };
		struct { float s, t, p, q; };
		struct { float xstart, ystart, xend, yend; };
	};

	static const AtlasedTexture& DefaultAtlasTexture;
};



/** @brief Class for combining multiple bitmaps into one large single bitmap. */
class CTextureAtlas
{
public:
	enum TextureType {
		RGBA32
	};
	enum AllocatorType {
		ATLAS_ALLOC_LEGACY   = 0,
		ATLAS_ALLOC_QUADTREE = 1,
		ATLAS_ALLOC_ROW      = 2,
	};

public:
	CTextureAtlas(uint32_t allocType = ATLAS_ALLOC_LEGACY, int32_t atlasSizeX_ = 0, int32_t atlasSizeY_ = 0, const std::string& name_ = "", bool reloadable_ = false);
	CTextureAtlas(CTextureAtlas&& ta) noexcept { *this = std::move(ta); };
	CTextureAtlas(const CTextureAtlas&) = delete;

	~CTextureAtlas();

	void ReinitAllocator();

	CTextureAtlas& operator= (CTextureAtlas&& ta) noexcept {
		if (this != &ta) {
			delete atlasAllocator; // Free the existing atlasAllocator.

			atlasAllocator = ta.atlasAllocator;
			name = std::move(ta.name);
			memTextures = std::move(ta.memTextures);
			files = std::move(ta.files);
			textures = std::move(ta.textures);
			texToName = std::move(ta.texToName);
			atlasTexID = ta.atlasTexID;
			initialized = ta.initialized;
			freeTexture = ta.freeTexture;

			allocType = ta.allocType;
			atlasSizeX = ta.atlasSizeX;
			atlasSizeY = ta.atlasSizeY;

			reloadable = ta.reloadable;

			// Trick to not call destructor on atlasAllocator multiple times
			ta.atlasAllocator = nullptr;
			ta.atlasTexID = 0u;
		}
		return *this;
	};
	CTextureAtlas& operator= (const CTextureAtlas&) = delete;

	// add a texture from a memory pointer
	size_t AddTexFromMem(std::string name, int xsize, int ysize, TextureType texType, const void* data);
	// add a texture from a file
	size_t AddTexFromFile(std::string name, const std::string& file);
	// add a blank texture
	size_t AddTex(std::string name, int xsize, int ysize, TextureType texType = RGBA32);

	void* AddGetTex(std::string name, int xsize, int ysize, TextureType texType = RGBA32) {
		const size_t idx = AddTex(std::move(name), xsize, ysize, texType);

		MemTex& tex = memTextures[idx];
		auto& mem = tex.mem;
		return (mem.data());
	}


	/**
	 * Creates the atlas containing all the specified textures.
	 * @return true if suceeded, false if not all textures did fit
	 *         into the specified maxsize.
	 */
	bool Finalize();

	/**
	 * @return a boolean true if the texture exists within
	 *         the "textures" map and false if it does not.
	 */
	bool TextureExists(const std::string& name);

	const spring::unordered_map<std::string, IAtlasAllocator::SAtlasEntry>& GetTextures() const;

	void ReloadTextures();
	void DumpTexture(const char* newFileName = nullptr) const;


	//! @return reference to the Texture struct of the specified texture
	AtlasedTexture& GetTexture(const std::string& name);

	// NOTE: safe with unordered_map after atlas has been finalized
	AtlasedTexture* GetTexturePtr(const std::string& name) { return &GetTexture(name); }

	/**
	 * @return a Texture struct of the specified texture if it exists,
	 *         otherwise return a backup texture.
	 */
	AtlasedTexture& GetTextureWithBackup(const std::string& name, const std::string& backupName);

	std::string GetTextureName(AtlasedTexture* tex);


	IAtlasAllocator* GetAllocator() { return atlasAllocator; }

	int2 GetSize() const;
	std::string GetName() const { return name; }

	uint32_t GetTexID() const { return atlasTexID; }
	const uint32_t GetTexTarget() const;

	int GetNumTexLevels() const;
	void SetMaxTexLevel(int maxLevels);

	void BindTexture();
	void SetFreeTexture(bool b) { freeTexture = b; }
	void SetName(const std::string& s) { name = s; }

	static void SetDebug(bool b) { debug = b; }
	static bool GetDebug() { return debug; }

protected:
	int GetBPP(TextureType texType) const {
		switch (texType) {
			case RGBA32: return 32;
			default: return 32;
		}
	}
	bool CreateTexture();

protected:
	uint32_t allocType;
	int32_t atlasSizeX;
	int32_t atlasSizeY;

	bool reloadable;

	IAtlasAllocator* atlasAllocator = nullptr;

	struct MemTex {
	public:
		MemTex(): xsize(0), ysize(0), texType(RGBA32) {}
		MemTex(const MemTex&) = delete;
		MemTex(MemTex&& t) { *this = std::move(t); }

		MemTex& operator = (const MemTex&) = delete;
		MemTex& operator = (MemTex&& t) {
			xsize = t.xsize;
			ysize = t.ysize;
			texType = t.texType;

			names = std::move(t.names);
			mem = std::move(t.mem);
			return *this;
		}

	public:
		int xsize;
		int ysize;

		TextureType texType;

		std::vector<std::string> names;
		std::vector<uint8_t> mem;
	};

	std::string name;

	// temporary storage of all textures
	std::vector<MemTex> memTextures;

	spring::unordered_map<std::string, size_t> files;
	spring::unordered_map<std::string, AtlasedTexture> textures;
	spring::unordered_map<AtlasedTexture*, std::string> texToName;  // non-creg serialization

	uint32_t atlasTexID = 0;

	bool initialized = false;
	bool freeTexture = true; // free texture on atlas destruction?

	// set to true to write finalized texture atlas to disk
	static inline bool debug = false;
};

#endif // TEXTURE_ATLAS_H
