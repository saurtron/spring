#include "FeatureDrawerData.h"

#include "System/Config/ConfigHandler.h"
#include "System/StringHash.h"
#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Rendering/LuaObjectDrawer.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Common/ModelDrawerHelpers.h"

#include "System/Misc/TracyDefs.h"

CONFIG(float, FeatureDrawDistance)
.defaultValue(6000.0f)
.minimumValue(0.0f)
.description("Maximum distance at which features will be drawn.");

CONFIG(float, FeatureFadeDistance)
.defaultValue(4500.0f)
.minimumValue(0.0f)
.description("Distance at which features will begin to fade from view.");


void CFeatureDrawerData::RenderFeaturePreCreated(const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (feature->def->drawType != DRAWTYPE_MODEL)
		return;

	UpdateObject(feature, true);
}

//TODO remove
void CFeatureDrawerData::RenderFeatureCreated(const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(
		feature->def->drawType != DRAWTYPE_MODEL ||
		std::find(unsortedObjects.begin(), unsortedObjects.end(), feature) != unsortedObjects.end()
	);
}

void CFeatureDrawerData::RenderFeatureDestroyed(const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	DelObject(feature, feature->def->drawType == DRAWTYPE_MODEL);
	LuaObjectDrawer::SetObjectLOD(const_cast<CFeature*>(feature), LUAOBJ_FEATURE, 0);
}

CFeatureDrawerData::CFeatureDrawerData(bool& mtModelDrawer_)
	: CFeatureDrawerDataBase("[CFeatureDrawerData]", 313373, mtModelDrawer_)
{
	RECOIL_DETAILED_TRACY_ZONE;
	eventHandler.AddClient(this); //cannot be done in CModelRenderDataConcept, because object is not fully constructed
	configHandler->NotifyOnChange(this, { "FeatureDrawDistance", "FeatureFadeDistance" });

	featureDrawDistance = configHandler->GetFloat("FeatureDrawDistance");
	featureFadeDistance = std::min(configHandler->GetFloat("FeatureFadeDistance"), featureDrawDistance);

	featureFadeDistanceSq = Square(featureFadeDistance);
	featureDrawDistanceSq = Square(featureDrawDistance);
}

CFeatureDrawerData::~CFeatureDrawerData()
{
	RECOIL_DETAILED_TRACY_ZONE;
	configHandler->RemoveObserver(this);
}

void CFeatureDrawerData::ConfigNotify(const std::string& key, const std::string& value)
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (hashStringLower(key.c_str())) {
	case hashStringLower("FeatureDrawDistance"): {
		featureDrawDistance = std::strtof(value.c_str(), nullptr);
	} break;
	case hashStringLower("FeatureFadeDistance"): {
		featureFadeDistance = std::strtof(value.c_str(), nullptr);
	} break;
	default: {} break;
	}

	featureDrawDistance = std::max(0.0f, featureDrawDistance);
	featureFadeDistance = std::max(0.0f, featureFadeDistance);
	featureFadeDistance = std::min(featureDrawDistance, featureFadeDistance);

	featureFadeDistanceSq = Square(featureFadeDistance);
	featureDrawDistanceSq = Square(featureDrawDistance);

	LOG_L(L_INFO, "[FeatureDrawer::%s] {draw,fade}distance set to {%f,%f}", __func__, featureDrawDistance, featureFadeDistance);
}

void CFeatureDrawerData::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (mtModelDrawer) {
		for_mt_chunk(0, unsortedObjects.size(), [this](const int k) {
			CFeature* f = unsortedObjects[k];
			UpdateDrawPos(f);
			UpdateCommon(f);
		}, CModelDrawerDataConcept::MT_CHUNK_OR_MIN_CHUNK_SIZE_UPDT);
	}
	else {
		for (CFeature* f : unsortedObjects) {
			UpdateDrawPos(f);
			UpdateCommon(f);
		}
	}
}

