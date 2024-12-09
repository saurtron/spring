/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "PathingState.h"

#include "zlib.h"
#include "minizip/zip.h"

#include "Game/GlobalUnsynced.h"
#include "Game/LoadScreen.h"
#include "Net/Protocol/NetProtocol.h"

#include "Sim/Misc/ModInfo.h"
#include "Sim/MoveTypes/MoveDefHandler.h"
#include "Sim/MoveTypes/MoveMath/MoveMath.h"
#include "PathFinder.h"
#include "IPath.h"
#include "PathConstants.h"
#include "PathFinderDef.h"
#include "PathLog.h"
#include "Sim/Path/HAPFS/PathGlobal.h"
#include "PathMemPool.h"

#include "System/Config/ConfigHandler.h"
#include "System/FileSystem/Archives/IArchive.h"
#include "System/FileSystem/ArchiveLoader.h"
#include "System/FileSystem/DataDirsAccess.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/FileQueryFlags.h"
#include "System/Platform/Threading.h"
#include "System/StringUtil.h"
#include "System/Threading/ThreadPool.h" // for_mt

#include "System/Misc/TracyDefs.h"

#define ENABLE_NETLOG_CHECKSUM 1

static constexpr int BLOCK_UPDATE_DELAY_FRAMES = GAME_SPEED / 2;

namespace HAPFS {

bool TEST_ACTIVE = false;

static std::vector<PathNodeStateBuffer> nodeStateBuffers;
static size_t pathingStates = 0;

PCMemPool pcMemPool;
// PEMemPool peMemPool;

static const std::string GetPathCacheDir() {
	RECOIL_DETAILED_TRACY_ZONE;
	return (FileSystem::GetCacheDir() + FileSystemAbstraction::GetNativePathSeparator() + "paths" + FileSystemAbstraction::GetNativePathSeparator());
}

static const std::string GetCacheFileName(const std::string& fileHashCode, const std::string& peFileName, const std::string& mapFileName) {
	RECOIL_DETAILED_TRACY_ZONE;
	return (GetPathCacheDir() + mapFileName + "." + peFileName + "-" + fileHashCode + ".zip");
}

void PathingState::KillStatic() { pathingStates = 0; }

PathingState::PathingState()
{
	RECOIL_DETAILED_TRACY_ZONE;
	pathCache[0] = nullptr;
	pathCache[1] = nullptr;
}

void PathingState::Init(std::vector<IPathFinder*> pathFinderlist, PathingState* parentState, unsigned int _BLOCK_SIZE, const std::string& peFileName, const std::string& mapFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	BLOCK_SIZE = _BLOCK_SIZE;
	BLOCK_PIXEL_SIZE = BLOCK_SIZE * SQUARE_SIZE;

	{
		// 56 x 16 elms for QuickSilver
		mapDimensionsInBlocks.x = mapDims.mapx / BLOCK_SIZE;
		mapDimensionsInBlocks.y = mapDims.mapy / BLOCK_SIZE;
        mapBlockCount = mapDimensionsInBlocks.x * mapDimensionsInBlocks.y;

		// LOG("TK PathingState::Init X(%d) = mapx(%d) / blks(%d), Y(%d) = mapy(%d) / blks(%d)"
		// 	, mapDimensionsInBlocks.x
		// 	, mapDims.mapx
		// 	, BLOCK_SIZE
		// 	, mapDimensionsInBlocks.y
		// 	, mapDims.mapy
		// 	, BLOCK_SIZE
		// 	);

		nbrOfBlocks.x = mapDims.mapx / BLOCK_SIZE;
		nbrOfBlocks.y = mapDims.mapy / BLOCK_SIZE;

		instanceIndex = pathingStates++;
	}

	AllocStateBuffer();

	{
		RECOIL_DETAILED_TRACY_ZONE;
		pathFinders = pathFinderlist;
		BLOCKS_TO_UPDATE = (SQUARES_TO_UPDATE) / (BLOCK_SIZE * BLOCK_SIZE) + 1;

		blockUpdatePenalty = 0;
		nextOffsetMessageIdx = 0;
		nextCostMessageIdx = 0;

	 	pathChecksum = 0;
	 	fileHashCode = CalcHash(__func__);

		offsetBlockNum = {mapDimensionsInBlocks.x * mapDimensionsInBlocks.y};
		costBlockNum = {mapDimensionsInBlocks.x * mapDimensionsInBlocks.y};

		vertexCosts.clear();
		vertexCosts.resize(moveDefHandler.GetNumMoveDefs() * blockStates.GetSize() * PATH_DIRECTION_VERTICES, PATHCOST_INFINITY);
		maxSpeedMods.clear();
		maxSpeedMods.resize(moveDefHandler.GetNumMoveDefs(), 0.001f);

		updatedBlocks.clear();
		consumedBlocks.clear();
		offsetBlocksSortedByCost.clear();
	}

	PathingState*  childPE = this;
	PathingState* parentPE = parentState;

	if (parentPE != nullptr)
		parentPE->nextPathState = childPE;

	// precalc for FindBlockPosOffset()
	{
		offsetBlocksSortedByCost.reserve(BLOCK_SIZE * BLOCK_SIZE);
		for (unsigned int z = 0; z < BLOCK_SIZE; ++z) {
			for (unsigned int x = 0; x < BLOCK_SIZE; ++x) {
				const float dx = x - (float)(BLOCK_SIZE - 1) * 0.5f;
				const float dz = z - (float)(BLOCK_SIZE - 1) * 0.5f;
				const float cost = (dx * dx + dz * dz);

				offsetBlocksSortedByCost.emplace_back(cost, x, z);
			}
		}
		std::stable_sort(offsetBlocksSortedByCost.begin(), offsetBlocksSortedByCost.end(), [](const SOffsetBlock& a, const SOffsetBlock& b) {
			return (a.cost < b.cost);
		});
	}

	if (BLOCK_SIZE == LOWRES_PE_BLOCKSIZE) {
		assert(parentPE != nullptr);

		// calculate map-wide maximum positional speedmod for each MoveDef
		for_mt(0, moveDefHandler.GetNumMoveDefs(), [&](unsigned int i) {
			const MoveDef* md = moveDefHandler.GetMoveDefByPathType(i);

			for (int y = 0; y < mapDims.mapy; y++) {
				for (int x = 0; x < mapDims.mapx; x++) {
					childPE->maxSpeedMods[i] = std::max(childPE->maxSpeedMods[i], CMoveMath::GetPosSpeedMod(*md, x, y));
				}
			}
		});

		// calculate reciprocals, avoids divisions in TestBlock
		for (unsigned int i = 0; i < maxSpeedMods.size(); i++) {
			 childPE->maxSpeedMods[i] = 1.0f / childPE->maxSpeedMods[i];
			parentPE->maxSpeedMods[i] = childPE->maxSpeedMods[i];
		}
	}

	// load precalculated data if it exists
	InitEstimator(peFileName, mapFileName);
}

void PathingState::Terminate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (pathCache[0] != nullptr)
		pcMemPool.free(pathCache[0]);
	
