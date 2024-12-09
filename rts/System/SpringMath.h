/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef SPRING_MATH_H
#define SPRING_MATH_H

#include "Sim/Misc/GlobalConstants.h"
#include "System/type2.h"
#include "System/float3.h"
#include "System/float4.h"
#include "System/MathConstants.h"

#include <cmath> // std::fabs
#include <algorithm> // std::{min,max}
#include <limits>

static constexpr int SPRING_MAX_HEADING = 32768;
static constexpr int SPRING_CIRCLE_DIVS = (SPRING_MAX_HEADING << 1);

#define HEADING_CHECKSUM_1024 0x617a9968
#define HEADING_CHECKSUM_4096 0x3d51b476
#define NUM_HEADINGS 4096

#if (NUM_HEADINGS == 1024)
	#define HEADING_CHECKSUM  HEADING_CHECKSUM_1024
#elif (NUM_HEADINGS == 4096)
	#define HEADING_CHECKSUM  HEADING_CHECKSUM_4096
#else
	#error "HEADING_CHECKSUM not set, invalid NUM_HEADINGS?"
#endif

enum FacingMap {
	FACING_NORTH = 2,
	FACING_SOUTH = 0,
	FACING_EAST  = 1,
	FACING_WEST  = 3,
	NUM_FACINGS  = 4,
};

class SpringMath {
public:
	static void Init();
	static float2 headingToVectorTable[NUM_HEADINGS];
};


struct shortint2 {
	short int x, y;
};


short int GetHeadingFromFacing(const int facing) _pure _warn_unused_result;
int GetFacingFromHeading(const short int heading) _pure _warn_unused_result;
float GetHeadingFromVectorF(const float dx, const float dz) _pure _warn_unused_result;
short int GetHeadingFromVector(const float dx, const float dz) _pure _warn_unused_result;
shortint2 GetHAndPFromVector(const float3 vec) _pure _warn_unused_result; // vec should be normalized
float2 GetHAndPFromVectorF(const float3 vec) _pure _warn_unused_result; // vec should be normalized
float3 GetVectorFromHeading(const short int heading) _pure _warn_unused_result;
float3 GetVectorFromHAndPExact(const short int heading, const short int pitch) _pure _warn_unused_result;

float3 CalcBeizer(const float i, const float3 p1, const float3 p2, const float3 p3, const float3 p4) _pure _warn_unused_result;

float LinePointDist(const float3 l1, const float3 l2, const float3 p) _pure _warn_unused_result;
float3 ClosestPointOnLine(const float3 l1, const float3 l2, const float3 p) _pure _warn_unused_result;
bool ClosestPointOnRay(const float3 p0, const float3 ray, const float3 p, float3& px) _pure _warn_unused_result;
bool RayHitsSphere(const float4 sphere, const float3 p0, const float3 ray) _pure _warn_unused_result;

/**
 * @brief Returns the intersection points of a ray with a plane, defined by the canonical plane equation
 * @param p0 float3 the start point of the ray
 * @param p1 float3 the end point of the ray
 * @param plane the canonical plane equation Ax + By + Cy + D = 0
 * @param directional if the plane should be considered directional (intersection happens only when plane.n dot ray is negative)
 * @param px the intersection point
 * @return true if px is valid and the intersection point has been found, false otherwise
 */
bool RayAndPlaneIntersection(const float3& p0, const float3& p1, const float4& plane, bool directional, float3& px);


/**
 * @brief Returns the line result of the intersection of two planes
 * @param plane1 float4 the first plane
 * @param plane2 float4 the second plane
 * @param line <direction,point> std::pair<float3,float3> the direction and a point on the line
 * @return bool whether planes intersect
 */
bool IntersectPlanes(const float4& plane1, const float4& plane2, std::pair<float3, float3> &line);

/**
 * @brief Returns the line result of the intersection of two lines
 * @param l1 <direction,point> std::pair<float3,float3> the first line
 * @param l2 <direction,point> std::pair<float3,float3> the second line
 * @param px float3 the intersection point
 * @return bool whether lines intersect
 */
bool LinesIntersectionPoint(const std::pair<float3, float3>& l1, const std::pair<float3, float3>& l2, float3& px);

/**
 * @brief Returns the a point in the line intersection of two planes
 * @param zeroCoord int the axis to be pinned to
 */
float3 SolveIntersectingPoint(int zeroCoord, int coord1, int coord2, const float4& plane1, const float4& plane2);

/**
 * @brief Returns the intersection points of a ray with the map boundary (2d only)
 * @param start float3 the start point of the line
 * @param dir float3 direction of the ray
 * @return <near,far> std::pair<float,float> distance to the intersection points in mulitples of `dir`
 */
float2 GetMapBoundaryIntersectionPoints(const float3 start, const float3 dir) _pure _warn_unused_result;

/**
 * @brief clamps a line (start & end points) to the map boundaries
 * @param start float3 the start point of the line
 * @param end float3 the end point of the line
 * @return true if either `end` or `start` was changed
 */
bool ClampLineInMap(float3& start, float3& end);

/**
 * @brief clamps a ray (just the end point) to the map boundaries
 * @param start const float3 the start point of the line
 * @param end float3 the `end` point of the line
 * @return true if changed
 */