bool CFeatureDrawerData::IsAlpha(const CFeature* co) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (co->drawAlpha < 1.0f);
}

void CFeatureDrawerData::UpdateObjectDrawFlags(CSolidObject* o) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	CFeature* f = static_cast<CFeature*>(o);
	f->ResetDrawFlag();

	for (uint32_t camType = CCamera::CAMTYPE_PLAYER; camType < CCamera::CAMTYPE_ENVMAP; ++camType) {
		if (camType == CCamera::CAMTYPE_UWREFL && !IWater::GetWater()->CanDrawReflectionPass())
			continue;

		if (camType == CCamera::CAMTYPE_SHADOW && ((shadowHandler.shadowGenBits & CShadowHandler::SHADOWGEN_BIT_MODEL) == 0))
			continue;

		const CCamera* cam = CCameraHandler::GetCamera(camType);

		if (f->noDraw)
			continue;

		if (f->IsInVoid())
			continue;

		if (!f->IsInLosForAllyTeam(gu->myAllyTeam) && !gu->spectatingFullView)
			continue;

		if (!cam->InView(f->drawMidPos, f->GetDrawRadius()))
			continue;

		switch (camType)
			{
			case CCamera::CAMTYPE_PLAYER: {
				const float sqrCamDist = (f->drawPos - cam->GetPos()).SqLength();

				// special case for non-fading features
				if (!f->alphaFade) {
					f->SetDrawFlag(DrawFlags::SO_OPAQUE_FLAG);
					f->drawAlpha = 1.0f;
					continue;
				}

				// draw feature as normal, no fading
				if (sqrCamDist < featureFadeDistanceSq) {
					f->SetDrawFlag(DrawFlags::SO_OPAQUE_FLAG);

					if (f->IsInWater())
						f->AddDrawFlag(DrawFlags::SO_REFRAC_FLAG);

					f->drawAlpha = 1.0f;
					continue;
				}

				// otherwise save it for the fade-pass
				if (sqrCamDist < featureDrawDistanceSq) {
					f->drawAlpha = 1.0f - (sqrCamDist - featureFadeDistanceSq) / (featureDrawDistanceSq - featureFadeDistanceSq);
					f->SetDrawFlag(DrawFlags::SO_ALPHAF_FLAG);

					if (f->IsInWater())
						f->AddDrawFlag(DrawFlags::SO_REFRAC_FLAG);

					continue;
				}
			} break;

			case CCamera::CAMTYPE_UWREFL: {
				if (!f->HasDrawFlag(DrawFlags::SO_OPAQUE_FLAG) && !f->HasDrawFlag(DrawFlags::SO_ALPHAF_FLAG))
					continue;

				if (CModelDrawerHelper::ObjectVisibleReflection(f->drawMidPos, cam->GetPos(), f->GetDrawRadius()))
					f->AddDrawFlag(DrawFlags::SO_REFLEC_FLAG);
			} break;

			case CCamera::CAMTYPE_SHADOW: {
				if      (f->HasDrawFlag(DrawFlags::SO_OPAQUE_FLAG))
					f->AddDrawFlag(DrawFlags::SO_SHOPAQ_FLAG);
				else if (f->HasDrawFlag(DrawFlags::SO_ALPHAF_FLAG))
					f->AddDrawFlag(DrawFlags::SO_SHTRAN_FLAG);
			} break;

			default: { assert(false); } break;
		}
	}

	if (f->alwaysUpdateMat || (f->drawFlag > DrawFlags::SO_NODRAW_FLAG && f->drawFlag < DrawFlags::SO_DRICON_FLAG)) {
		f->UpdateTransform(f->drawPos, false);
	}
}

void CFeatureDrawerData::UpdateDrawPos(CFeature* f)
{
	RECOIL_DETAILED_TRACY_ZONE;
	f->drawPos    = f->GetDrawPos(globalRendering->timeOffset);
	f->drawMidPos = f->GetMdlDrawMidPos();
}