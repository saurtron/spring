/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <array>
#include <cstring> // memcpy

#include "ColorMap.h"
#include "Bitmap.h"
#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "System/StringUtil.h"
#include "System/UnorderedMap.hpp"
#include "System/creg/STL_Map.h"

#include "System/Misc/TracyDefs.h"

CR_BIND(CColorMap, )
CR_REG_METADATA(CColorMap, (
	CR_MEMBER(xsize),
	CR_IGNORED(nxsize),
	CR_MEMBER(ysize),
	CR_IGNORED(map),

	CR_SERIALIZER(Serialize),
	CR_POSTLOAD(PostLoad)
))


static std::array<CColorMap, 2048 + 2> colorMapsCache;
static spring::unordered_map<std::string, CColorMap*> namedColorMaps;

static size_t numColorMaps = 0;


void CColorMap::InitStatic()
{
	RECOIL_DETAILED_TRACY_ZONE;
	namedColorMaps.clear();
	namedColorMaps.reserve(colorMapsCache.size() - 2);

	// reuse inner ColorMap vectors when reloading
	// colorMapsCache.fill({});

	for (CColorMap& cm: colorMapsCache) {
		cm.Clear();
	}

	numColorMaps = 0;
}

CColorMap* CColorMap::LoadFromBitmapFile(const std::string& fileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto fn = StringToLower(fileName);
	const auto it = namedColorMaps.find(fn);

	if (it != namedColorMaps.end())
		return it->second;

	// hand out a dummy if cache is full
	if (numColorMaps >= (colorMapsCache.size() - 2))
		return &colorMapsCache[colorMapsCache.size() - 2];

	colorMapsCache[numColorMaps] = {fileName};
	namedColorMaps[fn] = &colorMapsCache[numColorMaps];

	return &colorMapsCache[numColorMaps++];
}

CColorMap* CColorMap::LoadFromRawVector(const float* data, size_t size)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CColorMap& cm = colorMapsCache[colorMapsCache.size() - 1];

	cm.Clear();
	cm.Load(data, size);

	// slowish, but gets invoked by /reloadcegs via LoadFromDefString callback
	// need to do a cache lookup or numColorMaps quickly spirals out of control
	for (size_t i = 0; i < numColorMaps; i++) {
		if (colorMapsCache[i].map.size() != cm.map.size())
			continue;

		if (memcmp(colorMapsCache[i].map.data(), cm.map.data(), cm.map.size() * sizeof(SColor)) == 0)
			return &colorMapsCache[i];
	}

	// ditto
	if (numColorMaps >= (colorMapsCache.size() - 2))
		return &colorMapsCache[colorMapsCache.size() - 2];

	colorMapsCache[numColorMaps].Clear();
	colorMapsCache[numColorMaps].Load(data, size);

	return &colorMapsCache[numColorMaps++];
}


CColorMap* CColorMap::LoadFromDefString(const std::string& defString)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::array<float, 4096> vec;

	size_t idx = 0;

	char* pos = const_cast<char*>(defString.c_str());
	char* end = nullptr;

	vec.fill(0.0f);

	for (float val; (val = std::strtof(pos, &end), pos != end && idx < vec.size()); pos = end) {
		vec[idx++] = val;
	}

	if (idx == 0)
		return (CColorMap::LoadFromBitmapFile("bitmaps\\" + defString));

	return (CColorMap::LoadFromRawVector(vec.data(), idx));
}



CColorMap::CColorMap(const std::string& fileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBitmap bitmap;

	if (!bitmap.Load(fileName)) {
		bitmap.Alloc(2, 2, 4);
		LOG_L(L_WARNING, "[ColorMap] could not load texture from file \"%s\"", fileName.c_str());
	}

	if (bitmap.compressed || (bitmap.channels != 4) || (bitmap.xsize < 2))
		throw content_error("[ColorMap] unsupported bitmap format in file " + fileName);

	xsize  = bitmap.xsize;
	ysize  = bitmap.ysize;
	nxsize = xsize - 1;

	LoadMap(bitmap.GetRawMem(), xsize * ysize);
}


void CColorMap::Load(const float* data, size_t size)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (size < 8)
		throw content_error("[ColorMap] less than two RGBA colors specified");

	xsize  = (size - (size % 4)) / 4;
	ysize  = 1;
	nxsize = xsize - 1;

	static std::array<SColor, 4096> cmap; //to move it off the stack

	for (size_t i = 0, n = std::min(size_t(xsize), cmap.size()); i < n; ++i) {
		cmap[i] = SColor(&data[i * 4]);
	}

	LoadMap(&cmap[0].r, xsize);
}

void CColorMap::LoadMap(const unsigned char* buf, int num)
{
	RECOIL_DETAILED_TRACY_ZONE;
	map.clear();
	map.resize(num);

	std::memcpy(&map[0], buf, num * 4);
}

void CColorMap::GetColor(unsigned char* color, float pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto indices = GetIndices(pos);
	if (indices.first == size_t(-1)) {
		// dummy map, just return grey
		color[0] = 128;
		color[1] = 128;
		color[2] = 128;
		color[3] = 255;
		return;
	}

	const float fposn = pos * nxsize;
	const float fracn = fposn - indices.first;
	const auto aa = (int) (fracn * 256);
	const auto ia = 256 - aa;

	const auto* col1 = static_cast<uint8_t*>(map[indices.first ]);
	const auto* col2 = static_cast<uint8_t*>(map[indices.second]);

	color[0] = ((col1[0] * ia) + (col2[0] * aa)) >> 8;
	color[1] = ((col1[1] * ia) + (col2[1] * aa)) >> 8;
	color[2] = ((col1[2] * ia) + (col2[2] * aa)) >> 8;
	color[3] = ((col1[3] * ia) + (col2[3] * aa)) >> 8;
}

std::pair<size_t, size_t> CColorMap::GetIndices(float pos) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (map.empty())
		return std::make_pair(size_t(-1), size_t(-1));

	if (pos >= 1.0f)
		return std::make_pair(map.size() - 1, map.size() - 1);

	pos = std::max(pos, 0.0f);

	const float fposn = pos * nxsize;
	const size_t p = static_cast<size_t>(fposn);

	return std::make_pair(p, std::min(p + 1, map.size() - 1));
}

#ifdef USING_CREG
void CColorMap::SerializeColorMaps(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!s->IsWriting()) {
		for (CColorMap& cm: colorMapsCache) {
			cm.Clear();
		}
	}

	s->SerializeInt(&numColorMaps, sizeof(numColorMaps));
	for (size_t i = 0; i < numColorMaps; ++i) {
		s->SerializeObjectInstance(&colorMapsCache[i], CColorMap::StaticClass());
	}

	std::unique_ptr<creg::IType> mapType = creg::DeduceType<decltype(namedColorMaps)>::Get();
	mapType->Serialize(s, &namedColorMaps);
}

void CColorMap::PostLoad()
{
	RECOIL_DETAILED_TRACY_ZONE;
	nxsize = xsize - 1;
}

void CColorMap::Serialize(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!s->IsWriting())
		map.resize(xsize * ysize);

	s->Serialize(&map[0].r, xsize * ysize * 4);
}
#endif
