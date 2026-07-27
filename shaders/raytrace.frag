#version 330 core

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCamera;
uniform vec2 uYawPitch;
uniform int uZone;
uniform int uCollectedMask;
uniform int uShowHud;
uniform int uQuality; // 0 low, 1 medium, 2 high
uniform sampler2D uUiTexture;
uniform vec2 uSealPos;
uniform int uWeapon;          // 0 portal gun, 1 USP, 2 AK
uniform float uMuzzle;
uniform float uRecoil;
uniform int uPortalBlueOn;
uniform int uPortalOrangeOn;
uniform vec3 uPortalBluePos;
uniform vec3 uPortalBlueN;
uniform vec3 uPortalOrangePos;
uniform vec3 uPortalOrangeN;
uniform vec3 uImpactPos;
uniform float uImpactLife;
uniform vec3 uImpactPos1;
uniform float uImpactLife1;
uniform vec3 uImpactPos2;
uniform float uImpactLife2;
uniform int uDestroyedMask;
uniform int uTargetMask;
uniform float uHitMarker;
uniform float uSpread;
uniform float uMoveSway;

#define MAX_STEPS 92
#define MAX_DIST 70.0
#define SURF 0.0025

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdRoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

float sdSphere(vec3 p, float r) { return length(p) - r; }

float sdCylinder(vec3 p, float r, float h) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

vec2 take(vec2 a, float d, float material) { return d < a.x ? vec2(d, material) : a; }

vec2 commonRoom(vec3 p) {
    vec2 r = vec2(p.y, 1.0);
    r = take(r, 5.35 - p.y, 2.0);
    r = take(r, 11.8 - abs(p.x), 3.0);
    return r;
}

vec2 sealGeometry(vec3 p, vec2 r) {
    if (uZone == 0) return r;
    int bit = 1 << (uZone - 1);
    if ((uCollectedMask & bit) != 0) return r;
    vec3 q = p - vec3(uSealPos.x, 1.15, uSealPos.y);
    float ring = sdTorus(q.xzy, vec2(0.46, 0.075));
    float stamp = sdRoundBox(q, vec3(.21, .21, .045), .035);
    return take(r, min(ring, stamp), 21.0);
}

vec2 mapHub(vec3 p, vec2 r) {
    vec3 q = p;
    q.z = mod(q.z + 1.8, 6.0) - 3.0;
    for (float sx = -1.0; sx <= 1.0; sx += 2.0) {
        r = take(r, sdBox(q - vec3(sx * 5.2, 2.55, 0.0), vec3(.42, 2.55, .42)), 4.0);
    }
    vec3 l = p; l.z = mod(l.z + 1.2, 5.5) - 2.75;
    r = take(r, sdRoundBox(l - vec3(0.0, 5.20, 0.0), vec3(2.0, .055, .18), .04), 22.0);
    vec3 gate = p - vec3(0.0, 2.2, -15.0);
    float portal = min(sdBox(gate - vec3(-1.55, 0.0, 0.0), vec3(.16, 2.2, .2)),
                       sdBox(gate - vec3( 1.55, 0.0, 0.0), vec3(.16, 2.2, .2)));
    portal = min(portal, sdBox(gate - vec3(0.0, 2.2, 0.0), vec3(1.7, .16, .2)));
    r = take(r, portal, 10.0);
    return r;
}

vec2 mapBeijing(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 1.0, 6.4) - 3.2;
    r = take(r, sdBox(q - vec3(-11.35, 2.0, 0.0), vec3(.22, 1.85, 1.75)), 10.0);
    r = take(r, sdBox(q - vec3( 11.35, 2.0, 0.0), vec3(.22, 1.85, 1.75)), 10.0);
    r = take(r, sdSphere(q - vec3(-10.55, 3.75, .3), .27), 21.0);
    r = take(r, sdSphere(q - vec3( 10.55, 3.75, .3), .27), 21.0);
    for (float x = -6.0; x <= 6.0; x += 4.0)
        r = take(r, sdCylinder(q - vec3(x, .9, -2.3), .09, .9), 17.0);
    return r;
}

vec2 mapJiangnan(vec3 p, vec2 r) {
    r = take(r, abs(p.y - .025) - .018 + step(2.7, abs(p.x)) * 10.0, 18.0);
    vec3 q = p; q.z = mod(q.z + 1.0, 6.0) - 3.0;
    r = take(r, sdBox(q - vec3(-8.2, 1.65, 0.0), vec3(3.2, 1.65, 1.9)), 11.0);
    r = take(r, sdBox(q - vec3( 8.2, 1.65, 0.0), vec3(3.2, 1.65, 1.9)), 11.0);
    r = take(r, sdBox(q - vec3(-8.2, 3.4, 0.0), vec3(3.6, .14, 2.25)), 6.0);
    r = take(r, sdBox(q - vec3( 8.2, 3.4, 0.0), vec3(3.6, .14, 2.25)), 6.0);
    r = take(r, sdSphere(q - vec3(-4.1, 3.6, .2), .23), 21.0);
    // The central stone bridge rises in shallow steps.
    for (int i = -4; i <= 4; ++i) {
        float fi = float(i);
        float h = .12 + .72 * (1.0 - abs(fi) / 5.0);
        r = take(r, sdBox(p - vec3(fi * .62, h, -10.0), vec3(.34, h, 2.0)), 4.0);
    }
    return r;
}

