#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

// -----------------------------------------------------------------------------
// Math primitives for an external overlay.
//
// Coordinate space is Source 2 world space: X forward-ish, Y left, Z up, units
// are inches. Angles are (pitch, yaw, roll) in degrees, pitch clamped to +-89
// because the engine misbehaves past that.
//
// w2s() uses the view-projection matrix at dwViewMatrix in the row-vector
// convention: clip = p * M, with translation in M[3][0..2] and w in M[3][3].
// -----------------------------------------------------------------------------

struct Vec2 { float x = 0, y = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s)       const { return { x * s, y * s, z * s }; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float len() const { return std::sqrt(x * x + y * y + z * z); }
    float len2d() const { return std::sqrt(x * x + y * y); }

    bool finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }
    bool zero() const { return x == 0.f && y == 0.f && z == 0.f; }
};

struct QAngle {
    float pitch = 0, yaw = 0, roll = 0;
    bool finite() const {
        return std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(roll);
    }
};

// CTransform as stored in the bone array: quaternion (xyzw) then translation.
struct Transform {
    float qx = 0, qy = 0, qz = 0, qw = 1;
    float x = 0, y = 0, z = 0;
    float pad = 0;
};

// ------------------------------------------------------------------ constants --
constexpr float PI_F        = 3.14159265358979323846f;
constexpr float RAD2DEG     = 180.0f / PI_F;
constexpr float DEG2RAD     = PI_F / 180.0f;

// --------------------------------------------------------------------- vec ops --
inline Vec3  vsub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline float vdot(const Vec3& a, const Vec3& b) { return a.dot(b); }
inline float vlen(const Vec3& a)                { return a.len(); }

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Subtract angles with wraparound handled on both input and output.
inline QAngle angleSub(const QAngle& a, const QAngle& b) {
    QAngle d;
    d.pitch = a.pitch - b.pitch;
    d.yaw   = a.yaw   - b.yaw;
    d.roll  = a.roll  - b.roll;
    d.yaw   = std::fmodf(d.yaw + 540.f, 360.f) - 180.f;
    d.pitch = std::fmodf(d.pitch + 540.f, 360.f) - 180.f;
    d.roll  = std::fmodf(d.roll + 540.f, 360.f) - 180.f;
    return d;
}

inline float angleLen(const QAngle& a) {
    return std::sqrt(a.pitch * a.pitch + a.yaw * a.yaw);
}

// Convert world-space direction to the engine's pitch/yaw convention.
// Source: yaw measures rotation about Z from +X, pitch is +down.
inline QAngle vecToAngles(const Vec3& delta) {
    QAngle a;
    float hyp = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    a.pitch = std::atan2f(-delta.z, hyp) * RAD2DEG;
    a.yaw   = std::atan2f(delta.y, delta.x) * RAD2DEG;
    a.roll  = 0.f;
    return a;
}

inline void normalizeAngle(QAngle& a) {
    a.pitch = std::fmodf(a.pitch, 360.f);
    a.yaw   = std::fmodf(a.yaw, 360.f);
    if (a.yaw > 180.f)   a.yaw   -= 360.f;
    if (a.yaw < -180.f)  a.yaw   += 360.f;
    a.pitch = clampf(a.pitch, -89.f, 89.f);
    a.roll  = 0.f;
}

// ------------------------------------------------------------------- transform --
// CTransform decompressed -> world space, given the skeleton's world position.
// quatRotate applies the stored rotation to a local bone vector.
inline Vec3 quatRotate(float qx, float qy, float qz, float qw, const Vec3& v) {
    // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    Vec3 q{ qx, qy, qz };
    Vec3 t = {
        q.y * v.z - q.z * v.y,
        q.z * v.x - q.x * v.z,
        q.x * v.y - q.y * v.x
    };
    t.x += qw * v.x; t.y += qw * v.y; t.z += qw * v.z;
    Vec3 c = {
        q.y * t.z - q.z * t.y,
        q.z * t.x - q.x * t.z,
        q.x * t.y - q.y * t.x
    };
    return { v.x + 2.f * c.x, v.y + 2.f * c.y, v.z + 2.f * c.z };
}

// Bone world position: rotate the bone's local translation by the skeleton
// rotation, then add the skeleton origin. Bone transforms in the array are
// model-space, so the origin is the entity's absolute origin.
inline Vec3 bonePos(const Transform& bone, const Vec3& origin) {
    Vec3 local{ bone.x, bone.y, bone.z };
    Vec3 rot = quatRotate(bone.qx, bone.qy, bone.qz, bone.qw, local);
    return origin + rot;
}

// --------------------------------------------------------------------- w2s --
// Returns false when the point is behind the camera or mathematically invalid.
inline bool worldToScreen(const float m[16], const Vec3& p, Vec2& out, int sw, int sh) {
    float clipX = p.x * m[0] + p.y * m[4] + p.z * m[8]  + m[12];
    float clipY = p.x * m[1] + p.y * m[5] + p.z * m[9]  + m[13];
    float clipW = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];

    if (clipW < 0.001f) return false;
    if (!std::isfinite(clipW) || !std::isfinite(clipX) || !std::isfinite(clipY)) return false;

    float invW = 1.0f / clipW;
    float ndcX = clipX * invW;
    float ndcY = clipY * invW;

    out.x = (sw * 0.5f) * (1.0f + ndcX);
    out.y = (sh * 0.5f) * (1.0f - ndcY);
    return std::isfinite(out.x) && std::isfinite(out.y);
}

// Orthonormalise a 4x4 view-projection matrix in place.
// CS2 hands back a row-major matrix whose last row can drift; re-deriving the
// right/up rows from the reconstructed view axes removes the skew that
// otherwise makes boxes lean near the screen edges. Falls back to the original
// matrix if the reconstructed basis degenerates.
inline void fixViewMatrix(float m[16]) {
    Vec3 forward{ m[12], m[13], m[14] };
    Vec3 up{ m[4], m[5], m[6] };
    Vec3 right{ m[0], m[1], m[2] };

    if (!forward.finite() || forward.len() < 1e-6f) return;
    if (!up.finite() || up.len() < 1e-6f) return;
    if (!right.finite() || right.len() < 1e-6f) return;

    Vec3 R = right * (1.0f / right.len());
    Vec3 U = up * (1.0f / up.len());
    Vec3 F = forward * (1.0f / forward.len());

    // Re-derive an orthogonal basis around the original forward axis.
    Vec3 U2 = { F.y * R.z - F.z * R.y, F.z * R.x - F.x * R.z, F.x * R.y - F.y * R.x };
    if (U2.len() < 1e-6f) return;
    U2 = U2 * (1.0f / U2.len());

    Vec3 R2 = { U2.y * F.z - U2.z * F.y, U2.z * F.x - U2.x * F.z, U2.x * F.y - U2.y * F.x };
    if (R2.len() < 1e-6f) return;
    R2 = R2 * (1.0f / R2.len());

    m[0] = R2.x; m[1] = R2.y; m[2]  = R2.z;
    m[4] = U2.x; m[5] = U2.y; m[6]  = U2.z;
    m[12] = F.x; m[13] = F.y; m[14] = F.z;
}
