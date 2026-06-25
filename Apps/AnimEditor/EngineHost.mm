// EngineHost.mm — Ironclad AE: drives one animated soldier and the gait editor.

#import "EngineHost.h"
#include "../../Engine/Renderer/Metal/MetalRenderer.hpp"
#include "../../Engine/Animation/Animation.hpp"
#include "../../Engine/Simulation/Components.hpp"
#include "../../Engine/Core/Log.hpp"
#include <memory>
#include <chrono>
#import  <simd/simd.h>

// Rotation quaternion from a model matrix (scale removed) — the retarget input.
static Quat QuatFromModelMat(const Mat4& m) {
    simd_float3 c0 = simd_normalize(m.columns[0].xyz);
    simd_float3 c1 = simd_normalize(m.columns[1].xyz);
    simd_float3 c2 = simd_normalize(m.columns[2].xyz);
    return simd_quaternion(simd_matrix(c0, c1, c2));
}

struct EngineState {
    std::unique_ptr<MetalRenderer> renderer;
    u32  viewWidth   { 0 };
    u32  viewHeight  { 0 };
    f32  frameTimeMs { 0 };
    i32  fps         { 0 };
    i32  fpsCounter  { 0 };
    f64  fpsTimer    { 0 };

    Skeleton           skel;
    AnimationLibrary   lib;
    AnimationController ctrl;
    int  preset      { 0 };       // 0=idle 1=walk 2=jog 3=run
    bool animDirty   { false };
    Quat animBoneRot[kHumanoidBoneSlots];

    // Orbit camera (spherical around camTarget)
    f32  camYaw    { 0.35f };
    f32  camPitch  { 0.20f };
    f32  camDist   { 3.2f };
    Vec3 camTarget { Vec3Make(0.0f, 0.9f, 0.0f) };
};

@implementation EngineHost {
    std::unique_ptr<EngineState> _state;
    NSInteger _fps;
    double    _frameTimeMs;
    NSInteger _drawCalls;
    double    _gpuTimeMs;
}

@synthesize fps         = _fps;
@synthesize frameTimeMs = _frameTimeMs;
@synthesize drawCalls   = _drawCalls;
@synthesize gpuTimeMs   = _gpuTimeMs;

- (BOOL)setupWithLayer:(CAMetalLayer*)layer width:(NSUInteger)w height:(NSUInteger)h {
    _state = std::make_unique<EngineState>();
    auto& s = *_state;

    s.renderer = std::make_unique<MetalRenderer>();
    s.viewWidth  = static_cast<u32>(w);
    s.viewHeight = static_cast<u32>(h);
    if (!s.renderer->Init((__bridge void*)layer, s.viewWidth, s.viewHeight)) {
        LOG_ERR("AE", "Renderer init failed");
        return NO;
    }

    [self loadAnim];
    s.skel = Skeleton::CreateHumanoid();
    s.lib  = AnimationLibrary::BuildSoldierLibrary(s.skel);
    s.ctrl.Init(&s.skel, &s.lib);
    s.ctrl.overrideClip = s.lib.Find(GaitClipName(s.preset));

    LOG_INF("AE", "Ironclad AE initialised (%lux%lu)", (unsigned long)w, (unsigned long)h);
    return YES;
}