vec2 mapChongqing(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 2.0, 7.0) - 3.5;
    r = take(r, sdBox(q - vec3(-7.0, 1.75, 0.0), vec3(4.7, .18, 1.8)), 5.0);
    r = take(r, sdBox(q - vec3( 7.0, 3.85, 1.4), vec3(4.7, .18, 1.6)), 5.0);
    for (int i = 0; i < 7; ++i) {
        float fi = float(i);
        r = take(r, sdBox(q - vec3(0.0, .18 + fi * .28, 2.8 - fi * .70), vec3(2.2, .15, .38)), 5.0);
    }
    r = take(r, sdRoundBox(q - vec3(-11.45, 2.7, 0.0), vec3(.035, 1.25, .08), .02), 22.0);
    r = take(r, sdRoundBox(q - vec3( 11.45, 3.3, 1.5), vec3(.035, .75, .08), .02), 23.0);
    return r;
}

vec2 mapSichuan(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 1.0, 5.8) - 2.9;
    for (float x = -6.5; x <= 6.5; x += 6.5) {
        r = take(r, sdCylinder(q - vec3(x, .78, 0.0), .95, .07), 13.0);
        r = take(r, sdCylinder(q - vec3(x, .39, 0.0), .10, .39), 13.0);
        r = take(r, sdBox(q - vec3(x - 1.45, .65, 0.0), vec3(.42, .65, .42)), 13.0);
        r = take(r, sdBox(q - vec3(x + 1.45, .65, 0.0), vec3(.42, .65, .42)), 13.0);
        r = take(r, sdCylinder(q - vec3(x, .91, 0.0), .20, .07), 11.0);
    }
    r = take(r, sdSphere(q - vec3(-10.6, 4.15, 0.0), .3), 21.0);
    r = take(r, sdSphere(q - vec3( 10.6, 4.15, 0.0), .3), 21.0);
    return r;
}

vec2 mapXian(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 1.0, 5.0) - 2.5;
    for (float x = -8.0; x <= 8.0; x += 2.0) {
        if(abs(x)>.5){
            r = take(r, sdRoundBox(q - vec3(x, .85, -1.7), vec3(.35, .68, .28), .08), 14.0);
            r = take(r, sdSphere(q - vec3(x, 1.75, -1.7), .29), 14.0);
        }
    }
    float arch = abs(sdTorus((q - vec3(0.0, 0.2, 0.0)).xzy, vec2(4.2, .25))) - .08;
    arch = max(arch, -(q.y - 1.0));
    r = take(r, arch, 14.0);
    r = take(r, sdBox(q - vec3(-4.2, 1.6, 0.0), vec3(.36, 1.6, .38)), 14.0);
    r = take(r, sdBox(q - vec3( 4.2, 1.6, 0.0), vec3(.36, 1.6, .38)), 14.0);
    return r;
}

vec2 mapDunhuang(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 1.0, 6.0) - 3.0;
    float arch = abs(sdTorus((q - vec3(0.0, .3, 0.0)).xzy, vec2(5.2, .34))) - .03;
    arch = max(arch, -(q.y - .8));
    r = take(r, arch, 14.0);
    float ribbon = sdTorus(vec3(q.x, q.y - 3.55 - sin(q.z) * .25, q.z * .23), vec2(3.0, .07));
    r = take(r, ribbon, 23.0);
    r = take(r, sdSphere(q - vec3(-7.8, 3.0, 0.0), .42), 17.0);
    r = take(r, sdSphere(q - vec3( 7.8, 3.3, 1.3), .32), 17.0);
    return r;
}

vec2 mapDongbei(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 1.0, 6.0) - 3.0;
    for (float x = -6.0; x <= 6.0; x += 12.0) {
        r = take(r, sdBox(q - vec3(x, .65, 0.0), vec3(1.8, .12, .48)), 13.0);
        r = take(r, sdBox(q - vec3(x, 1.2, .40), vec3(1.8, .65, .10)), 13.0);
        r = take(r, sdBox(q - vec3(x - 1.5, .32, 0.0), vec3(.1, .32, .4)), 13.0);
        r = take(r, sdBox(q - vec3(x + 1.5, .32, 0.0), vec3(.1, .32, .4)), 13.0);
    }
    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        r = take(r, sdCylinder(q.xzy - vec3(-11.45, q.z, -.1 + fi * .24), .075, .56), 17.0);
    }
    r = take(r, sdRoundBox(q - vec3(0.0, 5.18, 0.0), vec3(2.0, .055, .18), .04), 24.0);
    return r;
}

vec2 mapLingnan(vec3 p, vec2 r) {
    vec3 q = p; q.z = mod(q.z + 1.0, 5.2) - 2.6;
    r = take(r, sdCylinder(q - vec3(-9.2, 2.4, 0.0), .36, 2.4), 11.0);
    r = take(r, sdCylinder(q - vec3( 9.2, 2.4, 0.0), .36, 2.4), 11.0);
    r = take(r, sdBox(q - vec3(-10.7, 3.0, 0.0), vec3(.08, 1.3, 1.5)), 16.0 + mod(floor((p.z + 20.0) / 5.2), 3.0));
    r = take(r, sdCylinder(q - vec3(7.5, .42, 1.5), .48, .42), 14.0);
    r = take(r, sdSphere(q - vec3(7.5, 1.25, 1.5), .72), 16.0);
    r = take(r, sdSphere(q - vec3(-10.2, 4.1, -1.6), .28), 21.0);
    return r;
}

