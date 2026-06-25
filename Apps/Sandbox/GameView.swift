// GameView.swift — MTKView-backed SwiftUI view that drives the C++ engine.

import SwiftUI
import MetalKit

// MARK: - Metal View (NSView)

final class GameMTKView: MTKView {

    var engineHost: EngineHost?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let w = window else { return }
        w.makeFirstResponder(self)
        w.acceptsMouseMovedEvents = true
    }

    override var acceptsFirstResponder: Bool { true }

    // ─── Keyboard
    override func keyDown(with event: NSEvent) {
        engineHost?.keyDown(Int32(event.keyCode))
    }
    override func keyUp(with event: NSEvent) {
        engineHost?.keyUp(Int32(event.keyCode))
    }

    // ─── Mouse
    override func mouseDown(with event: NSEvent) {
        let p = normalisedPoint(event)
        engineHost?.mouseDown(x: p.x, y: p.y, button: 0)
    }
    override func mouseUp(with event: NSEvent) {
        let p = normalisedPoint(event)
        engineHost?.mouseUp(x: p.x, y: p.y, button: 0)
    }
    override func rightMouseDown(with event: NSEvent) {
        let p = normalisedPoint(event)
        engineHost?.mouseDown(x: p.x, y: p.y, button: 1)
    }
    override func rightMouseUp(with event: NSEvent) {
        let p = normalisedPoint(event)
        engineHost?.mouseUp(x: p.x, y: p.y, button: 1)
    }
    override func mouseMoved(with event: NSEvent) {
        let p = normalisedPoint(event)
        engineHost?.mouseMoved(x: p.x, y: p.y, deltaX: event.deltaX, deltaY: event.deltaY)
    }
    override func mouseDragged(with event: NSEvent) { mouseMoved(with: event) }
    override func rightMouseDragged(with event: NSEvent) { mouseMoved(with: event) }
    override func scrollWheel(with event: NSEvent) {
        engineHost?.scrollDelta(event.deltaY)
    }

    private func normalisedPoint(_ e: NSEvent) -> (x: Double, y: Double) {
        let pt = convert(e.locationInWindow, from: nil)
        return (x: Double(pt.x / bounds.width), y: Double(1.0 - pt.y / bounds.height))
    }
}

// MARK: - Coordinator (MTKViewDelegate)

final class GameCoordinator: NSObject, MTKViewDelegate {

    // Optional so SwiftUI can create it before the host exists.
    // Set in makeNSView via context.coordinator — this instance is
    // the one SwiftUI retains for the view's lifetime.
    var host: EngineHost?
    var lastTime: CFAbsoluteTime = CFAbsoluteTimeGetCurrent()

    override init() { super.init() }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard let host else { return }
        let scale = view.window?.backingScaleFactor ?? 1.0
        host.setDisplayScale(scale)
        host.resize(width: UInt(size.width), height: UInt(size.height))
    }

    func draw(in view: MTKView) {
        guard let host else { return }
        let now = CFAbsoluteTimeGetCurrent()
        let dt  = min(now - lastTime, 0.05)
        lastTime = now
        host.renderFrame(dt)
    }
}

// MARK: - NSViewRepresentable wrapper

struct MetalGameView: NSViewRepresentable {
    @ObservedObject var stats: EngineStats

    func makeCoordinator() -> GameCoordinator {
        GameCoordinator()
    }

    func makeNSView(context: Context) -> GameMTKView {
        let view = GameMTKView()
        view.colorPixelFormat         = .bgra8Unorm
        view.depthStencilPixelFormat  = .depth32Float
        view.clearColor               = MTLClearColorMake(0.08, 0.09, 0.11, 1)
        view.preferredFramesPerSecond = 60
        view.enableSetNeedsDisplay    = false
        view.isPaused                 = false

        let host  = EngineHost()
        let layer = view.layer as! CAMetalLayer

        let w = max(UInt(view.drawableSize.width),  1)
        let h = max(UInt(view.drawableSize.height), 1)

        guard host.setup(layer: layer, width: w, height: h) else {
            print("[PostFall] EngineHost setup failed")
            return view
        }

        let scale = NSScreen.main?.backingScaleFactor ?? 1.0
        host.setDisplayScale(scale)

        // Wire the real host into the coordinator SwiftUI already retains.
        // Never create a second coordinator — MTKView.delegate is weak and
        // a locally-created coordinator would be deallocated immediately.
        context.coordinator.host = host
        view.engineHost           = host
        view.delegate             = context.coordinator

        return view
    }

    func updateNSView(_ nsView: GameMTKView, context: Context) {}
}

// MARK: - Stats observable (drives debug overlay)

final class EngineStats: ObservableObject {
    @Published var fps: Int = 0
    @Published var frameTimeMs: Double = 0
    @Published var drawCalls: Int = 0
    @Published var visibleEntities: Int = 0
    @Published var projectileCount: Int = 0
    @Published var gpuTimeMs: Double = 0
}
