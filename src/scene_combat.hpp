#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

struct Vec3 {
    float x{}, y{}, z{};
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
};

inline float vdot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float vlength(Vec3 v) { return std::sqrt(vdot(v, v)); }
inline Vec3 vnormalize(Vec3 v) {
    const float len = vlength(v);
    return len > 1e-6f ? v * (1.f / len) : Vec3{0, 1, 0};
}
inline Vec3 vcross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float vmax3(float a, float b, float c) { return std::max(a, std::max(b, c)); }

inline float sdBox(Vec3 p, Vec3 b) {
    const float qx = std::abs(p.x) - b.x, qy = std::abs(p.y) - b.y, qz = std::abs(p.z) - b.z;
    const float outside = vlength({std::max(qx, 0.f), std::max(qy, 0.f), std::max(qz, 0.f)});
    const float inside = std::min(vmax3(qx, qy, qz), 0.f);
    return outside + inside;
}

inline float sdRoundBox(Vec3 p, Vec3 b, float r) {
    return sdBox(p, {b.x - r, b.y - r, b.z - r}) - r;
}

inline float sdCylinderY(Vec3 p, float r, float h) {
    const float dxz = std::sqrt(p.x * p.x + p.z * p.z) - r;
    const float dy = std::abs(p.y) - h;
    const float outside = vlength({std::max(dxz, 0.f), std::max(dy, 0.f)});
    return outside + std::min(std::max(dxz, dy), 0.f);
}

struct RayHit {
    bool hit{};
    Vec3 pos{};
    Vec3 normal{};
    float t{1e9f};
    int material{-1};
};

struct PuzzleState {
    Vec3 cubePos{2.2f, .42f, 14.2f};
    Vec3 cubeVel{};
    bool cubeHeld{};
    bool cubeGrounded{true};
    bool buttonOn{};
    bool doorOpen{};
    float doorOpenT{};
    bool armoryLooted{};
    bool nearCube{};
    bool nearArmory{};

    void reset() {
        cubePos = {2.2f, .42f, 14.2f};
        cubeVel = {};
        cubeHeld = false;
        cubeGrounded = true;
        buttonOn = false;
        doorOpen = false;
        doorOpenT = 0.f;
        armoryLooted = false;
        nearCube = false;
        nearArmory = false;
    }
    bool doorBlocking() const { return doorOpenT < .72f; }
};

inline constexpr Vec3 kPracticeTargets[] = {
    {0.f, 1.15f, 4.2f},     // mid
    {11.f, 1.15f, -8.f},    // long A
    {2.2f, 1.2f, -19.2f},   // A site
    {-8.2f, 1.2f, -15.2f},  // B site
    {5.8f, 1.9f, -12.f},    // cat
};

inline constexpr int kPracticeTargetCount = 5;