bool coverAlive(int bit) { return (uDestroyedMask & (1 << bit)) == 0; }
bool targetAlive(int bit) { return (uTargetMask & (1 << bit)) == 0; }

float targetProp(vec3 p, vec3 c) {
    float stand = sdCylinder(p - (c + vec3(0.0, -.55, 0.0)), .05, .55);
    float head = sdCylinder(p - c, .28, .32);
    return min(stand, head);
}

// Compressed Dust II: T spawn south, A north-east, B north-west, mid doors center.
vec2 mapDust2(vec3 p) {
    // Cheap exterior bound — most sky/background rays skip the full layout.
    float bound = sdBox(p - vec3(0.0, 3.0, -1.0), vec3(20.0, 9.0, 28.0));
    if (bound > 1.6) return vec2(bound, 1.0);

    vec2 r = vec2(p.y, 1.0); // desert floor
    // Outer cliff walls keep the player in-bounds without a corridor ceiling.
    r = take(r, sdBox(p - vec3(0.0, 4.0, 24.5), vec3(22.0, 5.0, .6)), 19.0);
    r = take(r, sdBox(p - vec3(0.0, 4.0, -26.5), vec3(22.0, 5.0, .6)), 19.0);
    r = take(r, sdBox(p - vec3(-18.5, 4.0, -1.0), vec3(.6, 5.0, 26.0)), 19.0);
    r = take(r, sdBox(p - vec3( 18.5, 4.0, -1.0), vec3(.6, 5.0, 26.0)), 19.0);

    // T spawn pit walls
    r = take(r, sdBox(p - vec3(-5.2, 1.4, 16.0), vec3(.25, 1.4, 3.2)), 19.0);
    r = take(r, sdBox(p - vec3( 5.2, 1.4, 16.0), vec3(.25, 1.4, 3.2)), 19.0);
    r = take(r, sdBox(p - vec3(0.0, 1.4, 19.4), vec3(5.4, 1.4, .25)), 19.0);
    if (coverAlive(0)) r = take(r, sdBox(p - vec3(-3.4, .55, 13.2), vec3(.7, .55, .55)), 5.0);
    if (coverAlive(1)) r = take(r, sdBox(p - vec3( 3.2, .55, 13.4), vec3(.6, .55, .5)), 5.0);

    // Mid doors corridor
    r = take(r, sdBox(p - vec3(-3.6, 2.0, 6.5), vec3(.35, 2.0, 3.0)), 19.0);
    r = take(r, sdBox(p - vec3( 3.6, 2.0, 6.5), vec3(.35, 2.0, 3.0)), 19.0);
    r = take(r, sdBox(p - vec3(-3.6, 2.0, 1.2), vec3(.35, 2.0, 1.4)), 19.0);
    r = take(r, sdBox(p - vec3( 3.6, 2.0, 1.2), vec3(.35, 2.0, 1.4)), 19.0);
    r = take(r, sdBox(p - vec3(-3.6, 2.0, -3.6), vec3(.35, 2.0, 1.2)), 19.0);
    r = take(r, sdBox(p - vec3( 3.6, 2.0, -3.6), vec3(.35, 2.0, 1.2)), 19.0);
    r = take(r, sdBox(p - vec3(-1.55, 1.8, .6), vec3(.18, 1.8, .12)), 17.0);
    r = take(r, sdBox(p - vec3( 1.55, 1.8, .6), vec3(.18, 1.8, .12)), 17.0);
    r = take(r, sdBox(p - vec3(0.0, 3.55, .6), vec3(1.75, .18, .12)), 17.0);
    if (coverAlive(2)) r = take(r, sdBox(p - vec3(-1.1, .45, 2.2), vec3(.55, .45, .45)), 5.0);
    if (coverAlive(3)) r = take(r, sdBox(p - vec3( 1.2, .45, -1.0), vec3(.5, .45, .5)), 5.0);

    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float h = .18 + fi * .28;
        r = take(r, sdBox(p - vec3(1.2, h, -5.0 - fi * .55), vec3(2.4, h, .32)), 5.0);
    }
    r = take(r, sdBox(p - vec3(4.8, 1.55, -8.2), vec3(1.1, .18, 2.2)), 5.0);

    // Long A corridor (east)
    r = take(r, sdBox(p - vec3(8.2, 2.2, -2.0), vec3(.35, 2.2, 8.5)), 19.0);
    r = take(r, sdBox(p - vec3(13.6, 2.2, -6.0), vec3(.35, 2.2, 12.0)), 19.0);
    r = take(r, sdBox(p - vec3(10.9, 2.2, 6.2), vec3(2.9, 2.2, .35)), 19.0);
    r = take(r, sdBox(p - vec3(10.9, 2.2, -18.2), vec3(2.9, 2.2, .35)), 19.0);
    if (coverAlive(4)) r = take(r, sdBox(p - vec3(10.0, .5, -4.0), vec3(.55, .5, .55)), 5.0);
    if (coverAlive(5)) r = take(r, sdBox(p - vec3(11.2, .5, -10.0), vec3(.6, .5, .5)), 5.0);
    if (coverAlive(6)) r = take(r, sdBox(p - vec3(9.8, .5, -14.5), vec3(.55, .5, .55)), 5.0);

    // Short A / catwalk / Xbox
    r = take(r, sdBox(p - vec3(5.5, .65, -12.0), vec3(.9, .65, .9)), 5.0);
    r = take(r, sdBox(p - vec3(5.5, 1.55, -12.0), vec3(.55, .35, .55)), 5.0);
    r = take(r, sdBox(p - vec3(7.2, 1.15, -14.5), vec3(1.0, .18, 1.6)), 5.0);
    r = take(r, sdBox(p - vec3(6.4, .55, -16.2), vec3(.7, .55, .7)), 5.0);
    r = take(r, sdBox(p - vec3(-0.2, .55, -17.8), vec3(.85, .55, .55)), 5.0); // goose

    // A site platform + CT-side cover
    r = take(r, sdBox(p - vec3(2.0, .12, -20.0), vec3(4.5, .12, 3.2)), 5.0);
    if (coverAlive(7)) r = take(r, sdBox(p - vec3(-1.2, .7, -18.5), vec3(.7, .7, .55)), 5.0);
    if (coverAlive(8)) r = take(r, sdBox(p - vec3(4.8, .7, -21.5), vec3(.65, .7, .55)), 5.0);
    r = take(r, sdBox(p - vec3(0.0, 1.6, -23.5), vec3(5.5, 1.6, .3)), 19.0);
    r = take(r, sdRoundBox(p - vec3(-3.5, .85, -20.5), vec3(1.6, .55, .7), .08), 13.0);
    r = take(r, sdCylinder(p - vec3(2.0, .05, -20.0), 1.1, .05), 28.0); // A bomb pad

    // Upper tunnels into B
    r = take(r, sdBox(p - vec3(-7.5, 2.0, 8.0), vec3(.35, 2.0, 3.0)), 19.0);
    r = take(r, sdBox(p - vec3(-7.5, 2.0, 1.5), vec3(.35, 2.0, 2.0)), 19.0);
    r = take(r, sdBox(p - vec3(-12.5, 2.0, 2.0), vec3(.35, 2.0, 9.0)), 19.0);
    r = take(r, sdBox(p - vec3(-10.0, 2.0, 10.8), vec3(2.8, 2.0, .35)), 19.0);
    r = take(r, sdBox(p - vec3(-10.0, 3.55, 2.0), vec3(2.8, .2, 9.0)), 19.0);
    r = take(r, sdBox(p - vec3(-11.6, 2.0, -7.2), vec3(1.2, 2.0, .35)), 19.0);
    r = take(r, sdBox(p - vec3(-8.4, 2.0, -7.2), vec3(1.2, 2.0, .35)), 19.0);
    if (coverAlive(9)) r = take(r, sdBox(p - vec3(-8.5, .5, 4.0), vec3(.5, .5, .5)), 5.0);

    // B site
    r = take(r, sdBox(p - vec3(-8.0, .12, -15.5), vec3(4.0, .12, 3.5)), 5.0);
    r = take(r, sdBox(p - vec3(-11.5, 1.4, -12.0), vec3(.3, 1.4, 3.0)), 19.0);
    r = take(r, sdBox(p - vec3(-4.5, 1.4, -15.5), vec3(.3, 1.4, 3.5)), 19.0);
    r = take(r, sdBox(p - vec3(-8.0, 1.4, -19.2), vec3(4.0, 1.4, .3)), 19.0);
    if (coverAlive(10)) r = take(r, sdBox(p - vec3(-6.2, .55, -14.0), vec3(.7, .55, .55)), 5.0);
    if (coverAlive(11)) r = take(r, sdBox(p - vec3(-9.5, .55, -16.8), vec3(.65, .55, .6)), 5.0);
    r = take(r, sdRoundBox(p - vec3(-10.5, .8, -14.2), vec3(1.4, .5, .65), .07), 13.0);
    r = take(r, sdCylinder(p - vec3(-8.0, .05, -15.5), 1.0, .05), 29.0); // B bomb pad

    // CT spawn building stub near A
    r = take(r, sdBox(p - vec3(14.0, 2.0, -21.0), vec3(2.5, 2.0, 2.0)), 19.0);
    r = take(r, sdBox(p - vec3(12.2, 1.1, -18.8), vec3(.2, 1.1, 1.2)), 19.0);

    r = take(r, sdCylinder(p - vec3(3.8, 1.8, -10.5), .08, 1.8), 17.0);
    r = take(r, sdCylinder(p - vec3(-5.5, 1.6, -13.0), .08, 1.6), 17.0);

    // Practice range targets
    if (targetAlive(0)) r = take(r, targetProp(p, vec3(0.0, 1.15, 4.2)), 27.0);
    if (targetAlive(1)) r = take(r, targetProp(p, vec3(11.0, 1.15, -8.0)), 27.0);
    if (targetAlive(2)) r = take(r, targetProp(p, vec3(2.2, 1.2, -19.2)), 27.0);
    if (targetAlive(3)) r = take(r, targetProp(p, vec3(-8.2, 1.2, -15.2)), 27.0);
    if (targetAlive(4)) r = take(r, targetProp(p, vec3(5.8, 1.9, -12.0)), 27.0);
    return r;
}