	if (pathCache[1] != nullptr)
		pcMemPool.free(pathCache[1]);

	//LOG("Pathing unporcessed updatedBlocks is %llu", updatedBlocks.size());

	// Clear out lingering unprocessed map changes
	while (!updatedBlocks.empty()) {
		const int2& pos = updatedBlocks.front();
		const int idx = BlockPosToIdx(pos);
		updatedBlocks.pop_front();
		blockStates.nodeMask[idx] &= ~PATHOPT_OBSOLETE;
		blockStates.nodeLinksObsoleteFlags[idx] = 0;
	}

	// allow our PNSB to be reused across reloads
	if (instanceIndex < nodeStateBuffers.size())
		nodeStateBuffers[instanceIndex] = std::move(blockStates);
}

void PathingState::AllocStateBuffer()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (instanceIndex >= nodeStateBuffers.size())
		nodeStateBuffers.emplace_back();

	nodeStateBuffers[instanceIndex].Clear();
	nodeStateBuffers[instanceIndex].Resize(nbrOfBlocks, int2(mapDims.mapx, mapDims.mapy));

	// steal memory, returned in dtor
	blockStates = std::move(nodeStateBuffers[instanceIndex]);
}

bool PathingState::RemoveCacheFile(const std::string& peFileName, const std::string& mapFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (FileSystem::Remove(GetCacheFileName(IntToString(fileHashCode, "%x"), peFileName, mapFileName)));
}


void PathingState::InitEstimator(const std::string& peFileName, const std::string& mapFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const unsigned int numThreads = ThreadPool::GetNumThreads();
	//LOG("TK PathingState::InitEstimator: %d threads available", numThreads);

	// Not much point in multithreading these...
	InitBlocks();

	if (!ReadFile(peFileName, mapFileName)) {
		char calcMsg[512];
		const char* fmtStrs[4] = {
			"[%s] creating PE%u cache with %u PF threads",
			"[%s] creating PE%u cache with %u PF thread",
			"[%s] writing PE%u cache-file %s-%x",
			"[%s] written PE%u cache-file %s-%x",
		};

		{
			sprintf(calcMsg, fmtStrs[numThreads==1], __func__, BLOCK_SIZE, numThreads);
			loadscreen->SetLoadMessage(calcMsg);
		}

		// Mark block directions as dirty to ensure they get updated.
		auto& nodeFlags = blockStates.nodeLinksObsoleteFlags;
		std::for_each(nodeFlags.begin(), nodeFlags.end(), [](std::uint8_t& f){ f = PATH_DIRECTIONS_HALF_MASK; });

		// note: only really needed if numExtraThreads > 0
		spring::barrier pathBarrier(numThreads);

		for_mt(0, numThreads, [this, &pathBarrier](int i) {
			CalcOffsetsAndPathCosts(ThreadPool::GetThreadNum(), &pathBarrier);
		});

		std::for_each(nodeFlags.begin(), nodeFlags.end(), [](std::uint8_t& f){ f = 0; });

		sprintf(calcMsg, fmtStrs[2], __func__, BLOCK_SIZE, peFileName.c_str(), fileHashCode);
		loadscreen->SetLoadMessage(calcMsg, true);

		WriteFile(peFileName, mapFileName);

		sprintf(calcMsg, fmtStrs[3], __func__, BLOCK_SIZE, peFileName.c_str(), fileHashCode);
		loadscreen->SetLoadMessage(calcMsg, true);
	}

	// calculate checksum over block-offsets and vertex-costs
	pathChecksum = CalcChecksum();

	pathCache[0] = pcMemPool.alloc<CPathCache>(mapDimensionsInBlocks.x, mapDimensionsInBlocks.y);
	pathCache[1] = pcMemPool.alloc<CPathCache>(mapDimensionsInBlocks.x, mapDimensionsInBlocks.y);
}

