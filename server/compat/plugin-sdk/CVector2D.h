#pragma once

class CVector2D
{
public:
    float x = 0.0f;
    float y = 0.0f;

    CVector2D() = default;
    CVector2D(float x, float y) : x(x), y(y) {}
};

static_assert(sizeof(CVector2D) == 0x8, "CVector2D must retain GTA SA's wire-compatible layout");
