/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef I_SKY_H
#define I_SKY_H

#include "SkyLight.h"
#include <memory>
#include <string>

struct MapTextureData;
class ISky
{
protected:
	ISky();

public:
	virtual ~ISky();

	virtual void Update() = 0;
	virtual void UpdateSunDir() = 0;
	virtual void UpdateSkyTexture() = 0;

	virtual void Draw() = 0;

	virtual void SetLuaTexture(const MapTextureData& td) {}

	virtual bool IsValid() const = 0;

	virtual std::string GetName() const = 0;

	void IncreaseCloudDensity() { cloudDensity *= 1.05f; }
	void DecreaseCloudDensity() { cloudDensity *= 0.95f; }
	float GetCloudDensity() const { return cloudDensity; }

	ISkyLight* GetLight() const { return skyLight; }

	bool SunVisible(const float3 pos) const;

	bool& WireFrameModeRef() { return wireFrameMode; }

	/**
	 * Sets up OpenGL to draw fog or not, according to the value of
	 * globalRendering->drawFog.
	 */
	void SetupFog();

public:
	static void SetSky();
	static auto& GetSky() { return sky; }
	static void KillSky() { sky = nullptr; }
public:
	float3 skyColor;
	float3 sunColor;
	float3 cloudColor;
	float4 fogColor;

	float fogStart;
	float fogEnd;
	float cloudDensity;

protected:
	static inline std::unique_ptr<ISky> sky = nullptr;

	ISkyLight* skyLight;

	bool wireFrameMode;
};

#endif // I_SKY_H