void PathingState::InitBlocks()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// TK NOTE: moveDefHandler.GetNumMoveDefs() == 47
	blockStates.peNodeOffsets.resize(moveDefHandler.GetNumMoveDefs());
	for (unsigned int idx = 0; idx < moveDefHandler.GetNumMoveDefs(); idx++) {
		blockStates.peNodeOffsets[idx].resize(mapDimensionsInBlocks.x * mapDimensionsInBlocks.y);
		//LOG("TK PathingState::InitBlocks: blockStates.peNodeOffsets %d now %d", idx, blockStates.peNodeOffsets[idx].size());
	}
}


__FORCE_ALIGN_STACK__
void PathingState::CalcOffsetsAndPathCosts(unsigned int threadNum, spring::barrier* pathBarrier)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// reset FPU state for synced computations
	//streflop::streflop_init<streflop::Simple>();

	// NOTE: EstimatePathCosts() [B] is temporally dependent on CalculateBlockOffsets() [A],
	// A must be completely finished before B_i can be safely called. This means we cannot
	// let thread i execute (A_i, B_i), but instead have to split the work such that every
	// thread finishes its part of A before any starts B_i.
	const unsigned int maxBlockIdx = blockStates.GetSize() - 1;
	int i;

	while ((i = --offsetBlockNum) >= 0)
		CalculateBlockOffsets(maxBlockIdx - i, threadNum);

	pathBarrier->wait();

	while ((i = --costBlockNum) >= 0)
		EstimatePathCosts(maxBlockIdx - i, threadNum);
}

void PathingState::CalculateBlockOffsets(unsigned int blockIdx, unsigned int threadNum)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int2 blockPos = BlockIdxToPos(blockIdx);

	if (threadNum == 0 && blockIdx >= nextOffsetMessageIdx) {
		nextOffsetMessageIdx = blockIdx + blockStates.GetSize() / 16;
		clientNet->Send(CBaseNetProtocol::Get().SendCPUUsage(BLOCK_SIZE | (blockIdx << 8)));
	}

	for (unsigned int i = 0; i < moveDefHandler.GetNumMoveDefs(); i++) {
		const MoveDef* md = moveDefHandler.GetMoveDefByPathType(i);

		//LOG("TK PathingState::InitBlocks: blockStates.peNodeOffsets %d now %d looking up %d", i, blockStates.peNodeOffsets[md->pathType].size(), blockIdx);
		blockStates.peNodeOffsets[md->pathType][blockIdx] = FindBlockPosOffset(*md, blockPos.x, blockPos.y, threadNum);
		// LOG("UPDATED blockStates.peNodeOffsets[%d][%d] = (%d, %d) : (%d, %d)"
		// 		, md->pathType, blockIdx
		// 		, blockStates.peNodeOffsets[md->pathType][blockIdx].x, blockStates.peNodeOffsets[md->pathType][blockIdx].y
		// 		, blockPos.x, blockPos.y);
	}
}

/**
 * Move around the blockPos a bit, so we `surround` unpassable blocks.
 */
int2 PathingState::FindBlockPosOffset(const MoveDef& moveDef, unsigned int blockX, unsigned int blockZ, int threadNum) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// lower corner position of block
	const unsigned int lowerX = blockX * BLOCK_SIZE;
	const unsigned int lowerZ = blockZ * BLOCK_SIZE;
	const unsigned int blockArea = (BLOCK_SIZE * BLOCK_SIZE) / SQUARE_SIZE;

	int2 bestPos(lowerX + (BLOCK_SIZE >> 1), lowerZ + (BLOCK_SIZE >> 1));
	float bestCost = std::numeric_limits<float>::max();

	// same as above, but with squares sorted by their baseCost
	// s.t. we can exit early when a square exceeds our current
	// best (from testing, on avg. 40% of blocks can be skipped)
	for (const SOffsetBlock& ob: offsetBlocksSortedByCost) {
		if (ob.cost >= bestCost)
			break;

		const int2 blockPos(lowerX + ob.offset.x, lowerZ + ob.offset.y);
		const float speedMod = CMoveMath::GetPosSpeedMod(moveDef, blockPos.x, blockPos.y);

		//assert((blockArea / (0.001f + speedMod) >= 0.0f);
		const float cost = ob.cost + (blockArea / (0.001f + speedMod));

		if (cost >= bestCost)
			continue;

		if (!CMoveMath::IsBlockedStructure(moveDef, blockPos.x, blockPos.y, nullptr, threadNum)
				&& !moveDef.IsInExitOnly(blockPos.x, blockPos.y)) {
			bestCost = cost;
			bestPos  = blockPos;
		}
	}

	// return the offset found
	return bestPos;
}

