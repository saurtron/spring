/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef NANO_PROJECTILE_H
#define NANO_PROJECTILE_H

#include "Sim/Projectiles/Projectile.h"
#include "System/Color.h"

class CNanoProjectile : public CProjectile
{
	CR_DECLARE_DERIVED(CNanoProjectile)

public:
	CNanoProjectile();
	CNanoProjectile(float3 pos, float3 speed, int lifeTime, SColor color);
	~CNanoProjectile();

	void Update() override;
	void Draw() override;
	void DrawOnMinimap() const override;

	int GetProjectilesCount() const override;

	static bool GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo);

private:
	float rotAcc = 0.0f;

	float rotVal0x = 0.0f;
	float rotVel0x = 0.0f;
	float rotAcc0x = 0.0f;
public:
	static inline float rotVal0 = 0.0f;
	static inline float rotVel0 = 0.0f;
	static inline float rotAcc0 = 0.0f;
	static inline float rotValRng0 = 0.0f;
	static inline float rotVelRng0 = 0.0f;
	static inline float rotAccRng0 = 0.0f;

	int deathFrame;
	SColor color;
};

#endif /* NANO_PROJECTILE_H */

