// VecMath.h - Vector / matrix math and world-to-screen projection.

#pragma once
#include <cmath>
#include <cstdint>

struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float distanceTo(const Vec3& o) const { return (*this - o).length(); }
};

struct Matrix4 { float m[4][4] = {}; };

inline bool worldToScreen(const Vec3& world, const Matrix4& vp,
                          float screenW, float screenH, Vec2& out) {
    const float clipX = world.x * vp.m[0][0] + world.y * vp.m[1][0] +
                        world.z * vp.m[2][0] + vp.m[3][0];
    const float clipY = world.x * vp.m[0][1] + world.y * vp.m[1][1] +
                        world.z * vp.m[2][1] + vp.m[3][1];
    const float clipW = world.x * vp.m[0][3] + world.y * vp.m[1][3] +
                        world.z * vp.m[2][3] + vp.m[3][3];
    if (clipW < 0.01f) return false;
    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;
    out.x = screenW * 0.5f + ndcX * screenW * 0.5f;
    out.y = screenH * 0.5f - ndcY * screenH * 0.5f;
    return true;
}

inline bool worldBoxToScreen(const Vec3& feet, float worldHeight,
                             float worldWidth, const Matrix4& vp,
                             float screenW, float screenH,
                             Vec2& topLeft, Vec2& bottomRight) {
    Vec2 head2d, feet2d;
    const Vec3 head = { feet.x, feet.y, feet.z + worldHeight };
    if (!worldToScreen(feet, vp, screenW, screenH, feet2d)) return false;
    if (!worldToScreen(head, vp, screenW, screenH, head2d)) return false;
    const float height = std::fabs(feet2d.y - head2d.y);
    const float width = height * (worldWidth / worldHeight);
    const float cx = (feet2d.x + head2d.x) * 0.5f;
    topLeft = { cx - width * 0.5f, head2d.y };
    bottomRight = { cx + width * 0.5f, feet2d.y };
    return true;
}