vec3 portalBasisRight(vec3 n) {
    vec3 up = abs(n.y) > .92 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    return normalize(cross(up, n));
}

float portalDisk(vec3 p, vec3 center, vec3 n) {
    vec3 d = p - center;
    float plane = abs(dot(d, n));
    vec3 rgt = portalBasisRight(n);
    vec3 upv = cross(n, rgt);
    float x = dot(d, rgt);
    float y = dot(d, upv);
    vec2 oval = vec2(x / .58, y / .95);
    float radial = length(oval);
    float ang = atan(oval.y, oval.x);
    float swirl = .035 * sin(ang * 6.0 + uTime * 5.5);
    float rim = abs(radial - (1.0 + swirl)) - .055;
    float face = max(plane - .02, radial - 1.02);
    float ring = max(rim, plane - .05);
    return min(ring, face);
}

vec2 mapPortals(vec3 p, vec2 r) {
    if (uPortalBlueOn != 0) {
        r = take(r, portalDisk(p, uPortalBluePos, normalize(uPortalBlueN)), 24.0);
    }
    if (uPortalOrangeOn != 0) {
        r = take(r, portalDisk(p, uPortalOrangePos, normalize(uPortalOrangeN)), 25.0);
    }
    if (uImpactLife > .01) r = take(r, sdSphere(p - uImpactPos, .06 + .04 * (1.0 - uImpactLife)), 26.0);
    if (uImpactLife1 > .01) r = take(r, sdSphere(p - uImpactPos1, .05 + .04 * (1.0 - uImpactLife1)), 26.0);
    if (uImpactLife2 > .01) r = take(r, sdSphere(p - uImpactPos2, .05 + .04 * (1.0 - uImpactLife2)), 26.0);
    return r;
}