void PathingState::EstimatePathCosts(unsigned int blockIdx, unsigned int threadNum)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int2 blockPos = BlockIdxToPos(blockIdx);

	if (threadNum == 0 && blockIdx >= nextCostMessageIdx) {
		nextCostMessageIdx = blockIdx + blockStates.GetSize() / 16;

		char calcMsg[128];
		sprintf(calcMsg, "[%s] precached %d of %d blocks", __func__, blockIdx, blockStates.GetSize());

		clientNet->Send(CBaseNetProtocol::Get().SendCPUUsage(0x1 | BLOCK_SIZE | (blockIdx << 8)));
		loadscreen->SetLoadMessage(calcMsg, (blockIdx != 0));
	}

	for (unsigned int i = 0; i < moveDefHandler.GetNumMoveDefs(); i++) {
		const MoveDef* md = moveDefHandler.GetMoveDefByPathType(i);

		CalcVertexPathCosts(*md, blockPos, threadNum);
	}
}

/**
 * Calculate costs of paths to all vertices connected from the given block
 */
void PathingState::CalcVertexPathCosts(const MoveDef& moveDef, int2 block, unsigned int threadNum)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// see GetBlockVertexOffset(); costs are bi-directional and only
	// calculated for *half* the outgoing edges (while costs for the
	// other four directions are stored at the adjacent vertices)
	auto idx = BlockPosToIdx(block);
	const uint8_t nodeLinksObsoleteFlags = blockStates.nodeLinksObsoleteFlags[idx]
								  		 & (moveDef.allowDirectionalPathing) ? PATH_DIRECTIONS_MASK : PATH_DIRECTIONS_HALF_MASK;

	int pathdir = 0;
	for (int checkBit = 1; checkBit <= PATHDIR_LEFT_DOWN_MASK; checkBit <<= 1, ++pathdir) {
		if (nodeLinksObsoleteFlags & checkBit)
			CalcVertexPathCost(moveDef, block, pathdir, threadNum);
	}
}

void PathingState::CalcVertexPathCost(
	const MoveDef& moveDef,
	int2 parentBlockPos,
	unsigned int pathDir,
	unsigned int threadNum
) {
	RECOIL_DETAILED_TRACY_ZONE;
	const int2 childBlockPos = parentBlockPos + PE_DIRECTION_VECTORS[pathDir];

	const unsigned int parentBlockIdx = BlockPosToIdx(parentBlockPos);
	const unsigned int  childBlockIdx = BlockPosToIdx( childBlockPos);

	// LOG("TK PathingState::CalcVertexPathCost parent (%d, %d) = %d (of %d), child (%d,%d) = %d (of %d)"
	// 		, parentBlockPos.x
	// 		, parentBlockPos.y
	// 		, parentBlockIdx
	// 		, mapDimensionsInBlocks.x
	// 		, childBlockPos.x
	// 		, childBlockPos.y
	// 		, childBlockIdx
	// 		, mapDimensionsInBlocks.y
	// 		);

	const unsigned int  vertexCostIdx =
		moveDef.pathType * mapBlockCount * PATH_DIRECTION_VERTICES +
		parentBlockIdx * PATH_DIRECTION_VERTICES +
		pathDir;

	// outside map?
	if ((unsigned)childBlockPos.x >= mapDimensionsInBlocks.x || (unsigned)childBlockPos.y >= mapDimensionsInBlocks.y) {
		vertexCosts[vertexCostIdx] = PATHCOST_INFINITY;
		return;
	}


	// start position within parent block, goal position within child block
	const int2 parentSquare = blockStates.peNodeOffsets[moveDef.pathType][parentBlockIdx];
	const int2  childSquare = blockStates.peNodeOffsets[moveDef.pathType][ childBlockIdx];

	const float3 startPos = SquareToFloat3(parentSquare.x, parentSquare.y);
	const float3  goalPos = SquareToFloat3( childSquare.x,  childSquare.y);

	// keep search exactly contained within the two blocks
	CRectangularSearchConstraint pfDef(startPos, goalPos, 0.0f, BLOCK_SIZE);

	// LOG("TK PathingState::CalcVertexPathCost: (%d,%d -> %d,%d) (%d,%d -> %d,%d [%d])"
	// 	, parentSquare.x, parentSquare.y
	// 	, childSquare.x,  childSquare.y
	// 	, pfDef.startSquareX, pfDef.startSquareZ
	// 	, pfDef.goalSquareX, pfDef.goalSquareZ
	// 	, BLOCK_SIZE);

	// we never want to allow searches from any blocked starting positions
	// (otherwise PE and PF can disagree), but are more lenient for normal
	// searches so players can "unstuck" units
	// note: PE itself should ensure this never happens to begin with?
	//
	// blocked goal positions are always early-outs (no searching needed)
	const bool strtBlocked = ((CMoveMath::IsBlocked(moveDef, startPos, nullptr, threadNum) & CMoveMath::BLOCK_STRUCTURE) != 0);
	const bool goalBlocked = pfDef.IsGoalBlocked(moveDef, CMoveMath::BLOCK_STRUCTURE, nullptr, threadNum);

	if (strtBlocked || goalBlocked) {
		vertexCosts[vertexCostIdx] = PATHCOST_INFINITY;
		return;
	}

	// find path from parent to child block
	pfDef.skipSubSearches = true;
	pfDef.testMobile      = false;
	pfDef.needPath        = false;
	pfDef.exactPath       = true;
	pfDef.dirIndependent  = true;

	IPath::Path path;
	IPath::SearchResult result = pathFinders[threadNum]->GetPath(moveDef, pfDef, nullptr, startPos, path, MAX_SEARCHED_NODES_PF >> 2);

	// store the result
	if (result == IPath::Ok) {
		vertexCosts[vertexCostIdx] = path.pathCost;
	} else {
		vertexCosts[vertexCostIdx] = PATHCOST_INFINITY;
	}
}


