// EngineHost.h — Objective-C interface bridging Swift ↔ C++ engine.

#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

/// Wraps the C++ engine (GameSim + MetalRenderer) for use from Swift.
@interface EngineHost : NSObject

/// Attach to the CAMetalLayer of an MTKView. Call once before the view appears.
/// Swift: host.setup(layer:width:height:)
- (BOOL)setupWithLayer:(CAMetalLayer*)layer
                 width:(NSUInteger)w
                height:(NSUInteger)h
    NS_SWIFT_NAME(setup(layer:width:height:));

/// Called each display-link frame with the elapsed real time (seconds).
- (void)renderFrame:(double)dt;

/// Notify the engine when the drawable size changes.
- (void)resizeWidth:(NSUInteger)w height:(NSUInteger)h
    NS_SWIFT_NAME(resize(width:height:));

/// Notify of a display scale change (Retina / ProMotion).
- (void)setDisplayScale:(double)scale;

/// Mouse input — normalised [0,1] coordinates in the viewport.
- (void)mouseDownX:(double)x y:(double)y button:(int)btn
    NS_SWIFT_NAME(mouseDown(x:y:button:));
- (void)mouseUpX:(double)x y:(double)y button:(int)btn
    NS_SWIFT_NAME(mouseUp(x:y:button:));
- (void)mouseMovedX:(double)x y:(double)y deltaX:(double)dx deltaY:(double)dy
    NS_SWIFT_NAME(mouseMoved(x:y:deltaX:deltaY:));
- (void)scrollDelta:(double)delta;

/// Keyboard input — passes raw macOS/UIKit key codes.
- (void)keyDown:(int)keyCode;
- (void)keyUp:(int)keyCode;

/// Debug info readable by Swift debug overlay.
@property (readonly) NSInteger fps;
@property (readonly) double    frameTimeMs;
@property (readonly) NSInteger drawCalls;
@property (readonly) NSInteger visibleEntities;
@property (readonly) NSInteger projectileCount;
@property (readonly) double    gpuTimeMs;

@end

NS_ASSUME_NONNULL_END