vec2 mapScene(vec3 p) {
    vec2 r;
    if (uZone == 9) {
        r = mapDust2(p);
    } else {
        r = commonRoom(p);
        if      (uZone == 0) r = mapHub(p, r);
        else if (uZone == 1) r = mapBeijing(p, r);
        else if (uZone == 2) r = mapJiangnan(p, r);
        else if (uZone == 3) r = mapChongqing(p, r);
        else if (uZone == 4) r = mapSichuan(p, r);
        else if (uZone == 5) r = mapXian(p, r);
        else if (uZone == 6) r = mapDunhuang(p, r);
        else if (uZone == 7) r = mapDongbei(p, r);
        else if (uZone == 8) r = mapLingnan(p, r);
    }
    r = sealGeometry(p, r);
    return mapPortals(p, r);
}

vec2 trace(vec3 ro, vec3 rd, float maxDistance) {
    float total = 0.0;
    float material = -1.0;
    bool found = false;
    int stepLimit = uQuality > 1 ? 92 : (uQuality > 0 ? 76 : 58);
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (i >= stepLimit) break;
        vec2 hit = mapScene(ro + rd * total);
        material = hit.y;
        if (abs(hit.x) < SURF) { found = true; break; }
        if (total > maxDistance) break;
        total += max(hit.x * .82, .0018);
    }
    if (!found || total > maxDistance) material = -1.0;
    return vec2(total, material);
}

vec3 normalAt(vec3 p) {
    vec2 e = vec2(.003, 0.0);
    float d = mapScene(p).x;
    return normalize(vec3(mapScene(p + e.xyy).x - d,
                          mapScene(p + e.yxy).x - d,
                          mapScene(p + e.yyx).x - d));
}

float softShadow(vec3 ro, vec3 rd, float maxT) {
    float shade = 1.0;
    float t = .025;
    int shadowLimit = uQuality > 1 ? 30 : (uQuality > 0 ? 22 : 14);
    for (int i = 0; i < 30; ++i) {
        if (i >= shadowLimit) break;
        float h = mapScene(ro + rd * t).x;
        shade = min(shade, 13.0 * h / t);
        t += clamp(h, .02, .35);
        if (h < .002 || t > maxT) break;
    }
    return clamp(shade, 0.08, 1.0);
}

float ambientOcclusion(vec3 p, vec3 n) {
    float occ = 0.0, weight = 1.0;
    int aoLimit = uQuality > 1 ? 3 : (uQuality > 0 ? 2 : 1);
    for (int i = 1; i <= 3; ++i) {
        if (i > aoLimit) break;
        float d = float(i) * .12;
        occ += (d - mapScene(p + n * d).x) * weight;
        weight *= .55;
    }
    return clamp(1.0 - occ * 1.5, .15, 1.0);
}