/**
 * Try to read offset and vertices data from file, return false on failure
 */
bool PathingState::ReadFile(const std::string& peFileName, const std::string& mapFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const std::string hashHexString = IntToString(fileHashCode, "%x");
	const std::string cacheFileName = GetCacheFileName(hashHexString, peFileName, mapFileName);

	LOG("[PathEstimator::%s] hash=%s file=\"%s\" (exists=%d)", __func__, hashHexString.c_str(), cacheFileName.c_str(), FileSystem::FileExists(cacheFileName));

	if (!FileSystem::FileExists(cacheFileName))
		return false;

	std::unique_ptr<IArchive> upfile(archiveLoader.OpenArchive(dataDirsAccess.LocateFile(cacheFileName), "sdz"));

	if (upfile == nullptr || !upfile->IsOpen()) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	char calcMsg[512];
	sprintf(calcMsg, "Reading Estimate PathCosts [%d]", BLOCK_SIZE);
	loadscreen->SetLoadMessage(calcMsg);

	const unsigned fid = upfile->FindFile("pathinfo");
	if (fid >= upfile->NumFiles()) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	std::vector<std::uint8_t> buffer;

	if (!upfile->GetFile(fid, buffer) || buffer.size() < 4) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	const unsigned int filehash = *(reinterpret_cast<unsigned int*>(&buffer[0]));
	const unsigned int blockSize = blockStates.GetSize() * sizeof(short2);
	unsigned int pos = sizeof(unsigned);

	if (filehash != fileHashCode) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	if (buffer.size() < (pos + blockSize * moveDefHandler.GetNumMoveDefs())) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	// read center-offset data
	for (int pathType = 0; pathType < moveDefHandler.GetNumMoveDefs(); ++pathType) {
		std::memcpy(&blockStates.peNodeOffsets[pathType][0], &buffer[pos], blockSize);
		pos += blockSize;
	}

	// read vertex-cost data
	if (buffer.size() < (pos + vertexCosts.size() * sizeof(float))) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	std::memcpy(&vertexCosts[0], &buffer[pos], vertexCosts.size() * sizeof(float));
	return true;
}


/**
 * Try to write offset and vertex data to file.
 */
bool PathingState::WriteFile(const std::string& peFileName, const std::string& mapFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// we need this directory to exist
	if (!FileSystem::CreateDirectory(GetPathCacheDir()))
		return false;

	const std::string hashHexString = IntToString(fileHashCode, "%x");
	const std::string cacheFileName = GetCacheFileName(hashHexString, peFileName, mapFileName);

	LOG("[PathEstimator::%s] hash=%s file=\"%s\" (exists=%d)", __func__, hashHexString.c_str(), cacheFileName.c_str(), FileSystem::FileExists(cacheFileName));

	// open file for writing in a suitable location
	zipFile file = zipOpen(dataDirsAccess.LocateFile(cacheFileName, FileQueryFlags::WRITE).c_str(), APPEND_STATUS_CREATE);

	if (file == nullptr)
		return false;

	zipOpenNewFileInZip(file, "pathinfo", nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_BEST_COMPRESSION);

	// write hash-code (NOTE: this also affects the CRC!)
	zipWriteInFileInZip(file, (const void*) &fileHashCode, 4);

	// write center-offsets
	for (int pathType = 0; pathType < moveDefHandler.GetNumMoveDefs(); ++pathType) {
		zipWriteInFileInZip(file, (const void*) &blockStates.peNodeOffsets[pathType][0], blockStates.peNodeOffsets[pathType].size() * sizeof(short2));
	}

	// write vertex-costs
	zipWriteInFileInZip(file, vertexCosts.data(), vertexCosts.size() * sizeof(float));

	zipCloseFileInZip(file);
	zipClose(file, nullptr);


	// get the CRC over the written path data
	std::unique_ptr<IArchive> upfile(archiveLoader.OpenArchive(dataDirsAccess.LocateFile(cacheFileName), "sdz"));

	if (upfile == nullptr || !upfile->IsOpen()) {
		FileSystem::Remove(cacheFileName);
		return false;
	}

	assert(upfile->FindFile("pathinfo") < upfile->NumFiles());
	return true;
}


/**
 * Update some obsolete blocks using the FIFO-principle
 */