// CPU mirror of the raytraced world used for portal placement and hitscan.
inline float sceneDistance(int zone, Vec3 p, std::uint32_t destroyedMask, std::uint32_t targetMask,
                           int* materialOut = nullptr, const PuzzleState* puzzle = nullptr) {
    float best = p.y;
    int material = 1;
    auto take = [&](float d, int mat) {
        if (d < best) {
            best = d;
            material = mat;
        }
    };

    if (zone == 9) {
        // Coarse exterior bound: rays far from the playable volume skip detail.
        const float bound = sdBox(p - Vec3{0.f, 3.f, -1.f}, {20.f, 9.f, 28.f});
        if (bound > 1.5f) {
            if (materialOut) *materialOut = 1;
            return bound;
        }

        take(sdBox(p - Vec3{0.f, 4.f, 24.5f}, {22.f, 5.f, .6f}), 19);
        take(sdBox(p - Vec3{0.f, 4.f, -26.5f}, {22.f, 5.f, .6f}), 19);
        take(sdBox(p - Vec3{-18.5f, 4.f, -1.f}, {.6f, 5.f, 26.f}), 19);
        take(sdBox(p - Vec3{18.5f, 4.f, -1.f}, {.6f, 5.f, 26.f}), 19);

        take(sdBox(p - Vec3{-5.2f, 1.4f, 16.f}, {.25f, 1.4f, 3.2f}), 19);
        take(sdBox(p - Vec3{5.2f, 1.4f, 16.f}, {.25f, 1.4f, 3.2f}), 19);
        take(sdBox(p - Vec3{0.f, 1.4f, 19.4f}, {5.4f, 1.4f, .25f}), 19);
        if ((destroyedMask & 1u) == 0) take(sdBox(p - Vec3{-3.4f, .55f, 13.2f}, {.7f, .55f, .55f}), 5);
        if ((destroyedMask & 2u) == 0) take(sdBox(p - Vec3{3.2f, .55f, 13.4f}, {.6f, .55f, .5f}), 5);

        take(sdBox(p - Vec3{-3.6f, 2.f, 6.5f}, {.35f, 2.f, 3.f}), 19);
        take(sdBox(p - Vec3{3.6f, 2.f, 6.5f}, {.35f, 2.f, 3.f}), 19);
        take(sdBox(p - Vec3{-3.6f, 2.f, 1.2f}, {.35f, 2.f, 1.4f}), 19);
        take(sdBox(p - Vec3{3.6f, 2.f, 1.2f}, {.35f, 2.f, 1.4f}), 19);
        take(sdBox(p - Vec3{-3.6f, 2.f, -3.6f}, {.35f, 2.f, 1.2f}), 19);
        take(sdBox(p - Vec3{3.6f, 2.f, -3.6f}, {.35f, 2.f, 1.2f}), 19);
        take(sdBox(p - Vec3{-1.55f, 1.8f, .6f}, {.18f, 1.8f, .12f}), 17);
        take(sdBox(p - Vec3{1.55f, 1.8f, .6f}, {.18f, 1.8f, .12f}), 17);
        take(sdBox(p - Vec3{0.f, 3.55f, .6f}, {1.75f, .18f, .12f}), 17);
        if ((destroyedMask & 4u) == 0) take(sdBox(p - Vec3{-1.1f, .45f, 2.2f}, {.55f, .45f, .45f}), 5);
        if ((destroyedMask & 8u) == 0) take(sdBox(p - Vec3{1.2f, .45f, -1.f}, {.5f, .45f, .5f}), 5);

        for (int i = 0; i < 5; ++i) {
            const float fi = static_cast<float>(i);
            const float h = .18f + fi * .28f;
            take(sdBox(p - Vec3{1.2f, h, -5.f - fi * .55f}, {2.4f, h, .32f}), 5);
        }
        take(sdBox(p - Vec3{4.8f, 1.55f, -8.2f}, {1.1f, .18f, 2.2f}), 5);

        take(sdBox(p - Vec3{8.2f, 2.2f, -2.f}, {.35f, 2.2f, 8.5f}), 19);
        take(sdBox(p - Vec3{13.6f, 2.2f, -6.f}, {.35f, 2.2f, 12.f}), 19);
        take(sdBox(p - Vec3{10.9f, 2.2f, 6.2f}, {2.9f, 2.2f, .35f}), 19);
        take(sdBox(p - Vec3{10.9f, 2.2f, -18.2f}, {2.9f, 2.2f, .35f}), 19);
        if ((destroyedMask & 16u) == 0) take(sdBox(p - Vec3{10.f, .5f, -4.f}, {.55f, .5f, .55f}), 5);
        if ((destroyedMask & 32u) == 0) take(sdBox(p - Vec3{11.2f, .5f, -10.f}, {.6f, .5f, .5f}), 5);
        if ((destroyedMask & 64u) == 0) take(sdBox(p - Vec3{9.8f, .5f, -14.5f}, {.55f, .5f, .55f}), 5);

        // Xbox-like crate near A default
        take(sdBox(p - Vec3{5.5f, .65f, -12.f}, {.9f, .65f, .9f}), 5);
        take(sdBox(p - Vec3{5.5f, 1.55f, -12.f}, {.55f, .35f, .55f}), 5);
        take(sdBox(p - Vec3{7.2f, 1.15f, -14.5f}, {1.f, .18f, 1.6f}), 5);
        take(sdBox(p - Vec3{6.4f, .55f, -16.2f}, {.7f, .55f, .7f}), 5);
        take(sdBox(p - Vec3{-.2f, .55f, -17.8f}, {.85f, .55f, .55f}), 5); // goose-ish

        take(sdBox(p - Vec3{2.f, .12f, -20.f}, {4.5f, .12f, 3.2f}), 5);
        if ((destroyedMask & 128u) == 0) take(sdBox(p - Vec3{-1.2f, .7f, -18.5f}, {.7f, .7f, .55f}), 5);
        if ((destroyedMask & 256u) == 0) take(sdBox(p - Vec3{4.8f, .7f, -21.5f}, {.65f, .7f, .55f}), 5);
        take(sdBox(p - Vec3{0.f, 1.6f, -23.5f}, {5.5f, 1.6f, .3f}), 19);
        take(sdRoundBox(p - Vec3{-3.5f, .85f, -20.5f}, {1.6f, .55f, .7f}, .08f), 13);

        take(sdBox(p - Vec3{-7.5f, 2.f, 8.f}, {.35f, 2.f, 3.f}), 19);
        take(sdBox(p - Vec3{-7.5f, 2.f, 1.5f}, {.35f, 2.f, 2.f}), 19);
        take(sdBox(p - Vec3{-12.5f, 2.f, 2.f}, {.35f, 2.f, 9.f}), 19);
        take(sdBox(p - Vec3{-10.f, 2.f, 10.8f}, {2.8f, 2.f, .35f}), 19);
        take(sdBox(p - Vec3{-10.f, 3.55f, 2.f}, {2.8f, .2f, 9.f}), 19);
        take(sdBox(p - Vec3{-11.6f, 2.f, -7.2f}, {1.2f, 2.f, .35f}), 19);
        take(sdBox(p - Vec3{-8.4f, 2.f, -7.2f}, {1.2f, 2.f, .35f}), 19);
        if ((destroyedMask & 512u) == 0) take(sdBox(p - Vec3{-8.5f, .5f, 4.f}, {.5f, .5f, .5f}), 5);

        take(sdBox(p - Vec3{-8.f, .12f, -15.5f}, {4.f, .12f, 3.5f}), 5);
        take(sdBox(p - Vec3{-11.5f, 1.4f, -12.f}, {.3f, 1.4f, 3.f}), 19);
        take(sdBox(p - Vec3{-4.5f, 1.4f, -15.5f}, {.3f, 1.4f, 3.5f}), 19);
        take(sdBox(p - Vec3{-8.f, 1.4f, -19.2f}, {4.f, 1.4f, .3f}), 19);
        if ((destroyedMask & 1024u) == 0) take(sdBox(p - Vec3{-6.2f, .55f, -14.f}, {.7f, .55f, .55f}), 5);
        if ((destroyedMask & 2048u) == 0) take(sdBox(p - Vec3{-9.5f, .55f, -16.8f}, {.65f, .55f, .6f}), 5);
        take(sdRoundBox(p - Vec3{-10.5f, .8f, -14.2f}, {1.4f, .5f, .65f}, .07f), 13);

        take(sdBox(p - Vec3{14.f, 2.f, -21.f}, {2.5f, 2.f, 2.f}), 19);
        take(sdBox(p - Vec3{12.2f, 1.1f, -18.8f}, {.2f, 1.1f, 1.2f}), 19);

        // Bomb-site pads (emissive markers)
        take(sdCylinderY(p - Vec3{2.f, .05f, -20.f}, 1.1f, .05f), 28);
        take(sdCylinderY(p - Vec3{-8.f, .05f, -15.5f}, 1.0f, .05f), 29);

        // Mid doors + long pit + palms (visuals mirrored lightly for hitscan)
        take(sdBox(p - Vec3{-.85f, 1.5f, .55f}, {.55f, 1.5f, .06f}), 17);
        take(sdBox(p - Vec3{.85f, 1.5f, .55f}, {.55f, 1.5f, .06f}), 17);
        take(sdBox(p - Vec3{10.9f, -.15f, -11.5f}, {2.4f, .35f, 1.6f}), 5);

        if (puzzle) {
            take(sdCylinderY(p - Vec3{0.f, .06f, 5.4f}, .7f, .06f), puzzle->buttonOn ? 31 : 30);
            if (puzzle->doorBlocking())
                take(sdBox(p - Vec3{13.2f, 1.5f, -8.f}, {.18f, 1.5f, 1.4f}), 32);
            take(sdBox(p - Vec3{16.4f, 2.f, -8.f}, {.25f, 2.f, 1.6f}), 19);
            take(sdBox(p - Vec3{14.8f, 2.f, -6.4f}, {1.4f, 2.f, .25f}), 19);
            take(sdBox(p - Vec3{14.8f, 2.f, -9.6f}, {1.4f, 2.f, .25f}), 19);
            if (!puzzle->cubeHeld)
                take(sdRoundBox(p - puzzle->cubePos, {.32f, .32f, .32f}, .04f), 33);
        }

        for (int i = 0; i < kPracticeTargetCount; ++i) {
            if (targetMask & (1u << i)) continue;
            const Vec3 c = kPracticeTargets[i];
            take(sdCylinderY(p - (c + Vec3{0.f, -.55f, 0.f}), .05f, .55f), 17);
            take(sdCylinderY(p - c, .28f, .32f), 27);
        }
    } else {
        take(5.35f - p.y, 2);
        take(11.8f - std::abs(p.x), 3);
        take(25.f + p.z, 3);
        take(11.f - p.z, 3);
        if (zone == 0) {
            take(sdBox(p - Vec3{-1.55f, 2.2f, -15.f}, {.16f, 2.2f, .2f}), 10);
            take(sdBox(p - Vec3{1.55f, 2.2f, -15.f}, {.16f, 2.2f, .2f}), 10);
            take(sdBox(p - Vec3{0.f, 4.4f, -15.f}, {1.7f, .16f, .2f}), 10);
        } else if (zone == 4) {
            take(sdBox(p - Vec3{0.f, .55f, 0.f}, {1.1f, .55f, 1.1f}), 14);
        } else if (zone == 7) {
            take(sdBox(p - Vec3{-6.f, .45f, 0.f}, {1.8f, .45f, .48f}), 14);
            take(sdBox(p - Vec3{6.f, .45f, 0.f}, {1.8f, .45f, .48f}), 14);
        }
    }

    if (materialOut) *materialOut = material;
    return best;
}

