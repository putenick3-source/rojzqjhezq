// Projection.h - View-projection matrix read + sanity validation.
//
// The 4x4 matrix is stored row-major as math (m[row][col]) regardless of the
// layout found in memory: readProjection transposes column-major sources so
// callers only ever see one convention. A matrix is trusted only after
// validateProjection passes (see below), which keeps garbage addresses from
// producing plausible-looking boxes.

#pragma once
#include "Process.h"
#include "VecMath.h"

#include <cmath>
#include <cstdint>

struct Mat4 { float m[4][4] = {}; };

enum class Layout {
    ColumnMajor,
    RowMajor,
};

inline bool validateProjection(const Mat4& m, float& residual);

struct Projection {
    Mat4      mat{};
    uintptr_t addr = 0;

    // Validates once per read (cached). False for NaN/stale/garbage matrices.
    bool valid() const {
        if (!checked_) {
            float residual = 1.0f;
            valid_   = validateProjection(mat, residual);
            checked_ = true;
        }
        return valid_;
    }

    // World -> screen through the matrix. Fails on invalid matrices, behind
    // the camera or non-finite input/output.
    bool project(const Vec3& world, float screenW, float screenH, Vec2& out) const {
        if (!valid() || !world.finite()) return false;

        // Row-vector convention: rows 0..2 are the basis, row 3 the translation.
        const float clipX = row(0, 0) * world.x + row(1, 0) * world.y +
                            row(2, 0) * world.z + row(3, 0);
        const float clipY = row(0, 1) * world.x + row(1, 1) * world.y +
                            row(2, 1) * world.z + row(3, 1);
        const float clipW = row(0, 3) * world.x + row(1, 3) * world.y +
                            row(2, 3) * world.z + row(3, 3);
        if (!(clipW > 0.01f)) return false;

        out.x = screenW * 0.5f + (clipX / clipW) * screenW * 0.5f;
        out.y = screenH * 0.5f - (clipY / clipW) * screenH * 0.5f;
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    // Clears the validation cache after a fresh matrix read.
    void invalidate() {
        checked_ = false;
        valid_   = false;
    }

private:
    friend bool readProjection(const Process& proc, uintptr_t addr,
                               Layout layout, Projection& out);

    float row(int r, int p) const { return mat.m[r][p]; }

    mutable bool checked_ = false;
    mutable bool valid_   = false;
};

// A row-vector view-projection matrix carries the camera basis in rows 0..2 and
// a unit-ish translation row in row 3. Rows 0..2 must be orthonormal: the
// residual is |dot(normalize(row0), normalize(row1 x row2))| and stays near 0
// for a real matrix. `residual` is reported even when the row-3 gate fails.
inline bool validateProjection(const Mat4& m, float& residual) {
    const Vec3 r0{ m.m[0][0], m.m[0][1], m.m[0][2] };
    const Vec3 r1{ m.m[1][0], m.m[1][1], m.m[1][2] };
    const Vec3 r2{ m.m[2][0], m.m[2][1], m.m[2][2] };
    const Vec3 r3{ m.m[3][0], m.m[3][1], m.m[3][2] };

    residual = 1.0f;

    const float scale = r3.length();
    if (!std::isfinite(scale) || scale < 0.6f || scale > 1.4f) return false;

    residual = std::fabs(r0.normalized().dot(r1.cross(r2).normalized()));
    return std::isfinite(residual) && residual < 0.06f;
}

// Reads 16 floats at `addr` and stores them row-major, transposing when the
// source layout is column-major. Returns false on a short/unreadable read.
inline bool readProjection(const Process& proc, uintptr_t addr,
                           Layout layout, Projection& out) {
    float raw[16]{};
    if (!addr || !proc.readRaw(addr, raw, sizeof(raw))) return false;

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.mat.m[r][c] =
                (layout == Layout::RowMajor) ? raw[r * 4 + c] : raw[c * 4 + r];
        }
    }
    out.addr = addr;
    out.invalidate();
    return true;
}