vec3 palette(float id, vec3 p) {
    vec3 c = vec3(.32);
    if (id < 1.5) {
        if (uZone == 2) c = vec3(.12,.17,.17);
        else if (uZone == 6) c = vec3(.36,.19,.09);
        else if (uZone == 9) c = vec3(.52,.42,.28);
        else c = vec3(.25,.24,.20);
        float tile = step(.94, fract(p.x*.5)) + step(.94, fract(p.z*.5));
        c *= 1.0 - .18 * min(tile,1.0);
        if (uZone == 9) {
            float dune = .08 * sin(p.x * 1.7 + p.z * .9) * sin(p.z * 1.3);
            c *= 1.0 + dune;
        }
    } else if (id < 2.5) c = vec3(.08,.085,.075);
    else if (id < 3.5) {
        if (uZone == 1) c = vec3(.20,.19,.17);
        else if (uZone == 4) c = vec3(.12,.27,.22);
        else if (uZone == 6) c = vec3(.46,.25,.12);
        else if (uZone == 7) c = vec3(.12,.27,.29);
        else c = vec3(.43,.43,.37);
    } else if (id < 5.0) c = uZone == 9 ? vec3(.42,.35,.24) : vec3(.28,.27,.23);
    else if (id < 7.0) c = vec3(.17,.19,.18);
    else if (id < 10.5) c = vec3(.46,.075,.045);
    else if (id < 11.5) c = vec3(.66,.64,.54);
    else if (id < 14.0) c = uZone == 9 ? vec3(.18,.16,.14) : vec3(.25,.13,.065);
    else if (id < 15.0) c = vec3(.47,.25,.12);
    else if (id < 16.5) c = vec3(.12,.34,.24);
    else if (id < 17.5) c = uZone == 9 ? vec3(.35,.32,.28) : vec3(.49,.37,.19);
    else if (id < 18.5) c = vec3(.05,.23,.25);
    else if (id < 19.5) c = uZone == 9 ? vec3(.62,.48,.32) : vec3(.50,.30,.12);
    else if (id < 21.5) c = vec3(1.0,.08,.035);
    else if (id < 22.5) c = vec3(.2,1.0,1.0);
    else if (id < 23.5) c = vec3(1.0,.23,.55);
    else if (id < 24.5) c = vec3(.15,.55,1.0);   // blue portal
    else if (id < 25.5) c = vec3(1.0,.42,.08);    // orange portal
    else if (id < 26.5) c = vec3(.08,.07,.05);    // bullet scar
    else if (id < 27.5) c = vec3(.85,.18,.12);    // practice target
    else if (id < 28.5) c = vec3(.75,.12,.08);    // A site pad
    else if (id < 29.5) c = vec3(.12,.35,.75);    // B site pad
    else c = vec3(.65,.86,.95);
    if (id == 3.0 && uZone == 1) {
        float brick = step(.92, fract(p.y * 2.4)) + step(.94, fract((p.z + floor(p.y*2.4)*.3)*1.2));
        c *= 1.0 - .22 * min(brick, 1.0);
    }
    if (uZone == 9 && id > 18.5 && id < 19.5) {
        float adobe = step(.93, fract(p.y * 1.8)) + step(.94, fract((p.z + floor(p.y*1.8)*.25)*1.1));
        c *= 1.0 - .16 * min(adobe, 1.0);
    }
    return c;
}

float reflectivity(float id) {
    if (id > 17.5 && id < 18.5) return .72;
    if (id > 21.5) return .24;
    if (id < 1.5) return .07;
    return .06;
}

vec3 lightPosition() {
    if (uZone == 3) return vec3(-8.0, 4.3, uCamera.z - 3.0);
    if (uZone == 6) return vec3(0.0, 4.2, uCamera.z - 4.0);
    if (uZone == 9) return vec3(8.0, 12.0, uCamera.z + 4.0);
    return vec3(-4.5, 4.5, uCamera.z - 2.5);
}

vec3 skyColor(vec3 rd) {
    vec3 a = uZone == 3 ? vec3(.015,.055,.06) :
             uZone == 6 ? vec3(.07,.025,.008) :
             uZone == 9 ? vec3(.18,.12,.06) : vec3(.012,.016,.014);
    vec3 lift = uZone == 9 ? vec3(.35,.28,.16) : vec3(.015,.018,.015);
    return a + max(rd.y,0.0)*lift;
}

vec3 shadeSurface(vec3 ro, vec3 rd, vec2 hit, bool secondary) {
    if (hit.y < 0.0) return skyColor(rd);
    vec3 p = ro + rd * hit.x;
    vec3 n = normalAt(p);
    vec3 base = palette(hit.y, p);
    vec3 lp = lightPosition();
    vec3 l = normalize(lp - p);
    float lightDist = length(lp - p);
    float diff = max(dot(n,l),0.0);
    float shadow = secondary ? .72 : softShadow(p+n*.012,l,lightDist);
    float ao = ambientOcclusion(p,n);
    float attenuation = 1.0 / (1.0 + .018 * lightDist * lightDist);
    vec3 warm = (uZone==3||uZone==7) ? vec3(.55,.82,1.0) :
                (uZone==9) ? vec3(1.0,.82,.55) : vec3(1.0,.72,.43);
    vec3 col = base * (.13 + .10 * max(n.y,0.0) + diff * shadow * attenuation * 4.2 * warm) * ao;
    float emissive = step(20.5, hit.y) * step(hit.y, 23.5)
                   + step(23.5, hit.y) * step(hit.y, 25.5) * 1.8
                   + step(25.5, hit.y) * step(hit.y, 26.5) * uImpactLife * 2.5
                   + step(26.5, hit.y) * step(hit.y, 27.5) * 1.4
                   + step(27.5, hit.y) * step(hit.y, 29.5) * (.55 + .35 * sin(uTime * 3.0));
    col += base * emissive * (hit.y < 21.5 ? 2.4 : (hit.y < 23.5 ? 1.25 : 1.6));
    float spec = pow(max(dot(reflect(-l,n),-rd),0.0),34.0);
    col += spec * shadow * .35;
    return col;
}

float hash21(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }

bool insidePortal(vec3 p, vec3 center, vec3 n) {
    vec3 d = p - center;
    if (abs(dot(d, n)) > .12) return false;
    vec3 r = portalBasisRight(n);
    vec3 u = cross(n, r);
    float x = dot(d, r) / .58;
    float y = dot(d, u) / .95;
    return x * x + y * y <= 1.0;
}

