#include "AnimationClip.hpp"
#include "GaitConfig.hpp"
#include <algorithm>
#include <cmath>

// ─── AnimationTrack ───────────────────────────────────────────────────────────

f32 AnimationTrack::Sample(f32 t) const {
    if (keys.empty()) return 0.f;
    if (keys.size() == 1) return keys[0].value;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time)  return keys.back().value;

    // Binary search for the enclosing bracket.
    int lo = 0, hi = (int)keys.size() - 1;
    while (lo + 1 < hi) {
        int mid = (lo + hi) >> 1;
        if (keys[mid].time <= t) lo = mid; else hi = mid;
    }
    f32 dt = keys[hi].time - keys[lo].time;
    if (dt < 1e-7f) return keys[lo].value;
    f32 alpha = (t - keys[lo].time) / dt;
    return Lerp(keys[lo].value, keys[hi].value, alpha);
}

// ─── AnimationClip ────────────────────────────────────────────────────────────

f32 AnimationClip::WrapTime(f32 t) const {
    if (duration < 1e-6f) return 0.f;
    if (loop) {
        t = fmodf(t, duration);
        if (t < 0.f) t += duration;
    } else {
        t = Clamp(t, 0.f, duration);
    }
    return t;
}

void AnimationClip::SamplePose(f32 time, LocalPose& outPose) const {
    if (tracks.empty()) return;

    // Temporary per-bone channel accumulators.
    struct Channels {
        f32 tx, ty, tz;
        f32 rx, ry, rz, rw;
        f32 sx, sy, sz;
        bool touched;
    };
    Channels ch[kMaxBonesPerSkeleton];

    // Seed from the current outPose so untouched channels stay at their existing values.
    for (u16 i = 0; i < outPose.boneCount; ++i) {
        auto& b = outPose.bones[i];
        auto& c = ch[i];
        c.tx = b.translation.x; c.ty = b.translation.y; c.tz = b.translation.z;
#if PFGE_USE_SIMD
        c.rx = b.rotation.vector.x; c.ry = b.rotation.vector.y;
        c.rz = b.rotation.vector.z; c.rw = b.rotation.vector.w;
#else
        c.rx = b.rotation.x; c.ry = b.rotation.y;
        c.rz = b.rotation.z; c.rw = b.rotation.w;
#endif
        c.sx = b.scale.x; c.sy = b.scale.y; c.sz = b.scale.z;
        c.touched = false;
    }
    for (u16 i = outPose.boneCount; i < kMaxBonesPerSkeleton; ++i)
        ch[i] = Channels{};

    using Ch = AnimationTrack::Channel;
    for (const auto& track : tracks) {
        if (track.bone >= outPose.boneCount) continue;
        f32 v = track.Sample(time);
        auto& c = ch[track.bone];
        c.touched = true;
        switch (track.channel) {
            case Ch::TX: c.tx = v; break;  case Ch::TY: c.ty = v; break;  case Ch::TZ: c.tz = v; break;
            case Ch::RX: c.rx = v; break;  case Ch::RY: c.ry = v; break;
            case Ch::RZ: c.rz = v; break;  case Ch::RW: c.rw = v; break;
            case Ch::SX: c.sx = v; break;  case Ch::SY: c.sy = v; break;  case Ch::SZ: c.sz = v; break;
        }
    }

    for (u16 i = 0; i < outPose.boneCount; ++i) {
        if (!ch[i].touched) continue;
        auto& c = ch[i];
        outPose.bones[i].translation = Vec3Make(c.tx, c.ty, c.tz);
#if PFGE_USE_SIMD
        outPose.bones[i].rotation = simd_normalize(simd_quaternion(c.rx, c.ry, c.rz, c.rw));
#else
        outPose.bones[i].rotation = QuatNorm({c.rx, c.ry, c.rz, c.rw});
#endif
        outPose.bones[i].scale = Vec3Make(c.sx, c.sy, c.sz);
    }
}

// ─── Procedural clip generation ───────────────────────────────────────────────