inline Vec3 sceneNormal(int zone, Vec3 p, std::uint32_t destroyedMask, std::uint32_t targetMask,
                        const PuzzleState* puzzle = nullptr) {
    constexpr float e = .004f;
    const float d = sceneDistance(zone, p, destroyedMask, targetMask, nullptr, puzzle);
    return vnormalize({
        sceneDistance(zone, p + Vec3{e, 0, 0}, destroyedMask, targetMask, nullptr, puzzle) - d,
        sceneDistance(zone, p + Vec3{0, e, 0}, destroyedMask, targetMask, nullptr, puzzle) - d,
        sceneDistance(zone, p + Vec3{0, 0, e}, destroyedMask, targetMask, nullptr, puzzle) - d});
}

inline RayHit sceneRaycast(int zone, Vec3 ro, Vec3 rd, std::uint32_t destroyedMask,
                           std::uint32_t targetMask, float maxDist = 48.f,
                           const PuzzleState* puzzle = nullptr) {
    RayHit result;
    float t = .05f;
    rd = vnormalize(rd);
    for (int i = 0; i < 140; ++i) {
        const Vec3 p = ro + rd * t;
        int material = -1;
        const float d = sceneDistance(zone, p, destroyedMask, targetMask, &material, puzzle);
        if (d < .012f) {
            result.hit = true;
            result.t = t;
            result.pos = p;
            result.normal = sceneNormal(zone, p, destroyedMask, targetMask, puzzle);
            if (vdot(result.normal, rd) > 0.f) result.normal = -result.normal;
            result.material = material;
            return result;
        }
        t += std::max(d * .82f, .018f);
        if (t > maxDist) break;
    }
    return result;
}