void portalFrame(vec3 n, out vec3 r, out vec3 u) {
    r = portalBasisRight(n);
    u = cross(n, r);
}

void transferPortal(vec3 pos, vec3 rd, vec3 fromPos, vec3 fromN, vec3 toPos, vec3 toN,
                    out vec3 outPos, out vec3 outRd) {
    fromN = normalize(fromN); toN = normalize(toN);
    vec3 fr, fu, tr, tu;
    portalFrame(fromN, fr, fu);
    portalFrame(toN, tr, tu);
    tr = -tr;
    vec3 d = pos - fromPos;
    float x = dot(d, fr), y = dot(d, fu), z = dot(d, fromN);
    outPos = toPos + tr * x + tu * y - toN * z + toN * .06;
    outRd = normalize(tr * dot(rd, fr) + tu * dot(rd, fu) - toN * dot(rd, fromN));
}

float viewmodelGun(vec3 p) {
    float swayX = sin(uTime * 1.7) * .012 * uMoveSway;
    float swayY = cos(uTime * 2.1) * .008 * uMoveSway;
    p.x += swayX;
    p.y += swayY + uRecoil * .14;
    p.z += uRecoil * .06;
    float body = sdRoundBox(p - vec3(.30, -.22, .55), vec3(.05, .055, .22), .02);
    float barrel = sdCylinder((p - vec3(.30, -.18, .80)).xzy, .016, .18);
    float grip = sdRoundBox(p - vec3(.30, -.33, .48), vec3(.032, .085, .05), .012);
    float d = min(min(body, barrel), grip);
    if (uWeapon == 0) {
        // Aperture-like portal gun: dish + twin prongs + glowing core
        float dish = sdCylinder((p - vec3(.38, -.15, .66)).xzy, .07, .025);
        float core = sdSphere(p - vec3(.38, -.15, .66), .035);
        float prongL = sdBox(p - vec3(.38, -.05, .62), vec3(.01, .07, .01));
        float prongR = sdBox(p - vec3(.38, -.05, .70), vec3(.01, .07, .01));
        float tank = sdRoundBox(p - vec3(.22, -.18, .42), vec3(.04, .05, .08), .015);
        d = min(d, min(min(dish, core), min(min(prongL, prongR), tank)));
    } else if (uWeapon == 1) {
        float slide = sdRoundBox(p - vec3(.30, -.16, .62), vec3(.035, .025, .12), .008);
        float mag = sdRoundBox(p - vec3(.30, -.38, .50), vec3(.018, .07, .035), .008);
        d = min(d, min(slide, mag));
    } else {
        float stock = sdRoundBox(p - vec3(.30, -.20, .32), vec3(.04, .045, .11), .015);
        float mag = sdRoundBox(p - vec3(.30, -.40, .52), vec3(.02, .09, .04), .01);
        float handguard = sdRoundBox(p - vec3(.30, -.17, .72), vec3(.04, .03, .1), .01);
        d = min(d, min(stock, min(mag, handguard)));
    }
    return d;
}