static void AddSineRotTrack(AnimationClip& clip, BoneIndex bone,
                             Vec3 axis, f32 amplitudeDeg, f32 periodSec,
                             f32 phaseRad, f32 duration, f32 fps) {
    if (bone == kInvalidBone) return;
    using Ch = AnimationTrack::Channel;

    AnimationTrack tx, ty, tz, tw;
    tx.bone = ty.bone = tz.bone = tw.bone = bone;
    tx.channel = Ch::RX; ty.channel = Ch::RY; tz.channel = Ch::RZ; tw.channel = Ch::RW;

    f32 amp = Deg2Rad(amplitudeDeg);
    int nFrames = (int)(duration * fps) + 1;
    for (int f = 0; f < nFrames; ++f) {
        f32 t     = (f32)f / fps;
        f32 angle = sinf(kTwoPi * t / periodSec + phaseRad) * amp;
        Quat q    = QuatAxisAngle(Vec3Norm(axis), angle);
#if PFGE_USE_SIMD
        tx.AddKey(t, q.vector.x);
        ty.AddKey(t, q.vector.y);
        tz.AddKey(t, q.vector.z);
        tw.AddKey(t, q.vector.w);
#else
        tx.AddKey(t, q.x);
        ty.AddKey(t, q.y);
        tz.AddKey(t, q.z);
        tw.AddKey(t, q.w);
#endif
    }
    clip.tracks.push_back(std::move(tx));
    clip.tracks.push_back(std::move(ty));
    clip.tracks.push_back(std::move(tz));
    clip.tracks.push_back(std::move(tw));
}

static void AddSineTransTrack(AnimationClip& clip, BoneIndex bone,
                               int component, // 0=X, 1=Y, 2=Z
                               f32 amplitude, f32 periodSec,
                               f32 phaseRad,  f32 duration, f32 fps) {
    if (bone == kInvalidBone) return;
    using Ch = AnimationTrack::Channel;
    AnimationTrack t;
    t.bone = bone;
    t.channel = (component == 0) ? Ch::TX : (component == 1) ? Ch::TY : Ch::TZ;
    int nFrames = (int)(duration * fps) + 1;
    for (int f = 0; f < nFrames; ++f) {
        f32 time = (f32)f / fps;
        t.AddKey(time, sinf(kTwoPi * time / periodSec + phaseRad) * amplitude);
    }
    clip.tracks.push_back(std::move(t));
}

// ─── Gait authoring ──────────────────────────────────────────────────────────
// Authored in the humanoid's own frame: +Z forward, +Y up, +X right, -X left.
// Limbs hang along -Y (after the controller's base stance). The renderer's
// retarget reflects this onto the Mixamo mesh, so we never reason about it here.

static inline Quat QRX(f32 deg) { return QuatAxisAngle(Vec3Make(1,0,0), Deg2Rad(deg)); }
static inline Quat QRY(f32 deg) { return QuatAxisAngle(Vec3Make(0,1,0), Deg2Rad(deg)); }
static inline Quat QRZ(f32 deg) { return QuatAxisAngle(Vec3Make(0,0,1), Deg2Rad(deg)); }

// Emit a full RX/RY/RZ/RW rotation track from a per-phase quaternion function.
// phase runs 0 → 2π across the clip duration (one full stride).
template <typename Fn>
static void AddPoseTrack(AnimationClip& clip, BoneIndex bone, Fn fn, f32 dur, f32 fps) {
    if (bone == kInvalidBone) return;
    using Ch = AnimationTrack::Channel;
    AnimationTrack tx, ty, tz, tw;
    tx.bone = ty.bone = tz.bone = tw.bone = bone;
    tx.channel = Ch::RX; ty.channel = Ch::RY; tz.channel = Ch::RZ; tw.channel = Ch::RW;
    int nF = (int)(dur * fps) + 1;
    for (int f = 0; f < nF; ++f) {
        f32 t  = (f32)f / fps;
        f32 ph = (dur > 1e-6f) ? (t / dur) * kTwoPi : 0.f;
        Quat q = QuatNorm(fn(ph));
#if PFGE_USE_SIMD
        tx.AddKey(t, q.vector.x); ty.AddKey(t, q.vector.y);
        tz.AddKey(t, q.vector.z); tw.AddKey(t, q.vector.w);
#else
        tx.AddKey(t, q.x); ty.AddKey(t, q.y); tz.AddKey(t, q.z); tw.AddKey(t, q.w);
#endif
    }
    clip.tracks.push_back(std::move(tx)); clip.tracks.push_back(std::move(ty));
    clip.tracks.push_back(std::move(tz)); clip.tracks.push_back(std::move(tw));
}

