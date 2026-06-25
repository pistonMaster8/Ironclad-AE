// EngineHost.h — Ironclad AE: animation-editor host (renderer + one animated soldier).

#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@interface EngineHost : NSObject

- (BOOL)setupWithLayer:(CAMetalLayer*)layer
                 width:(NSUInteger)w
                height:(NSUInteger)h
    NS_SWIFT_NAME(setup(layer:width:height:));

- (void)renderFrame:(double)dt;

- (void)resizeWidth:(NSUInteger)w height:(NSUInteger)h
    NS_SWIFT_NAME(resize(width:height:));
- (void)setDisplayScale:(double)scale;
- (void)keyDown:(int)keyCode;
- (void)keyUp:(int)keyCode;

// ─── Camera ──────────────────────────────────────────────────────────────────
- (void)orbitDeltaX:(double)dx deltaY:(double)dy NS_SWIFT_NAME(orbit(deltaX:deltaY:));
- (void)panDeltaX:(double)dx deltaY:(double)dy   NS_SWIFT_NAME(pan(deltaX:deltaY:));
- (void)zoomDelta:(double)delta;

// ─── Animation editor bridge ─────────────────────────────────────────────────
/// Currently previewed/edited preset: 0=idle, 1=walk, 2=jog, 3=run.
@property (readonly) NSInteger animPreset;
@property (readonly) NSInteger animFieldCount;          // 12
/// Advance the preset idle → walk → jog → run → idle.
- (void)cyclePreset;
- (NSString*)presetName;                                // "idle" / "walk" / "jog" / "run"
- (NSString*)animFieldLabel:(NSInteger)field NS_SWIFT_NAME(animFieldLabel(_:));
- (float)animValue:(NSInteger)field NS_SWIFT_NAME(animValue(_:));
- (void)setAnimValue:(NSInteger)field value:(float)v NS_SWIFT_NAME(setAnimValue(_:value:));
- (float)animPhase:(NSInteger)field NS_SWIFT_NAME(animPhase(_:));
- (void)setAnimPhase:(NSInteger)field value:(float)v NS_SWIFT_NAME(setAnimPhase(_:value:));
/// Persist all four presets (values + phases) to user defaults.
- (void)saveAnim;

// ─── Debug overlay ───────────────────────────────────────────────────────────
@property (readonly) NSInteger fps;
@property (readonly) double    frameTimeMs;
@property (readonly) NSInteger drawCalls;
@property (readonly) double    gpuTimeMs;

@end

NS_ASSUME_NONNULL_END