inline bool tryDestroyCover(std::uint32_t& destroyedMask, Vec3 hitPos) {
    static constexpr Vec3 kCovers[] = {
        {-3.4f, .55f, 13.2f}, {3.2f, .55f, 13.4f}, {-1.1f, .45f, 2.2f}, {1.2f, .45f, -1.f},
        {10.f, .5f, -4.f}, {11.2f, .5f, -10.f}, {9.8f, .5f, -14.5f}, {-1.2f, .7f, -18.5f},
        {4.8f, .7f, -21.5f}, {-8.5f, .5f, 4.f}, {-6.2f, .55f, -14.f}, {-9.5f, .55f, -16.8f}};
    for (int i = 0; i < 12; ++i) {
        const Vec3 d = hitPos - kCovers[i];
        if (vdot(d, d) < 1.35f) {
            const std::uint32_t bit = 1u << i;
            if (destroyedMask & bit) return false;
            destroyedMask |= bit;
            return true;
        }
    }
    return false;
}

inline bool tryHitTarget(std::uint32_t& targetMask, Vec3 hitPos) {
    for (int i = 0; i < kPracticeTargetCount; ++i) {
        const Vec3 d = hitPos - kPracticeTargets[i];
        if (vdot(d, d) < .55f) {
            const std::uint32_t bit = 1u << i;
            if (targetMask & bit) return false;
            targetMask |= bit;
            return true;
        }
    }
    return false;
}