void PathingState::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	pathCache[0]->Update();
	pathCache[1]->Update();

	//LOG("PathingState::Update %d", BLOCK_SIZE);

	const unsigned int numMoveDefs = moveDefHandler.GetNumMoveDefs();

	if (numMoveDefs == 0)
		return;

	if (updatedBlocks.empty())
		return;

	// determine how many blocks we should update
	int blocksToUpdate = 0;
	{
		const int progressiveUpdates = std::ceil(updatedBlocks.size() * (1.f / (BLOCKS_TO_UPDATE<<2)) * modInfo.pfUpdateRateScale);
		const int MIN_BLOCKS_TO_UPDATE = 1;
		const int MAX_BLOCKS_TO_UPDATE = std::max<int>(BLOCKS_TO_UPDATE >> 1, MIN_BLOCKS_TO_UPDATE);

		blocksToUpdate = std::clamp(progressiveUpdates, MIN_BLOCKS_TO_UPDATE, MAX_BLOCKS_TO_UPDATE) * numMoveDefs;
	
		// LOG("[%d] blocksToUpdate=%d progressiveUpdates=%d [%f]"
		// 		, BLOCK_SIZE, blocksToUpdate, progressiveUpdates, modInfo.pfUpdateRateScale);
	}

	//LOG("PathingState::Update blocksToUpdate %d", blocksToUpdate);

	if (blocksToUpdate == 0)
		return;

	//LOG("PathingState::Update updatedBlocks.empty == %d", (int)updatedBlocks.empty());
	//LOG("PathingState::Update updatedBlocksDelayActive %d", (int)updatedBlocksDelayActive);

	UpdateVertexPathCosts(blocksToUpdate);
}

void PathingState::UpdateVertexPathCosts(int blocksToUpdate)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const unsigned int numMoveDefs = moveDefHandler.GetNumMoveDefs();

	if (numMoveDefs == 0)
		return;

	if (blocksToUpdate == -1)
		blocksToUpdate = updatedBlocks.size() * numMoveDefs;

	int consumeBlocks = int(blocksToUpdate != 0) * int(ceil(float(blocksToUpdate) / numMoveDefs)) * numMoveDefs;

	consumedBlocks.clear();
	consumedBlocks.reserve(consumeBlocks);

	//LOG("PathingState::Update %d", updatedBlocks.size());

	std::vector<int> blockIds;
	blockIds.reserve(updatedBlocks.size());

	// get blocks to update
	while (!updatedBlocks.empty()) {
		const int2& pos = updatedBlocks.front();
		const int idx = BlockPosToIdx(pos);

		if ((blockStates.nodeMask[idx] & PATHOPT_OBSOLETE) == 0) {
			updatedBlocks.pop_front();
			continue;
		}

		if (consumedBlocks.size() >= blocksToUpdate)
			break;

		// issue repathing for all active movedefs
		for (unsigned int i = 0; i < numMoveDefs; i++) {
			const MoveDef* md = moveDefHandler.GetMoveDefByPathType(i);

			consumedBlocks.emplace_back(pos, md);
			//LOG("TK PathingState::Update: moveDef = %d %p (%p)", consumedBlocks.size(), &consumedBlocks.back(), consumedBlocks.back().moveDef);
		}

		updatedBlocks.pop_front(); // must happen _after_ last usage of the `pos` reference!
		blockStates.nodeMask[idx] &= ~PATHOPT_OBSOLETE;
		blockIds.emplace_back(idx);
	}

	// FindOffset (threadsafe)
	{
		SCOPED_TIMER("Sim::Path::Estimator::FindOffset");

		auto updateOffset = [&](const int n) {
				// copy the next block in line
				const SingleBlock sb = consumedBlocks[n];
				const int blockN = BlockPosToIdx(sb.blockPos);
				const MoveDef* currBlockMD = sb.moveDef;
				blockStates.peNodeOffsets[currBlockMD->pathType][blockN] = FindBlockPosOffset(*currBlockMD, sb.blockPos.x, sb.blockPos.y, ThreadPool::GetThreadNum());
			};

		for_mt(0, consumedBlocks.size(), updateOffset);
	}

	{
		SCOPED_TIMER("Sim::Path::Estimator::CalcVertexPathCosts");
		std::atomic<std::int64_t> updateCostBlockNum = consumedBlocks.size();
		const size_t threadsUsed = std::min(consumedBlocks.size(), (size_t)ThreadPool::GetNumThreads());

		auto updateVertexPathCosts = [this, &updateCostBlockNum](int threadNum){
				std::int64_t n;
				while ((n = --updateCostBlockNum) >= 0){
					//LOG("TK PathingState::Update: PROC moveDef = %d %p (%p)", n, &consumedBlocks[n], consumedBlocks[n].moveDef);
					CalcVertexPathCosts(*consumedBlocks[n].moveDef, consumedBlocks[n].blockPos, threadNum);
				}
			};

		for_mt(0, threadsUsed, updateVertexPathCosts);
	}

	std::for_each(blockIds.begin(), blockIds.end(), [this](int idx){ blockStates.nodeLinksObsoleteFlags[idx] = 0; });
}


/**
 * Mark affected blocks as obsolete
 */
