#pragma once

#include <cmath>

class CVector
{
public:
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    CVector() = default;
    CVector(float x, float y, float z) : x(x), y(y), z(z) {}

    CVector operator+(const CVector& other) const { return {x + other.x, y + other.y, z + other.z}; }
    CVector operator-(const CVector& other) const { return {x - other.x, y - other.y, z - other.z}; }
    CVector operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    CVector operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }

    CVector& operator+=(const CVector& other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    CVector& operator-=(const CVector& other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
};

inline CVector operator*(float scalar, const CVector& vector)
{
    return vector * scalar;
}

static_assert(sizeof(CVector) == 0xC, "CVector must retain GTA SA's wire-compatible layout");