// Four live-tunable presets: [0]=idle, [1]=walk, [2]=jog, [3]=run.
GaitParams gGaitParams[kGaitPresetCount] = {
    // idle — near-static stance (subtle bob/sway only; legs barely move)
    { /*dur*/ 2.0f,  /*thigh*/ 0.f,  /*bias*/ 2.f,  /*kBase*/ 6.f,  /*kSwing*/ 0.f,
      /*arm*/ 0.f,   /*lean*/ 2.f,   /*bob*/ 0.004f,/*sway*/ 0.004f,/*pelvYaw*/ 0.f,
      /*foot*/ 0.f,  /*elbow*/ 0.f },
    // walk
    { 0.48f, 36.f, 6.f, 14.f, 72.f, 34.f, 11.f, 0.026f, 0.012f, 10.f, 13.f, 24.f },
    // jog
    { 0.62f, 30.f, 7.f, 12.f, 58.f, 20.f, 10.f, 0.024f, 0.020f,  8.f, 14.f, 34.f },
    // run — faster sprint: longer reach, deeper fold, hard lean + arm drive
    { 0.50f, 42.f, 10.f, 16.f, 80.f, 45.f, 18.f, 0.030f, 0.018f, 10.f, 18.f, 40.f },
};
GaitParams gGaitPhase[kGaitPresetCount] = {};   // per-field phase offsets (deg), default 0

static const char* kPresetNames[kGaitPresetCount] = { "idle", "walk", "jog", "run" };
static const char* kClipNames[kGaitPresetCount] = {
    "soldier_idle", "soldier_walk", "soldier_jog", "soldier_run" };
static const char* kFieldLabels[kGaitFieldCount] = {
    "duration","thighAmp","thighBias","kneeBase","kneeSwing","armAmp",
    "lean","bob","sway","pelvYaw","foot","elbowSwing" };

const char* GaitPresetName(int p)  { return (p>=0 && p<kGaitPresetCount) ? kPresetNames[p] : ""; }
const char* GaitClipName(int p)    { return (p>=0 && p<kGaitPresetCount) ? kClipNames[p]   : ""; }
const char* GaitFieldLabel(int f)  { return (f>=0 && f<kGaitFieldCount)  ? kFieldLabels[f] : ""; }

static bool GaitIdxOK(int p, int f) {
    return p >= 0 && p < kGaitPresetCount && f >= 0 && f < kGaitFieldCount;
}
f32  GaitParamGet(int p, int f) { return GaitIdxOK(p,f) ? ((const f32*)&gGaitParams[p])[f] : 0.f; }
void GaitParamSet(int p, int f, f32 v) { if (GaitIdxOK(p,f)) ((f32*)&gGaitParams[p])[f] = v; }
f32  GaitPhaseGet(int p, int f) { return GaitIdxOK(p,f) ? ((const f32*)&gGaitPhase[p])[f] : 0.f; }
void GaitPhaseSet(int p, int f, f32 v) { if (GaitIdxOK(p,f)) ((f32*)&gGaitPhase[p])[f] = v; }