void PathingState::MapChanged(unsigned int x1, unsigned int z1, unsigned int x2, unsigned z2)
{
	assert(x2 >= x1);
	assert(z2 >= z1);

	const int lowerX = int(x1 / BLOCK_SIZE) - 1;
	const int upperX = int(x2 / BLOCK_SIZE) + 1;
	const int lowerZ = int(z1 / BLOCK_SIZE) - 1;
	const int upperZ = int(z2 / BLOCK_SIZE) + 0;

	// find the upper and lower corner of the rectangular area
	const int startX = std::clamp(lowerX, 0, int(mapDimensionsInBlocks.x - 1));
	const int endX   = std::clamp(upperX, 0, int(mapDimensionsInBlocks.x - 1));
	const int startZ = std::clamp(lowerZ, 0, int(mapDimensionsInBlocks.y - 1));
	const int endZ   = std::clamp(upperZ, 0, int(mapDimensionsInBlocks.y - 1));

	bool pathingDirectional = pathManager->AllowDirectionalPathing();

	// LOG("%s: clamped to [%d, %d] -> [%d, %d]", __func__, lowerX, lowerZ, upperX, upperZ);

	constexpr uint32_t ALL_LINKS = PATH_DIRECTIONS_MASK;
	constexpr uint32_t MASK_REMOVE_LEFT = ~(PATHDIR_LEFT_MASK | PATHDIR_LEFT_UP_MASK | PATHDIR_LEFT_DOWN_MASK);
	constexpr uint32_t MASK_REMOVE_RIGHT = ~(PATHDIR_RIGHT_MASK | PATHDIR_RIGHT_UP_MASK| PATHDIR_RIGHT_DOWN_MASK);
	constexpr uint32_t MASK_REMOVE_UP = ~(PATHDIR_UP_MASK | PATHDIR_LEFT_UP_MASK | PATHDIR_RIGHT_UP_MASK);
	constexpr uint32_t MASK_REMOVE_DOWN = ~(PATHDIR_DOWN_MASK | PATHDIR_LEFT_DOWN_MASK | PATHDIR_RIGHT_DOWN_MASK);

	constexpr uint32_t activeLinks[] = {
		ALL_LINKS & MASK_REMOVE_LEFT  & MASK_REMOVE_UP,
		ALL_LINKS                     & MASK_REMOVE_UP,
		ALL_LINKS & MASK_REMOVE_RIGHT & MASK_REMOVE_UP,
		ALL_LINKS & MASK_REMOVE_LEFT,
		ALL_LINKS,
		ALL_LINKS & MASK_REMOVE_RIGHT,
		ALL_LINKS & MASK_REMOVE_LEFT  & MASK_REMOVE_DOWN,
		ALL_LINKS                     & MASK_REMOVE_DOWN,
		ALL_LINKS & MASK_REMOVE_RIGHT & MASK_REMOVE_DOWN,
	};

	auto getIdxFromZ = [&](int z){
			if (z == lowerZ) return 0;
			else if (z == upperZ) return 6;
			else return 3;
	};
	auto getIdxFromX = [&](int x){
			if (x == lowerX) return 0;
			else if (x == upperX) return 2;
			else return 1;
	};

	// mark the blocks inside the rectangle, enqueue them
	// from upper to lower because of the placement of the
	// bi-directional vertices
	for (int z = endZ; z >= startZ; z--) {
		for (int x = endX; x >= startX; x--) {
			const int idx = BlockPosToIdx(int2(x, z));
			std::uint8_t blockOrigLinkFlags = blockStates.nodeLinksObsoleteFlags[idx];

			uint8_t linkType = getIdxFromZ(z) + getIdxFromX(x);
			blockStates.nodeLinksObsoleteFlags[idx] = uint8_t(activeLinks[linkType]);
			if (!pathingDirectional) 
				blockStates.nodeLinksObsoleteFlags[idx] &= PATH_DIRECTIONS_HALF_MASK;

			if (blockStates.nodeLinksObsoleteFlags[idx] == blockOrigLinkFlags)
				continue;

			//if ((blockStates.nodeMask[idx] & PATHOPT_OBSOLETE) != 0)
			//	continue;

			//LOG("%s: [%d, %d] lower is %02x", __func__, x, z, blockOrigLinkFlags);
			//LOG("%s: clamped to [%d, %d] -> [%d, %d]", __func__, lowerX, lowerZ, upperX, upperZ);
			//LOG("%s: [%d, %d] result is %02x", __func__, x, z, blockStates.nodeLinksObsoleteFlags[idx]);

			if (blockOrigLinkFlags != 0)
				continue;

			updatedBlocks.emplace_back(x, z);
			blockStates.nodeMask[idx] |= PATHOPT_OBSOLETE;
		}
	}
}


