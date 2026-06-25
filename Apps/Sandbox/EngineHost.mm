// EngineHost.mm — Minimal engine host: renderer + input only.
// Add game simulation by layering on top in a game-specific sim class.

#import "EngineHost.h"
#include "../../Engine/Renderer/Metal/MetalRenderer.hpp"
#include "../../Engine/Input/InputSystem.hpp"
#include "../../Engine/Core/Log.hpp"
#include <memory>
#include <chrono>

struct EngineState {
    std::unique_ptr<MetalRenderer> renderer;
    std::unique_ptr<InputSystem>   input;
    u32  viewWidth   { 0 };
    u32  viewHeight  { 0 };
    f32  frameTimeMs { 0 };
    i32  fps         { 0 };
    i32  fpsCounter  { 0 };
    f64  fpsTimer    { 0 };
};

@implementation EngineHost {
    std::unique_ptr<EngineState> _state;
    NSInteger _fps;
    double    _frameTimeMs;
    NSInteger _drawCalls;
    NSInteger _visibleEntities;
    NSInteger _projectileCount;
    double    _gpuTimeMs;
}

@synthesize fps             = _fps;
@synthesize frameTimeMs     = _frameTimeMs;
@synthesize drawCalls       = _drawCalls;
@synthesize visibleEntities = _visibleEntities;
@synthesize projectileCount = _projectileCount;
@synthesize gpuTimeMs       = _gpuTimeMs;

- (BOOL)setupWithLayer:(CAMetalLayer*)layer width:(NSUInteger)w height:(NSUInteger)h {
    _state = std::make_unique<EngineState>();
    auto& s = *_state;

    s.renderer = std::make_unique<MetalRenderer>();
    s.input    = std::make_unique<InputSystem>();
    s.viewWidth  = static_cast<u32>(w);
    s.viewHeight = static_cast<u32>(h);

    if (!s.renderer->Init((__bridge void*)layer, s.viewWidth, s.viewHeight)) {
        LOG_ERR("Sandbox", "Renderer init failed");
        return NO;
    }

    LOG_INF("Sandbox", "Ironclad sandbox initialised (%lux%lu)", (unsigned long)w, (unsigned long)h);
    return YES;
}

- (void)renderFrame:(double)dt {
    auto& s = *_state;
    if (!s.renderer) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    s.fpsTimer   += dt;
    s.fpsCounter += 1;
    if (s.fpsTimer >= 1.0) {
        s.fps        = s.fpsCounter;
        s.fpsCounter = 0;
        s.fpsTimer   = 0.0;
    }

    // Empty scene — game logic layers on top via game-specific sim class.
    RenderScene scene;
    scene.cameraPos    = Vec3Make(0, 18, 12);
    scene.cameraTarget = Vec3Make(0, 0, 0);
    scene.debug.fps         = static_cast<u32>(s.fps);
    scene.debug.frameTimeMs = s.frameTimeMs;
    scene.debug.drawCalls   = static_cast<u32>(s.renderer->DrawCallCount());
    scene.debug.gpuTimeMs   = s.renderer->LastGPUTimeMs();

    s.renderer->BeginFrame(static_cast<f32>(dt));
    s.renderer->RenderScene(scene);
    s.renderer->EndFrame();

    auto t1 = std::chrono::high_resolution_clock::now();
    s.frameTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    _fps             = s.fps;
    _frameTimeMs     = s.frameTimeMs;
    _drawCalls       = s.renderer->DrawCallCount();
    _visibleEntities = 0;
    _projectileCount = 0;
    _gpuTimeMs       = s.renderer->LastGPUTimeMs();

    s.input->NextFrame();
}

- (void)resizeWidth:(NSUInteger)w height:(NSUInteger)h {
    if (!_state) return;
    _state->viewWidth  = static_cast<u32>(w);
    _state->viewHeight = static_cast<u32>(h);
    _state->renderer->Resize(static_cast<u32>(w), static_cast<u32>(h));
}

- (void)setDisplayScale:(double)scale {
    if (_state) _state->renderer->SetDisplayScale(static_cast<f32>(scale));
}

- (void)mouseDownX:(double)x y:(double)y button:(int)btn {
    if (!_state) return;
    auto& s = *_state;
    Vec2 np = Vec2Make((f32)(x / s.viewWidth), (f32)(y / s.viewHeight));
    s.input->OnMouseDown(btn == 0 ? MouseButton::Left : MouseButton::Right);
    s.input->OnMouseMove(np, Vec2Make(0, 0));
}

- (void)mouseUpX:(double)x y:(double)y button:(int)btn {
    if (!_state) return;
    auto& s = *_state;
    Vec2 np = Vec2Make((f32)(x / s.viewWidth), (f32)(y / s.viewHeight));
    s.input->OnMouseUp(btn == 0 ? MouseButton::Left : MouseButton::Right);
    s.input->OnMouseMove(np, Vec2Make(0, 0));
}

- (void)mouseMovedX:(double)x y:(double)y deltaX:(double)dx deltaY:(double)dy {
    if (!_state) return;
    auto& s = *_state;
    Vec2 np    = Vec2Make((f32)(x / s.viewWidth), (f32)(y / s.viewHeight));
    Vec2 delta = Vec2Make((f32)dx, (f32)dy);
    s.input->OnMouseMove(np, delta);
}

- (void)scrollDelta:(double)delta {
    if (_state) _state->input->OnScroll((f32)delta);
}

- (void)keyDown:(int)keyCode {
    if (!_state) return;
    Key k = Key::Unknown;
    switch (keyCode) {
        case 49: k = Key::Space; break;
        case  0: k = Key::A;    break;
        case  1: k = Key::S;    break;
        case  2: k = Key::D;    break;
        case 13: k = Key::W;    break;
        default: break;
    }
    if (k != Key::Unknown) _state->input->OnKeyDown(k);
}

- (void)keyUp:(int)keyCode {
    if (!_state) return;
    Key k = Key::Unknown;
    switch (keyCode) {
        case 49: k = Key::Space; break;
        case  0: k = Key::A;    break;
        case  1: k = Key::S;    break;
        case  2: k = Key::D;    break;
        case 13: k = Key::W;    break;
        default: break;
    }
    if (k != Key::Unknown) _state->input->OnKeyUp(k);
}

@end