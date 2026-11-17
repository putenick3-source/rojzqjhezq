// Camera.h - Projection source for the ESP.
//
// ViewProj mode uses the matrix found in the game. Manual mode rebuilds a
// forward/right/up basis from yaw/pitch/fov and projects on its own, so boxes
// stay usable when no trustworthy matrix is available.

#pragma once
#include "Projection.h"
#include "VecMath.h"

#include <cmath>

enum class CamMode {
    ViewProj,
    Manual,
};

struct Camera {
    CamMode    mode = CamMode::ViewProj;
    Projection vp;
    Vec3       pos{};          // manual mode: eye position (world units)
    float      yaw   = 0.f;    // manual mode: degrees, 0 = +X
    float      pitch = 0.f;    // manual mode: degrees, + = up
    float      fov   = 70.f;   // manual mode: vertical FOV, degrees

    bool hasViewProj() const { return mode == CamMode::ViewProj && vp.addr != 0; }

    bool worldToScreen(const Vec3& world, float screenW, float screenH,
                       Vec2& out) const {
        if (mode == CamMode::ViewProj) return vp.project(world, screenW, screenH, out);

        constexpr float kPi = 3.14159265358979f;
        constexpr float kDeg = kPi / 180.f;
        if (!sane(pos, kSaneWorld) || !world.finite()) return false;

        const float yawRad   = yaw * kDeg;
        const float pitchRad = pitch * kDeg;
        const float cp       = std::cos(pitchRad);
        const Vec3  forward{ cp * std::cos(yawRad), cp * std::sin(yawRad),
                             std::sin(pitchRad) };

        Vec3 right = forward.cross(Vec3{ 0.f, 0.f, 1.f }).normalized();
        if (right.length() < 1e-4f) right = { 1.f, 0.f, 0.f }; // straight up/down
        const Vec3 up = right.cross(forward).normalized();
        if (up.length() < 1e-4f) return false;

        const Vec3  rel = world - pos;
        const float z   = rel.dot(forward);
        if (!(z > 0.01f)) return false;

        const float tanHalf = std::tan(fov * 0.5f * kDeg);
        if (!(tanHalf > 0.f)) return false;

        const float aspect = (screenH > 0.f) ? screenW / screenH : 1.f;
        const float ndcX   = rel.dot(right) / (z * tanHalf * aspect);
        const float ndcY   = rel.dot(up) / (z * tanHalf);

        out.x = screenW * 0.5f + ndcX * screenW * 0.5f;
        out.y = screenH * 0.5f - ndcY * screenH * 0.5f;
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    // Projects feet and head, then widens the span by a fixed 1:3 ratio.
    bool boxToScreen(const Vec3& feet, const Vec3& head,
                     float screenW, float screenH,
                     Vec2& topLeft, Vec2& bottomRight) const {
        Vec2 feet2d, head2d;
        if (!worldToScreen(feet, screenW, screenH, feet2d)) return false;
        if (!worldToScreen(head, screenW, screenH, head2d)) return false;

        const float height = std::fabs(feet2d.y - head2d.y);
        if (height < 1.f) return false;

        const float width = height * kBoxWidthPerHeight;
        const float cx    = (feet2d.x + head2d.x) * 0.5f;
        topLeft     = { cx - width * 0.5f, head2d.y };
        bottomRight = { cx + width * 0.5f, feet2d.y };
        return true;
    }

private:
    static constexpr float kSaneWorld          = 100000.f;
    static constexpr float kBoxWidthPerHeight  = 1.f / 3.f; // 0.6 m wide for 1.8 m tall
};