- (void)renderFrame:(double)dt {
    auto& s = *_state;
    if (!s.renderer) return;
    auto t0 = std::chrono::high_resolution_clock::now();

    s.fpsTimer += dt; s.fpsCounter += 1;
    if (s.fpsTimer >= 1.0) { s.fps = s.fpsCounter; s.fpsCounter = 0; s.fpsTimer = 0.0; }

    // Rebuild the clip library when a parameter was edited (preserve clip phase).
    if (s.animDirty) {
        f32 clipTime = s.ctrl.sampler.GetCurrentTime();
        s.lib = AnimationLibrary::BuildSoldierLibrary(s.skel);
        s.ctrl.library = &s.lib;
        s.ctrl.overrideClip = s.lib.Find(GaitClipName(s.preset));
        if (s.ctrl.overrideClip) {
            s.ctrl.sampler.SetClip(s.ctrl.overrideClip, 0.f);
            s.ctrl.sampler.layers[0].time = clipTime;
        }
        s.animDirty = false;
    }

    RenderScene scene;
    // Plain editor backdrop — no sky, ground/dirt, or grass.
    scene.drawEnvironment   = false;
    scene.shellGrassVisible = false;
    scene.longGrassVisible  = false;

    // Orbit camera from spherical coordinates around the target.
    scene.cameraTarget = s.camTarget;
    scene.cameraPos    = Vec3Make(
        s.camTarget.x + s.camDist * cosf(s.camPitch) * sinf(s.camYaw),
        s.camTarget.y + s.camDist * sinf(s.camPitch),
        s.camTarget.z + s.camDist * cosf(s.camPitch) * cosf(s.camYaw));

    // One soldier at the origin, facing the camera.
    scene.shadowDiscCount = 1;
    auto& disc = scene.shadowDiscs[0];
    disc.position   = Vec3Make(0, 0, 0);
    disc.radius     = 0.5f;
    disc.playerSlot = 1;          // ally → blue tint
    disc.hasFacing  = true;
    disc.facingYaw  = 0.0f;

    // Drive the controller with the selected preset clip; feed the retarget.
    s.ctrl.overrideClip = s.lib.Find(GaitClipName(s.preset));
    TransformComponent xf {};
    xf.position = disc.position;
    xf.rotation = QuatIdentity();
    xf.scale    = 1.f;
    s.ctrl.Update((f32)dt, xf);

    const ModelPose& mp = s.ctrl.modelPose;
    u16 bc = s.ctrl.GetBoneCount();
    if (bc > kHumanoidBoneSlots) bc = (u16)kHumanoidBoneSlots;
    for (u16 hb = 0; hb < bc; ++hb)
        s.animBoneRot[hb] = QuatFromModelMat(mp.modelMats[hb]);

    scene.skinnedUnitCount      = 1;
    scene.skinnedUnits[0].boneOffset = 0;
    scene.skinnedUnits[0].boneCount  = bc;
    scene.skinnedBoneRot        = s.animBoneRot;

    scene.debug.fps         = static_cast<u32>(s.fps);
    scene.debug.frameTimeMs = s.frameTimeMs;
    scene.debug.drawCalls   = static_cast<u32>(s.renderer->DrawCallCount());
    scene.debug.gpuTimeMs   = s.renderer->LastGPUTimeMs();

    s.renderer->BeginFrame(static_cast<f32>(dt));
    s.renderer->RenderScene(scene);
    s.renderer->EndFrame();

    auto t1 = std::chrono::high_resolution_clock::now();
    s.frameTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    _fps = s.fps; _frameTimeMs = s.frameTimeMs;
    _drawCalls = s.renderer->DrawCallCount(); _gpuTimeMs = s.renderer->LastGPUTimeMs();
}

- (void)resizeWidth:(NSUInteger)w height:(NSUInteger)h {
    if (!_state) return;
    _state->viewWidth = static_cast<u32>(w); _state->viewHeight = static_cast<u32>(h);
    _state->renderer->Resize(static_cast<u32>(w), static_cast<u32>(h));
}
- (void)setDisplayScale:(double)scale {
    if (_state) _state->renderer->SetDisplayScale(static_cast<f32>(scale));
}

- (void)keyDown:(int)keyCode {
    if (!_state) return;
    if (keyCode == 49) [self cyclePreset];   // Space cycles the preset
    if (keyCode == 1)  [self saveAnim];      // S saves
}
- (void)keyUp:(int)keyCode { (void)keyCode; }

