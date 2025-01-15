/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef EXTRACTOR_BEHAVIOUR_H
#define EXTRACTOR_BEHAVIOUR_H

#include "Behaviour.h"

#include "System/float3.h"

class CExtractorBehaviour : public CBehaviour
{
public:
	CR_DECLARE(CExtractorBehaviour)
	CR_DECLARE_SUB(MetalSquareOfControl)

	CExtractorBehaviour();
	CExtractorBehaviour(CUnit* owner);

	~CExtractorBehaviour();

	void PreInit(const UnitLoadParams& params) override;

	void ResetExtraction();
	void SetExtractionRangeAndDepth(float range, float depth);
	void ReCalculateMetalExtraction();
	bool IsNeighbour(CExtractorBehaviour* neighbour);
	void AddNeighbour(CExtractorBehaviour* neighbour);
	void RemoveNeighbour(CExtractorBehaviour* neighbour);

	float GetExtractionRange() const { return extractionRange; }
	float GetExtractionDepth() const { return extractionDepth; }

	void Activate() override;
	void Deactivate() override;

protected:
	struct MetalSquareOfControl {
		CR_DECLARE_STRUCT(MetalSquareOfControl)
		int x;
		int z;
		float extractionDepth;
	};

	float extractionRange, extractionDepth;
	std::vector<MetalSquareOfControl> metalAreaOfControl;
	std::vector<CExtractorBehaviour*> neighbours;

	static float maxExtractionRange;

};

#endif // EXTRACTOR_BEHAVIOUR_H