enum class WeaponId { PortalGun = 0, Usp = 1, Ak47 = 2 };

struct WeaponDef {
    const char* name;
    int magSize;
    int reserve;
    float fireInterval;
    float reloadTime;
    bool automatic;
    float baseSpread;   // radians
    float moveSpread;
    float recoilPitch;
    float recoilYaw;
};

inline constexpr WeaponDef kWeapons[] = {
    {"传送枪", 0, 0, .16f, 0.f, false, 0.f, 0.f, .006f, .003f},
    {"USP", 12, 36, .2f, 1.45f, false, .004f, .012f, .024f, .01f},
    {"AK-47", 30, 90, .095f, 2.15f, true, .01f, .028f, .038f, .018f},
};

struct PortalDisk {
    bool active{};
    Vec3 pos{};
    Vec3 normal{0, 0, 1};
};

struct WeaponLoadout {
    WeaponId current{WeaponId::PortalGun};
    WeaponId previous{WeaponId::Usp};
    int ammoMag[3]{0, 12, 30};
    int ammoReserve[3]{0, 36, 90};
    float cooldown{};
    float reloadLeft{};
    float muzzle{};
    float recoil{};
    float hitMarker{};
    float sway{};
    float swapT{};
    float denyFlash{};
    float viewPunch{};
    bool reloading{};
    PortalDisk blue{};
    PortalDisk orange{};
    float portalCooldown{};
    Vec3 impactPos[3]{};
    float impactLife[3]{};
    int impactCursor{};
    std::uint32_t destroyedMask{};
    std::uint32_t targetMask{};
    int targetsDown{};

    void select(WeaponId id) {
        if (id == current) return;
        previous = current;
        current = id;
        reloading = false;
        reloadLeft = 0.f;
        cooldown = .1f;
        sway = 1.f;
        swapT = 1.f;
    }
    void selectPrevious() { select(previous); }
    void cycle(int delta) {
        int idx = (static_cast<int>(current) + delta + 3) % 3;
        select(static_cast<WeaponId>(idx));
    }
    void clearPortals() {
        blue.active = false;
        orange.active = false;
    }
    void pushImpact(Vec3 pos) {
        impactPos[impactCursor] = pos;
        impactLife[impactCursor] = 1.f;
        impactCursor = (impactCursor + 1) % 3;
    }
    void resetArena() {
        destroyedMask = 0;
        targetMask = 0;
        targetsDown = 0;
        clearPortals();
        for (float& life : impactLife) life = 0.f;
    }
};