std::uint32_t PathingState::CalcChecksum() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::uint32_t chksum = 0;
	std::uint64_t nbytes = vertexCosts.size() * sizeof(float);
	std::uint64_t offset = 0;

	#if (ENABLE_NETLOG_CHECKSUM == 1)
	std::array<char, 128 + sha512::SHA_LEN * 2 + 1> msgBuffer;

	sha512::hex_digest hexChars;
	sha512::raw_digest shaBytes;
	sha512::msg_vector rawBytes;
	#endif

	#if (ENABLE_NETLOG_CHECKSUM == 1)
	for (const auto& pathTypeOffsets: blockStates.peNodeOffsets) {
		nbytes += (pathTypeOffsets.size() * sizeof(short2));
	}

	rawBytes.clear();
	rawBytes.resize(nbytes);

	for (const auto& pathTypeOffsets: blockStates.peNodeOffsets) {
		nbytes = pathTypeOffsets.size() * sizeof(short2);
		offset += nbytes;

		std::memcpy(&rawBytes[offset - nbytes], pathTypeOffsets.data(), nbytes);
	}

	// for (int i=0; i<blockStates.peNodeOffsets.size(); i++){
	// 	for (int j =0; j<blockStates.peNodeOffsets[i].size(); j++){
	// 		LOG("blockStates.peNodeOffsets[%d][%d] = (%d %d)", i, j
	// 		, blockStates.peNodeOffsets[i][j].x
	// 		, blockStates.peNodeOffsets[i][j].y);
	// 	}
	// }

	{
		nbytes = vertexCosts.size() * sizeof(float);
		offset += nbytes;

		std::memcpy(&rawBytes[offset - nbytes], vertexCosts.data(), nbytes);

		sha512::calc_digest(rawBytes, shaBytes); // hash(offsets|costs)
		sha512::dump_digest(shaBytes, hexChars); // hexify(hash)

		SNPRINTF(msgBuffer.data(), msgBuffer.size(), "[PE::%s][BLK_SIZE=%d][SHA_DATA=%s]", __func__, BLOCK_SIZE, hexChars.data());
		CLIENT_NETLOG(gu->myPlayerNum, LOG_LEVEL_INFO, msgBuffer.data());
	}
	#endif

	// make path-estimator checksum part of synced state s.t. when
	// a client has a corrupted or stale cache it desyncs from the
	// start, not minutes later
	for (size_t i = 0, n = shaBytes.size() / 4; i < n; i += 1) {
		const uint16_t hi = (shaBytes[i * 4 + 0] << 8) | (shaBytes[i * 4 + 1] << 0);
		const uint16_t lo = (shaBytes[i * 4 + 2] << 8) | (shaBytes[i * 4 + 3] << 0);

		const SyncedUint su = (hi << 16) | (lo << 0);

		// copy first four bytes to reduced checksum
		if (chksum == 0)
			chksum = su;
	}

	return chksum;
}

// CPathCache::CacheItem PathingState::GetCache(const int2 strtBlock, const int2 goalBlock, float goalRadius, int pathType, const bool synced) const
// {
// 	const std::lock_guard<std::mutex> lock(cacheAccessLock);
// 	return pathCache[synced]->GetCachedPath(strtBlock, goalBlock, goalRadius, pathType);
// }

void PathingState::AddCache(const IPath::Path* path, const IPath::SearchResult result, const int2 strtBlock, const int2 goalBlock, float goalRadius, int pathType, const bool synced)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const std::lock_guard<std::mutex> lock(cacheAccessLock);
	pathCache[synced]->AddPath(path, result, strtBlock, goalBlock, goalRadius, pathType);
}

void PathingState::AddPathForCurrentFrame(const IPath::Path* path, const IPath::SearchResult result, const int2 strtBlock, const int2 goalBlock, float goalRadius, int pathType, const bool synced)
{
	RECOIL_DETAILED_TRACY_ZONE;
	//const std::lock_guard<std::mutex> lock(cacheAccessLock);
	//pathCache[synced]->AddPathForCurrentFrame(path, result, strtBlock, goalBlock, goalRadius, pathType);
}

void PathingState::PromotePathForCurrentFrame(
		const IPath::Path* path,
		const IPath::SearchResult result,
		const float3 startPosition,
		const float3 goalPosition,
		float goalRadius,
		int pathType,
		const bool synced
	)
{
	RECOIL_DETAILED_TRACY_ZONE;
	int2 strtBlock = {int(startPosition.x / BLOCK_PIXEL_SIZE), int(startPosition.z / BLOCK_PIXEL_SIZE)};;
	int2 goalBlock = {int(goalPosition.x / BLOCK_PIXEL_SIZE), int(goalPosition.z / BLOCK_PIXEL_SIZE)};

	pathCache[synced]->AddPath(path, result, strtBlock, goalBlock, goalRadius, pathType);
}

std::uint32_t PathingState::CalcHash(const char* caller) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const unsigned int hmChecksum = readMap->CalcHeightmapChecksum();
	const unsigned int tmChecksum = readMap->CalcTypemapChecksum();
	const unsigned int mdChecksum = moveDefHandler.GetCheckSum();
	const unsigned int peHashCode = (hmChecksum + tmChecksum + mdChecksum + BLOCK_SIZE + PATHESTIMATOR_VERSION);

	LOG("[PathingState::%s][%s] BLOCK_SIZE=%u", __func__, caller, BLOCK_SIZE);
	LOG("[PathingState::%s][%s] PATHESTIMATOR_VERSION=%u", __func__, caller, PATHESTIMATOR_VERSION);
	LOG("[PathingState::%s][%s] heightMapChecksum=%x", __func__, caller, hmChecksum);
	LOG("[PathingState::%s][%s] typeMapChecksum=%x", __func__, caller, tmChecksum);
	LOG("[PathingState::%s][%s] moveDefChecksum=%x", __func__, caller, mdChecksum);
	LOG("[PathingState::%s][%s] estimatorHashCode=%x", __func__, caller, peHashCode);

	return peHashCode;
}

}