static AnimationClip MakeGaitClip(const Skeleton& skel, const char* name,
                                  const GaitParams& g, const GaitParams& ph) {
    AnimationClip clip;
    clip.name        = name;
    clip.duration    = g.duration;
    clip.loop        = true;
    clip.ticksPerSec = 30.f;
    const f32 d = g.duration, fps = 30.f;

    BoneIndex pelv = skel.FindBone("pelvis");
    BoneIndex sp01 = skel.FindBone("spine_01");
    BoneIndex sp02 = skel.FindBone("spine_02");
    BoneIndex uaL  = skel.FindBone("upper_arm_l"), uaR = skel.FindBone("upper_arm_r");
    BoneIndex laL  = skel.FindBone("lower_arm_l"), laR = skel.FindBone("lower_arm_r");
    BoneIndex ulL  = skel.FindBone("upper_leg_l"), ulR = skel.FindBone("upper_leg_r");
    BoneIndex llL  = skel.FindBone("lower_leg_l"), llR = skel.FindBone("lower_leg_r");
    BoneIndex ftL  = skel.FindBone("foot_l"),      ftR = skel.FindBone("foot_r");

    auto bump = [](f32 x){ return fmaxf(0.f, sinf(x)); };  // single positive lobe / cycle

    // Per-field phase offsets (degrees → radians), added to each motion's stride phase.
    // Non-oscillating fields (duration/bias/base/lean) ignore their phase.
    const f32 pThigh = Deg2Rad(ph.thighAmp),  pKnee = Deg2Rad(ph.kneeSwing);
    const f32 pArm   = Deg2Rad(ph.armAmp),    pFoot = Deg2Rad(ph.foot);
    const f32 pBob   = Deg2Rad(ph.bob),       pSway = Deg2Rad(ph.sway);
    const f32 pPelv  = Deg2Rad(ph.pelvYaw),   pElbow = Deg2Rad(ph.elbowSwing);

    // ── Legs ──────────────────────────────────────────────────────────────────
    // Hip fore/aft: forward (foot → +Z) is a negative X rotation. L/R opposite.
    AddPoseTrack(clip, ulL, [=](f32 p){ return QRX(-(g.thighBias + g.thighAmp*cosf(p + pThigh)));   }, d, fps);
    AddPoseTrack(clip, ulR, [=](f32 p){ return QRX(-(g.thighBias - g.thighAmp*cosf(p + pThigh)));   }, d, fps);
    // Knee flexion: heel-toward-hip is +X; rectified bump over each leg's swing phase.
    AddPoseTrack(clip, llL, [=](f32 p){ return QRX(g.kneeBase + g.kneeSwing*bump(p - kPi + pKnee)); }, d, fps);
    AddPoseTrack(clip, llR, [=](f32 p){ return QRX(g.kneeBase + g.kneeSwing*bump(p + pKnee));       }, d, fps);
    // Ankle: toes up at heel strike (φ=0), toes down at toe-off (φ=π).
    AddPoseTrack(clip, ftL, [=](f32 p){ return QRX(-g.foot*cosf(p + pFoot));                        }, d, fps);
    AddPoseTrack(clip, ftR, [=](f32 p){ return QRX( g.foot*cosf(p + pFoot));                        }, d, fps);

    // ── Arms ──────────────────────────────────────────────────────────────────
    // Lower (≈72° about Z) baked in, then fore/aft swing opposite the same-side leg.
    AddPoseTrack(clip, uaL, [=](f32 p){ return QuatMul(QRX( g.armAmp*cosf(p + pArm)), QRZ( 72.f)); }, d, fps);
    AddPoseTrack(clip, uaR, [=](f32 p){ return QuatMul(QRX(-g.armAmp*cosf(p + pArm)), QRZ(-72.f)); }, d, fps);
    // Elbows: the forearm's local frame is rotated by the upper arm's ~72° lower,
    // so the hinge axis is QRZ(∓72)·X, not plain X (which would only twist it).
    // Negative angle flexes the hand forward; a touch more on the forward swing.
    const Vec3 elbowAxL = Vec3Make(0.309f, -0.951f, 0.f);   // QRZ(-72)·X
    const Vec3 elbowAxR = Vec3Make(0.309f,  0.951f, 0.f);   // QRZ(+72)·X
    AddPoseTrack(clip, laL, [=](f32 p){ return QuatAxisAngle(elbowAxL, Deg2Rad(-(28.f + g.elbowSwing*fmaxf(0.f,-cosf(p + pElbow))))); }, d, fps);
    AddPoseTrack(clip, laR, [=](f32 p){ return QuatAxisAngle(elbowAxR, Deg2Rad(-(28.f + g.elbowSwing*fmaxf(0.f, cosf(p + pElbow))))); }, d, fps);

    // ── Pelvis ──────────────────────────────────────────────────────────────────
    // Vertical bob: 2 dips per stride, lowest at both contacts (−cos 2φ).
    AddSineTransTrack(clip, pelv, 1, g.bob,  d * 0.5f, -kPi*0.5f + pBob, d, fps);
    // Lateral sway toward the stance foot (−sin φ → left during left stance).
    AddSineTransTrack(clip, pelv, 0, g.sway, d,         kPi + pSway,     d, fps);
    // Transverse rotation (swing hip forward, +Y) + slight contralateral list (Z).
    AddPoseTrack(clip, pelv, [=](f32 p){
        return QuatMul(QRY(g.pelvYaw*cosf(p + pPelv)), QRZ(-g.pelvYaw*0.4f*sinf(p + pPelv)));
    }, d, fps);

    // ── Spine ─────────────────────────────────────────────────────────────────
    // Forward lean + counter-rotation opposite the pelvis (conserves momentum).
    AddPoseTrack(clip, sp01, [=](f32 p){ return QuatMul(QRX(g.lean*0.6f), QRY(-g.pelvYaw*0.8f*cosf(p + pPelv))); }, d, fps);
    AddPoseTrack(clip, sp02, [=](f32 p){ return QuatMul(QRX(g.lean*0.4f), QRY(-g.pelvYaw*0.4f*cosf(p + pPelv))); }, d, fps);

    return clip;
}