vec3 shadeViewmodel(vec2 uv) {
    vec3 rd = normalize(vec3(uv.x, uv.y, 1.35));
    float t = 0.0;
    bool hit = false;
    for (int i = 0; i < 40; ++i) {
        vec3 p = rd * t;
        float d = viewmodelGun(p);
        if (d < .002) { hit = true; break; }
        t += d;
        if (t > 1.5) break;
    }
    if (!hit) return vec3(-1.0);
    vec3 p = rd * t;
    vec2 e = vec2(.002, 0.0);
    vec3 n = normalize(vec3(
        viewmodelGun(p + e.xyy) - viewmodelGun(p - e.xyy),
        viewmodelGun(p + e.yxy) - viewmodelGun(p - e.yxy),
        viewmodelGun(p + e.yyx) - viewmodelGun(p - e.yyx)));
    vec3 base = uWeapon == 0 ? vec3(.70, .78, .84) :
                uWeapon == 1 ? vec3(.16, .16, .15) : vec3(.11, .13, .10);
    float lite = .22 + .78 * max(dot(n, normalize(vec3(-.25, .65, -.45))), 0.0);
    vec3 col = base * lite;
    if (uWeapon == 0) {
        float glow = exp(-length(p - vec3(.38, -.15, .66)) * 22.0);
        col += vec3(.25, .55, 1.0) * glow * (.4 + .6 * abs(sin(uTime * 4.0)));
    }
    if (uMuzzle > .02 && uWeapon != 0) {
        float flash = exp(-length(p - vec3(.30, -.16, .98)) * 26.0) * uMuzzle;
        col += vec3(1.0, .7, .25) * flash * 2.4;
    }
    return col;
}

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - uResolution.xy) / uResolution.y;
    float cy = cos(uYawPitch.x), sy = sin(uYawPitch.x);
    float cp = cos(uYawPitch.y), sp = sin(uYawPitch.y);
    vec3 forward = normalize(vec3(sy*cp, sp, -cy*cp));
    vec3 right = normalize(vec3(cy, 0.0, sy));
    vec3 up = normalize(cross(right, forward));
    vec3 rd = normalize(forward * 1.25 + right * uv.x + up * uv.y);
    vec3 ro = uCamera;

    vec2 hit = trace(ro,rd,MAX_DIST);
    vec3 color = shadeSurface(ro,rd,hit,false);

    // One portal recursion: looking into a linked pair reveals the exit view.
    if (hit.y >= 23.5 && hit.y < 25.5 && uPortalBlueOn != 0 && uPortalOrangeOn != 0) {
        vec3 p = ro + rd * hit.x;
        bool blueHit = hit.y < 24.5;
        vec3 fromPos = blueHit ? uPortalBluePos : uPortalOrangePos;
        vec3 fromN = blueHit ? uPortalBlueN : uPortalOrangeN;
        vec3 toPos = blueHit ? uPortalOrangePos : uPortalBluePos;
        vec3 toN = blueHit ? uPortalOrangeN : uPortalBlueN;
        if (insidePortal(p, fromPos, normalize(fromN))) {
            vec3 pro, prd;
            transferPortal(p, rd, fromPos, fromN, toPos, toN, pro, prd);
            vec2 ph = trace(pro, prd, 36.0);
            color = mix(color, shadeSurface(pro, prd, ph, true), .92);
        }
    } else if (hit.y >= 0.0) {
        vec3 p = ro + rd * hit.x;
        vec3 n = normalAt(p);
        float refl = reflectivity(hit.y);
        bool waterReflection = hit.y > 17.5 && hit.y < 18.5;
        if (refl > .08 && (uQuality > 0 || waterReflection)) {
            vec3 rr = reflect(rd,n);
            vec2 rh = trace(p+n*.012,rr,28.0);
            color = mix(color,shadeSurface(p+n*.012,rr,rh,true),refl);
        }
        float fog = 1.0-exp(-hit.x*hit.x*.0008);
        color = mix(color,skyColor(rd),fog);
    }

    // Regional atmosphere: rain, snow, or desert dust.
    if (uZone == 2 || uZone == 8) {
        vec2 ruv = gl_FragCoord.xy / 3.0;
        float cell = hash21(vec2(floor(ruv.x), floor((ruv.y+uTime*130.0)/22.0)));
        float streak = step(.982,cell) * smoothstep(.8,0.0,fract((ruv.y+uTime*130.0)/22.0));
        color += vec3(.13,.25,.27)*streak*.55;
    } else if (uZone == 7) {
        vec2 suv = gl_FragCoord.xy / 12.0;
        float snow = step(.985,hash21(floor(suv)+floor(uTime*2.0)));
        color += vec3(.65,.78,.82)*snow;
    } else if (uZone == 9) {
        vec2 duv = gl_FragCoord.xy / 9.0;
        float dust = step(.978,hash21(floor(duv)+floor(uTime*3.5+duv.x*.2)));
        color += vec3(.55,.42,.22)*dust*.35;
    }

    // First-person weapon viewmodel (camera-local SDF).
    if (uShowHud != 0) {
        vec3 gun = shadeViewmodel(uv);
        if (gun.x >= 0.0) color = gun;
    }

    color = color / (color + vec3(1.0));
    color = pow(color,vec3(.82));
    float vignette = 1.0 - .30*dot(uv*.52,uv*.52);
    color *= vignette;
    color *= .985 + .015*sin(gl_FragCoord.y*1.7);

    if (uShowHud != 0) {
        // Dynamic crosshair: expands with spread / recoil.
        vec2 px = abs(gl_FragCoord.xy-uResolution*.5);
        float gap = 3.0 + uSpread * 420.0 + uRecoil * 6.0;
        float arm = 7.0 + uRecoil * 3.0;
        float crosshair = max(step(px.x, arm)*step(px.y, 0.7)*step(gap, px.x),
                              step(px.y, arm)*step(px.x, 0.7)*step(gap, px.y));
        vec3 crossCol = uWeapon == 0 ? vec3(.45,.75,1.0) : vec3(.92,.88,.74);
        color = mix(color,crossCol,crosshair*.8);
        if (uHitMarker > .01) {
            float hm = max(step(abs(px.x-px.y), 1.1)*step(px.x, 10.0)*step(4.0, px.x),
                           step(abs(px.x+px.y), 1.1)*step(px.x, 10.0)*step(4.0, px.x));
            color = mix(color, vec3(1.0, .92, .55), hm * uHitMarker);
        }
        vec2 top = vec2(gl_FragCoord.x, uResolution.y-gl_FragCoord.y);
        for (int i=0;i<9;++i) {
            vec2 center=vec2(uResolution.x-34.0-float(8-i)*16.0,34.0);
            float pip=1.0-smoothstep(3.0,4.0,length(top-center));
            int bit=1<<i;
            vec3 pc=((uCollectedMask&bit)!=0)?vec3(.82,.12,.07):vec3(.22);
            color=mix(color,pc,pip);
        }
        float zoneBar = step(24.0,gl_FragCoord.x)*step(gl_FragCoord.x,28.0)*
                        step(uResolution.y-92.0,gl_FragCoord.y)*step(gl_FragCoord.y,uResolution.y-24.0);
        color=mix(color,vec3(.72,.12,.07),zoneBar);
    }
    vec2 uiUv=vec2(gl_FragCoord.x/uResolution.x,1.0-gl_FragCoord.y/uResolution.y);
    vec4 overlay=texture(uUiTexture,uiUv);
    color=overlay.rgb+color*(1.0-overlay.a); // Cairo supplies premultiplied ARGB
    fragColor=vec4(color,1.0);
}