// ─── Camera ──────────────────────────────────────────────────────────────────
- (void)orbitDeltaX:(double)dx deltaY:(double)dy {
    if (!_state) return;
    auto& s = *_state;
    const f32 kSens = 0.008f;
    s.camYaw   += (f32)dx * kSens;
    s.camPitch += (f32)dy * kSens;
    s.camPitch  = fmaxf(-0.20f, fminf(1.45f, s.camPitch));
}
- (void)panDeltaX:(double)dx deltaY:(double)dy {
    if (!_state) return;
    auto& s = *_state;
    f32 rightX =  cosf(s.camYaw), rightZ = -sinf(s.camYaw);
    f32 upScale = s.camDist * 0.0015f;
    s.camTarget.x -= rightX * (f32)dx * upScale;
    s.camTarget.z -= rightZ * (f32)dx * upScale;
    s.camTarget.y += (f32)dy * upScale;        // vertical pan
}
- (void)zoomDelta:(double)delta {
    if (!_state) return;
    auto& s = *_state;
    s.camDist *= (1.0f - (f32)delta);
    s.camDist  = fmaxf(0.8f, fminf(20.0f, s.camDist));
}

// ─── Animation editor bridge ─────────────────────────────────────────────────
- (NSInteger)animPreset     { return _state ? _state->preset : 0; }
- (NSInteger)animFieldCount { return kGaitFieldCount; }

- (void)cyclePreset {
    if (!_state) return;
    _state->preset = (_state->preset + 1) % kGaitPresetCount;
    _state->ctrl.overrideClip = _state->lib.Find(GaitClipName(_state->preset));
}
- (NSString*)presetName {
    return [NSString stringWithUTF8String:GaitPresetName(_state ? _state->preset : 0)];
}
- (NSString*)animFieldLabel:(NSInteger)field {
    return [NSString stringWithUTF8String:GaitFieldLabel((int)field)];
}
- (float)animValue:(NSInteger)field {
    return _state ? GaitParamGet(_state->preset, (int)field) : 0.f;
}
- (void)setAnimValue:(NSInteger)field value:(float)v {
    if (!_state) return;
    if (GaitParamGet(_state->preset, (int)field) == v) return;
    GaitParamSet(_state->preset, (int)field, v);
    _state->animDirty = true;
}
- (float)animPhase:(NSInteger)field {
    return _state ? GaitPhaseGet(_state->preset, (int)field) : 0.f;
}
- (void)setAnimPhase:(NSInteger)field value:(float)v {
    if (!_state) return;
    if (GaitPhaseGet(_state->preset, (int)field) == v) return;
    GaitPhaseSet(_state->preset, (int)field, v);
    _state->animDirty = true;
}

- (void)saveAnim {
    NSUserDefaults* ud = [NSUserDefaults standardUserDefaults];
    for (int p = 0; p < kGaitPresetCount; ++p)
        for (int f = 0; f < kGaitFieldCount; ++f) {
            [ud setFloat:GaitParamGet(p,f) forKey:[NSString stringWithFormat:@"ae_p%d_v%d", p, f]];
            [ud setFloat:GaitPhaseGet(p,f) forKey:[NSString stringWithFormat:@"ae_p%d_ph%d", p, f]];
        }
    [ud synchronize];
}
- (void)loadAnim {
    NSUserDefaults* ud = [NSUserDefaults standardUserDefaults];
    if (![ud objectForKey:@"ae_p0_v0"]) return;   // nothing saved yet
    for (int p = 0; p < kGaitPresetCount; ++p)
        for (int f = 0; f < kGaitFieldCount; ++f) {
            GaitParamSet(p, f, [ud floatForKey:[NSString stringWithFormat:@"ae_p%d_v%d", p, f]]);
            GaitPhaseSet(p, f, [ud floatForKey:[NSString stringWithFormat:@"ae_p%d_ph%d", p, f]]);
        }
}

@end
