/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BASE_BUILDER_BEHAVIOUR_H
#define BASE_BUILDER_BEHAVIOUR_H

#include "Behaviour.h"

#include "Sim/Misc/NanoPieceCache.h"
#include "System/float3.h"

class CBaseBuilderBehaviour : public CBehaviour
{
public:
	CR_DECLARE(CBaseBuilderBehaviour)

	CBaseBuilderBehaviour();
	CBaseBuilderBehaviour(CUnit* owner);

	virtual void PreInit(const UnitLoadParams& params) override;
	void CreateNanoParticle(const float3& goal, float radius, bool inverse, bool highPriority = false);

	float buildSpeed;
	CUnit* curBuild;
	bool range3D; ///< spheres instead of infinite cylinders for range tests
	NanoPieceCache nanoPieceCache;

	const NanoPieceCache& GetNanoPieceCache() const { return nanoPieceCache; }
	      NanoPieceCache& GetNanoPieceCache()       { return nanoPieceCache; }

	virtual void UpdatePre() override;

	inline float f3Dist(const float3& a, const float3& b) const {
		return range3D ? a.distance(b) : a.distance2D(b);
	}
	inline float f3SqDist(const float3& a, const float3& b) const {
		return range3D ? a.SqDistance(b) : a.SqDistance2D(b);
	}
	inline float f3Len(const float3& a) const {
		return range3D ? a.Length() : a.Length2D();
	}
	inline float f3SqLen(const float3& a) const {
		return range3D ? a.SqLength() : a.SqLength2D();
	}
};

#endif // BASE_BUILDER_BEHAVIOUR_H