bool ClampRayInMap(const float3 start, float3& end);


void ClipRayByPlanes(const float3& p0, float3& p, const std::initializer_list<float4>& clipPlanes);

float3 GetTriangleBarycentric(const float3& p0, const float3& p1, const float3& p2, const float3& p);
bool PointInsideTriangle(const float3& p0, const float3& p1, const float3& p2, const float3& p);
bool PointInsideQuadrilateral(const float3& p0, const float3& p1, const float3& p2, const float3& p3, const float3& px);

float smoothstep(const float edge0, const float edge1, const float value) _pure _warn_unused_result;
float3 smoothstep(const float edge0, const float edge1, float3 vec) _pure _warn_unused_result;

float linearstep(const float edge0, const float edge1, const float value) _pure _warn_unused_result;


#ifndef FAST_EPS_CMP
template<class T> inline bool epscmp(const T a, const T b, const T eps) {
	return ((a == b) || (math::fabs(a - b) <= (eps * std::max(std::max(math::fabs(a), math::fabs(b)), T(1)))));
}
#else
template<class T> inline bool epscmp(const T a, const T b, const T eps) {
	return ((a == b) || (std::fabs(a - b) <= (eps * (T(1) + std::fabs(a) + std::fabs(b)))));
}
#endif


// inlined to avoid multiple definitions due to the specializing templates
template<class T> inline T argmin(const T v1, const T v2) { return std::min(v1, v2); }
template<class T> inline T argmax(const T v1, const T v2) { return std::max(v1, v2); }
template<> inline float3 argmin(const float3 v1, const float3 v2) { return float3::min(v1, v2); }
template<> inline float3 argmax(const float3 v1, const float3 v2) { return float3::max(v1, v2); }

// multiple arguments option
template<typename T>
inline T argmin(const T v1) { return v1; }
template<typename T>
inline T argmax(const T v1) { return v1; }
template<typename T, typename ...Ts>
inline T argmin(const T v1, const Ts... vs) { return argmin(v1, argmin(vs...)); }
template<typename T, typename ...Ts>
inline T argmax(const T v1, const Ts... vs) { return argmax(v1, argmax(vs...)); }

// template<class T> T mix(const T v1, const T v2, const float a) { return (v1 * (1.0f - a) + v2 * a); }
template<class T, typename T2> constexpr T mix(const T v1, const T v2, const T2 a) { return (v1 + (v2 - v1) * a); }

template <class T, class T2> constexpr T mixRotation(T v1, T v2, T2 a) {
    v1=ClampRad(v1);
    v2=ClampRad(v2);
    return ClampRad(v1 + GetRadAngleToward(v1, v2) * a);
}

template<class T> constexpr T Blend(const T v1, const T v2, const float a) { return mix(v1, v2, a); }

int Round(const float f) _const _warn_unused_result;

template<class T> constexpr T Square(const T x) { return x*x; }
template<class T> constexpr T SignedSquare(const T x) { return x * std::abs(x); }
// NOTE: '>' instead of '>=' s.t. Sign(int(true)) != Sign(int(false)) --> zero is negative!
template<class T> constexpr T Sign(const T v) { return ((v > T(0)) * T(2) - T(1)); }

template <typename T>
constexpr T AlignUp(T value, size_t size)
{
	static_assert(std::is_unsigned<T>(), "T must be an unsigned value.");
	return static_cast<T>(value + (size - value % size) % size);
}

template <typename T>
constexpr T AlignDown(T value, size_t size)
{
	static_assert(std::is_unsigned<T>(), "T must be an unsigned value.");
	return static_cast<T>(value - value % size);
}

/**
 * @brief does a division and returns additionally the remnant
 */
int2 IdxToCoord(unsigned x, unsigned array_width) _const _warn_unused_result;


/**
 * @brief Clamps an radian angle between 0 .. 2*pi
 * @param f float* value to clamp
 */
float ClampRad(float f) _const _warn_unused_result;


/**
 * @brief Clamps an radian angle between 0 .. 2*pi
 * @param f float* value to clamp
 */
void ClampRad(float* f);


/**
 * @brief Clamps an radian angle between 0 .. 2*pi
 * @param v float3 value to clamp
 */
float3 ClampRad(float3 v);

/**
 * @brief Clamps a radian angle between -pi and pi
 * @param v float3 value to clamp
 */
float3 ClampRadPrincipal(float3 v);

/**
 * @brief Shortest angle in radians
 * @param f1 float first compare value
 * @param f2 float first compare value
 */
float GetRadAngleToward(float f1, float f2);


/**
 * @brief Shortest angle in radians for float3
 * @param v1 float3 first compare value
 * @param v2 float3 second compare value
 */
float3 GetRadAngleToward(float3 v1, float3 v2);


/**
 * @brief Checks if 2 radian values discribe the same angle
 * @param f1 float* first compare value
 * @param f2 float* second compare value
 */
bool RadsAreEqual(const float f1, const float f2) _const;


/**
 */
float GetRadFromXY(const float dx, const float dy) _const;


/**
 * convert a color in HS(V) space to RGB
 */
float3 hs2rgb(float h, float s) _pure _warn_unused_result;


#include "SpringMath.inl"

#undef _const

#endif // MYMATH_H