// All four locomotion clips are gait-driven from their editable preset (idle uses
// near-zero amplitudes → a standing stance with subtle breathing/bob).
AnimationClip MakeSoldierIdleClip(const Skeleton& skel) {
    return MakeGaitClip(skel, GaitClipName(0), gGaitParams[0], gGaitPhase[0]);
}
AnimationClip MakeSoldierWalkClip(const Skeleton& skel) {
    return MakeGaitClip(skel, GaitClipName(1), gGaitParams[1], gGaitPhase[1]);
}
AnimationClip MakeSoldierJogClip(const Skeleton& skel) {
    return MakeGaitClip(skel, GaitClipName(2), gGaitParams[2], gGaitPhase[2]);
}
AnimationClip MakeSoldierRunClip(const Skeleton& skel) {
    return MakeGaitClip(skel, GaitClipName(3), gGaitParams[3], gGaitPhase[3]);
}

AnimationClip MakeSoldierHitReactClip(const Skeleton& skel) {
    AnimationClip clip;
    clip.name        = "soldier_hit_react";
    clip.duration    = 0.35f;
    clip.loop        = false;
    clip.ticksPerSec = 30.f;

    BoneIndex sp01 = skel.FindBone("spine_01");
    BoneIndex sp02 = skel.FindBone("spine_02");
    BoneIndex head = skel.FindBone("head");
    BoneIndex uaL  = skel.FindBone("upper_arm_l");
    BoneIndex uaR  = skel.FindBone("upper_arm_r");

    // Build keyframes manually for a sharp jolt-and-return curve.
    auto addJolt = [&](BoneIndex bone, AnimationTrack::Channel ch, f32 joltVal) {
        if (bone == kInvalidBone) return;
        AnimationTrack t;
        t.bone = bone; t.channel = ch;
        t.AddKey(0.00f, 0.f);
        t.AddKey(0.05f, joltVal);
        t.AddKey(0.35f, 0.f);
        clip.tracks.push_back(std::move(t));
    };

    // Spine jolt backward on X (positive = lean back).
    Quat joltQ = QuatAxisAngle(Vec3Make(1,0,0), Deg2Rad(12.f));
#if PFGE_USE_SIMD
    addJolt(sp01, AnimationTrack::Channel::RX, joltQ.vector.x);
    addJolt(sp01, AnimationTrack::Channel::RY, joltQ.vector.y);
    addJolt(sp01, AnimationTrack::Channel::RZ, joltQ.vector.z);
    addJolt(sp01, AnimationTrack::Channel::RW, joltQ.vector.w);
    Quat joltH = QuatAxisAngle(Vec3Make(1,0,0), Deg2Rad(8.f));
    addJolt(head, AnimationTrack::Channel::RX, joltH.vector.x);
    addJolt(head, AnimationTrack::Channel::RY, joltH.vector.y);
    addJolt(head, AnimationTrack::Channel::RZ, joltH.vector.z);
    addJolt(head, AnimationTrack::Channel::RW, joltH.vector.w);
    Quat joltA = QuatAxisAngle(Vec3Make(0,0,1), Deg2Rad(5.f));
    addJolt(uaL, AnimationTrack::Channel::RX, joltA.vector.x);
    addJolt(uaL, AnimationTrack::Channel::RZ, joltA.vector.z);
    addJolt(uaL, AnimationTrack::Channel::RW, joltA.vector.w);
    Quat joltB = QuatAxisAngle(Vec3Make(0,0,-1), Deg2Rad(5.f));
    addJolt(uaR, AnimationTrack::Channel::RX, joltB.vector.x);
    addJolt(uaR, AnimationTrack::Channel::RZ, joltB.vector.z);
    addJolt(uaR, AnimationTrack::Channel::RW, joltB.vector.w);
    (void)sp02;
#else
    addJolt(sp01, AnimationTrack::Channel::RX, joltQ.x);
    addJolt(sp01, AnimationTrack::Channel::RY, joltQ.y);
    addJolt(sp01, AnimationTrack::Channel::RZ, joltQ.z);
    addJolt(sp01, AnimationTrack::Channel::RW, joltQ.w);
    Quat joltH = QuatAxisAngle(Vec3Make(1,0,0), Deg2Rad(8.f));
    addJolt(head, AnimationTrack::Channel::RX, joltH.x);
    addJolt(head, AnimationTrack::Channel::RY, joltH.y);
    addJolt(head, AnimationTrack::Channel::RZ, joltH.z);
    addJolt(head, AnimationTrack::Channel::RW, joltH.w);
    (void)sp02; (void)uaL; (void)uaR;
#endif

    return clip;
}
