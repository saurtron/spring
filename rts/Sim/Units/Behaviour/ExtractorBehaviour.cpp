/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Used for all metal-extractors.
// Handles the metal-make-process.

#include "ExtractorBehaviour.h"

#include "Sim/Units/Scripts/UnitScript.h"
#include "Sim/Units/UnitHandler.h"
#include "Map/ReadMap.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Map/MetalMap.h"
#include "Sim/Misc/QuadField.h"
#include "System/ContainerUtil.h"

#include "System/Misc/TracyDefs.h"

//#include "Sim/Units/Unit.h"

template CExtractorBehaviour* CUnit::GetBehaviour<CExtractorBehaviour>() const;

CR_BIND_DERIVED(CExtractorBehaviour, CBehaviour, )

CR_REG_METADATA(CExtractorBehaviour, (
	CR_MEMBER(extractionRange),
	CR_MEMBER(extractionDepth),
	CR_MEMBER(metalAreaOfControl),
	CR_MEMBER(neighbours)
))

CR_BIND(CExtractorBehaviour::MetalSquareOfControl, )

CR_REG_METADATA_SUB(CExtractorBehaviour, MetalSquareOfControl, (
	CR_MEMBER(x),
	CR_MEMBER(z),
	CR_MEMBER(extractionDepth)
))

// TODO: How are class statics incorporated into creg?
float CExtractorBehaviour::maxExtractionRange = 0.0f;

CExtractorBehaviour::CExtractorBehaviour():
	CBehaviour(),
	extractionRange(0.0f),
	extractionDepth(0.0f)
{
}

CExtractorBehaviour::CExtractorBehaviour(CUnit* owner):
	CBehaviour(owner),
	extractionRange(0.0f),
	extractionDepth(0.0f)
{
}

CExtractorBehaviour::~CExtractorBehaviour()
{
	ResetExtraction();
}

void CExtractorBehaviour::PreInit(const UnitLoadParams& params)
{
	LOG("CExtractorBehaviour::PreInit");
	extractionRange = owner->unitDef->extractRange;
	extractionDepth = owner->unitDef->extractsMetal;
}

/* resets the metalMap and notifies the neighbours */
void CExtractorBehaviour::ResetExtraction()
{
	const auto script = owner->script;
	auto& metalExtract = owner->metalExtract;
	metalExtract = 0;
	script->ExtractionRateChanged(metalExtract);

	// undo the extraction-area
	for (auto si = metalAreaOfControl.begin(); si != metalAreaOfControl.end(); ++si) {
		metalMap.RemoveExtraction(si->x, si->z, si->extractionDepth);
	}

	metalAreaOfControl.clear();

	// tell the neighbours (if any) to take it over
	for (CExtractorBehaviour* ngb: neighbours) {
		ngb->RemoveNeighbour(this);
		ngb->ReCalculateMetalExtraction();
	}
	neighbours.clear();
}



/* determine if two extraction areas overlap */
bool CExtractorBehaviour::IsNeighbour(CExtractorBehaviour* other)
{
	// circle vs. circle
	return (owner->pos.SqDistance2D(other->owner->pos) < Square(extractionRange + other->extractionRange));
}

/* sets the range of extraction for this extractor, also finds overlapping neighbours. */
void CExtractorBehaviour::SetExtractionRangeAndDepth(float range, float depth)
{
	const auto script = owner->script;
	const auto& pos = owner->pos;
	auto& metalExtract = owner->metalExtract;
	extractionRange = std::max(range, 0.001f);
	extractionDepth = std::max(depth, 0.0f);
	maxExtractionRange = std::max(extractionRange, maxExtractionRange);

	// find any neighbouring extractors
	QuadFieldQuery qfQuery;
	quadField.GetUnits(qfQuery, pos, extractionRange + maxExtractionRange);

	for (CUnit* u: *qfQuery.units) {
		if (u == owner)
			continue;

		CExtractorBehaviour* eb = u->GetBehaviour<CExtractorBehaviour>();
		if (!eb)
			continue;

		if (!IsNeighbour(eb))
			continue;

		this->AddNeighbour(eb);
		eb->AddNeighbour(this);
	}

	if (!owner->activated) {
		assert(metalExtract == 0); // when deactivated metalExtract should always be 0

		return;
	}

	// calculate this extractor's area of control and metalExtract amount
	metalExtract = 0;

	const int xBegin = std::max(                   0, (int) ((pos.x - extractionRange) / METAL_MAP_SQUARE_SIZE));
	const int xEnd   = std::min(mapDims.mapx / 2 - 1, (int) ((pos.x + extractionRange) / METAL_MAP_SQUARE_SIZE));
	const int zBegin = std::max(                   0, (int) ((pos.z - extractionRange) / METAL_MAP_SQUARE_SIZE));
	const int zEnd   = std::min(mapDims.mapy / 2 - 1, (int) ((pos.z + extractionRange) / METAL_MAP_SQUARE_SIZE));

	metalAreaOfControl.reserve((xEnd - xBegin + 1) * (zEnd - zBegin + 1));

	// go through the whole (x, z)-square
	for (int x = xBegin; x <= xEnd; x++) {
		for (int z = zBegin; z <= zEnd; z++) {
			// center of metalsquare at (x, z)
			const float3 msqrPos((x + 0.5f) * METAL_MAP_SQUARE_SIZE, pos.y,
													 (z + 0.5f) * METAL_MAP_SQUARE_SIZE);
			const float sqrCenterDistance = msqrPos.SqDistance2D(owner->pos);

			if (sqrCenterDistance < Square(extractionRange)) {
				MetalSquareOfControl msqr;
				msqr.x = x;
				msqr.z = z;
				// extraction is done in a cylinder of height <depth>
				msqr.extractionDepth = metalMap.RequestExtraction(x, z, depth);
				metalAreaOfControl.push_back(msqr);
				metalExtract += msqr.extractionDepth * metalMap.GetMetalAmount(msqr.x, msqr.z);
			}
		}
	}

	// set the COB animation speed
	script->ExtractionRateChanged(metalExtract);
}


/* adds a neighbour for this extractor */
void CExtractorBehaviour::AddNeighbour(CExtractorBehaviour* neighbour)
{
	assert(neighbour != this);
	spring::VectorInsertUnique(neighbours, neighbour, true);
}

/* removes a neighbour for this extractor */
void CExtractorBehaviour::RemoveNeighbour(CExtractorBehaviour* neighbour)
{
	assert(neighbour != this);
	spring::VectorErase(neighbours, neighbour);
}


/* recalculate metalExtract for this extractor (eg. when a neighbour dies) */
void CExtractorBehaviour::ReCalculateMetalExtraction()
{
	auto& metalExtract = owner->metalExtract;
	const auto script = owner->script;
	metalExtract = 0;

	for (MetalSquareOfControl& msqr: metalAreaOfControl) {
		metalMap.RemoveExtraction(msqr.x, msqr.z, msqr.extractionDepth);

		if (owner->activated) {
			// extraction is done in a cylinder
			msqr.extractionDepth = metalMap.RequestExtraction(msqr.x, msqr.z, extractionDepth);
			metalExtract += (msqr.extractionDepth * metalMap.GetMetalAmount(msqr.x, msqr.z));
		}
	}

	// set the new rotation-speed
	script->ExtractionRateChanged(metalExtract);
}


void CExtractorBehaviour::Activate()
{
	/* Finds the amount of metal to extract and sets the rotationspeed when the extractor is built. */
	SetExtractionRangeAndDepth(extractionRange, extractionDepth);
}


void CExtractorBehaviour::Deactivate()
{
	ResetExtraction();
}
