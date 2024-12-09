#version 430 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 T;
layout (location = 3) in vec3 B;
layout (location = 4) in vec4 uv;
layout (location = 5) in uvec2 bonesInfo; //boneIDs, boneWeights

layout (location = 6) in uvec4 instData;
// u32 matOffset
// u32 uniOffset
// u32 {teamIdx, drawFlag, unused, numPieces}
// u32 bposeMatOffset

layout(std140, binding = 0) uniform UniformMatrixBuffer {
	mat4 screenView;
	mat4 screenProj;
	mat4 screenViewProj;

	mat4 cameraView;
	mat4 cameraProj;
	mat4 cameraViewProj;
	mat4 cameraBillboardView;

	mat4 cameraViewInv;
	mat4 cameraProjInv;
	mat4 cameraViewProjInv;

	mat4 shadowView;
	mat4 shadowProj;
	mat4 shadowViewProj;

	mat4 reflectionView;
	mat4 reflectionProj;
	mat4 reflectionViewProj;

	mat4 orthoProj01;

	// transforms for [0] := Draw, [1] := DrawInMiniMap, [2] := Lua DrawInMiniMap
	mat4 mmDrawView; //world to MM
	mat4 mmDrawProj; //world to MM
	mat4 mmDrawViewProj; //world to MM

	mat4 mmDrawIMMView; //heightmap to MM
	mat4 mmDrawIMMProj; //heightmap to MM
	mat4 mmDrawIMMViewProj; //heightmap to MM

	mat4 mmDrawDimView; //mm dims
	mat4 mmDrawDimProj; //mm dims
	mat4 mmDrawDimViewProj; //mm dims
};

layout(std140, binding = 1) uniform UniformParamsBuffer {
	vec3 rndVec3; //new every draw frame.
	uint renderCaps; //various render booleans

	vec4 timeInfo;     //gameFrame, drawSeconds, interpolated(unsynced)GameSeconds(synced), frameTimeOffset
	vec4 viewGeometry; //vsx, vsy, vpx, vpy
	vec4 mapSize;      //xz, xzPO2
	vec4 mapHeight;    //height minCur, maxCur, minInit, maxInit

	vec4 fogColor;  //fog color
	vec4 fogParams; //fog {start, end, 0.0, scale}

	vec4 sunDir;

	vec4 sunAmbientModel;
	vec4 sunAmbientMap;
	vec4 sunDiffuseModel;
	vec4 sunDiffuseMap;
	vec4 sunSpecularModel;
	vec4 sunSpecularMap;

	vec4 shadowDensity; // {ground, units, 0.0, 0.0}

	vec4 windInfo; // windx, windy, windz, windStrength
	vec2 mouseScreenPos; //x, y. Screen space.
	uint mouseStatus; // bits 0th to 32th: LMB, MMB, RMB, offscreen, mmbScroll, locked
	uint mouseUnused;
	vec4 mouseWorldPos; //x,y,z; w=0 -- offmap. Ignores water, doesn't ignore units/features under the mouse cursor

	vec4 teamColor[255]; //all team colors
};

layout(std140, binding=0) readonly buffer MatrixBuffer {
	mat4 mat[];
};

uniform vec4 clipPlane0 = vec4(0.0, 0.0, 0.0, 1.0); //upper construction clip plane
uniform vec4 clipPlane1 = vec4(0.0, 0.0, 0.0, 1.0); //lower construction clip plane

out Data {
	vec4 uvCoord;
};
out float gl_ClipDistance[2];

void TransformShadowCam(vec4 worldPos, vec3 worldNormal) {
	vec4 lightVertexPos = shadowView * worldPos;
	vec3 lightVertexNormal = normalize(mat3(shadowView) * worldNormal);

	float NdotL = clamp(lightVertexNormal.z, 0.0, 1.0);

	//use old bias formula from GetShadowPCFRandom(), but this time to write down shadow depth map values
	const float cb = 1e-5;
	float bias = cb * clamp(tan(acos(NdotL)), 0.0, 30.0);

	lightVertexPos.xy += vec2(0.5);
	lightVertexPos.z  += bias;

	gl_Position = shadowProj * lightVertexPos;
}

#line 1115

uint GetUnpackedValue(uint packedValue, uint byteNum) {
	return (packedValue >> (8u * byteNum)) & 0xFFu;
}

void GetModelSpaceVertex(out vec4 msPosition, out vec3 msNormal)
{
	vec4 piecePos = vec4(pos, 1.0);

	vec4 weights = vec4(
		float(GetUnpackedValue(bonesInfo.y, 0)) / 255.0,
		float(GetUnpackedValue(bonesInfo.y, 1)) / 255.0,
		float(GetUnpackedValue(bonesInfo.y, 2)) / 255.0,
		float(GetUnpackedValue(bonesInfo.y, 3)) / 255.0
	);

	uint b0 = GetUnpackedValue(bonesInfo.x, 0); //first boneID
	mat4 b0BoneMat = mat[instData.x + b0 + 1u];
	mat3 b0NormMat = mat3(b0BoneMat);

	weights[0] *= b0BoneMat[3][3];

	msPosition = b0BoneMat * piecePos;
	msNormal   = b0NormMat * normal;

	if (weights[0] == 1.0)
		return;

	float wSum = 0.0;

	msPosition *= weights[0];
	msNormal   *= weights[0];
	wSum       += weights[0];

	uint numPieces = GetUnpackedValue(instData.z, 3);
	mat4 bposeMat    = mat[instData.w + b0];

	// Vertex[ModelSpace,BoneX] = PieceMat[BoneX] * InverseBindPosMat[BoneX] * BindPosMat[Bone0] * Vertex[Bone0]
	for (uint bi = 1; bi < 3; ++bi) {
		uint bID = GetUnpackedValue(bonesInfo.x, bi);

		if (bID == 0xFFu || weights[bi] == 0.0)
			continue;

		mat4 bposeInvMat = mat[instData.w + numPieces + bID];
		mat4 boneMat     = mat[instData.x +        1u + bID];

		weights[bi] *= boneMat[3][3];

		mat4 skinMat = boneMat * bposeInvMat * bposeMat;
		mat3 normMat = mat3(skinMat);

		msPosition += skinMat * piecePos * weights[bi];
		msNormal   += normMat * normal   * weights[bi];
		wSum       += weights[bi];
	}

	msPosition /= wSum;
	msNormal   /= wSum;
}

void main(void)
{
	mat4 worldMatrix = mat[instData.x];

	vec4 modelPos;
	vec3 modelNormal;
	GetModelSpaceVertex(modelPos, modelNormal);

	vec4 worldPos = worldMatrix * modelPos;
	vec3 worldNormal = mat3(worldMatrix) * modelNormal;

	gl_ClipDistance[0] = dot(modelPos, clipPlane0); //upper construction clip plane
	gl_ClipDistance[1] = dot(modelPos, clipPlane1); //lower construction clip plane

	uvCoord = uv;

	TransformShadowCam(worldPos, worldNormal);
}