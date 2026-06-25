// GameView.swift — Ironclad AE: MTKView host + editor state sync.

import SwiftUI
import MetalKit

final class GameMTKView: MTKView {
    var engineHost: EngineHost?
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let w = window else { return }
        w.makeFirstResponder(self)
        w.acceptsMouseMovedEvents = true
    }
    override var acceptsFirstResponder: Bool { true }
    override func keyDown(with event: NSEvent) { engineHost?.keyDown(Int32(event.keyCode)) }
    override func keyUp(with event: NSEvent)   { engineHost?.keyUp(Int32(event.keyCode)) }

    // Camera: left-drag orbits, right-drag pans, scroll / pinch zoom.
    override func mouseDragged(with event: NSEvent) {
        engineHost?.orbit(deltaX: Double(event.deltaX), deltaY: Double(event.deltaY))
    }
    override func rightMouseDragged(with event: NSEvent) {
        engineHost?.pan(deltaX: Double(event.deltaX), deltaY: Double(event.deltaY))
    }
    override func scrollWheel(with event: NSEvent) {
        engineHost?.zoomDelta(Double(event.deltaY) * 0.03)
    }
    override func magnify(with event: NSEvent) {
        engineHost?.zoomDelta(Double(event.magnification))
    }
}

final class GameCoordinator: NSObject, MTKViewDelegate {
    var host: EngineHost?
    weak var stats: EngineStats?
    var lastTime: CFAbsoluteTime = CFAbsoluteTimeGetCurrent()
    private var lastPreset = -1

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard let host else { return }
        host.setDisplayScale(Double(view.window?.backingScaleFactor ?? 1.0))
        host.resize(width: UInt(size.width), height: UInt(size.height))
    }

    func draw(in view: MTKView) {
        guard let host else { return }
        let now = CFAbsoluteTimeGetCurrent()
        let dt  = min(now - lastTime, 0.05)
        lastTime = now
        host.renderFrame(dt)
        syncStats(host)
    }

    private func syncStats(_ host: EngineHost) {
        guard let s = stats else { return }
        s.fps = Int(host.fps); s.frameTimeMs = host.frameTimeMs
        s.drawCalls = Int(host.drawCalls); s.gpuTimeMs = host.gpuTimeMs

        // One-shot UI requests.
        if s.requestCycle { host.cyclePreset(); s.requestCycle = false }
        if s.requestSave  { host.saveAnim();    s.requestSave  = false }

        // Refresh the editable arrays when the preset changes (or on first run).
        let preset = Int(host.animPreset)
        if preset != lastPreset || s.fieldLabels.isEmpty {
            lastPreset = preset
            let n = Int(host.animFieldCount)
            s.presetName  = host.presetName()
            s.fieldLabels = (0..<n).map { host.animFieldLabel($0) }
            s.values      = (0..<n).map { host.animValue($0) }
            s.valuesText  = s.values.map { String(format: "%g", $0) }
            s.phases      = (0..<n).map { host.animPhase($0) }
            s.phasesText  = s.phases.map { String(format: "%g", $0) }
        }

        // Push committed edits (host ignores unchanged values → rebuild only on change).
        for i in 0..<s.values.count { host.setAnimValue(i, value: s.values[i]) }
        for i in 0..<s.phases.count { host.setAnimPhase(i, value: s.phases[i]) }
    }
}

struct MetalGameView: NSViewRepresentable {
    @ObservedObject var stats: EngineStats

    func makeCoordinator() -> GameCoordinator { GameCoordinator() }

    func makeNSView(context: Context) -> GameMTKView {
        let view = GameMTKView()
        view.colorPixelFormat        = .bgra8Unorm
        view.depthStencilPixelFormat = .depth32Float
        view.clearColor              = MTLClearColorMake(0.10, 0.11, 0.13, 1)
        view.preferredFramesPerSecond = 60
        view.enableSetNeedsDisplay   = false
        view.isPaused                = false

        let host  = EngineHost()
        let layer = view.layer as! CAMetalLayer
        let w = max(UInt(view.drawableSize.width),  1)
        let h = max(UInt(view.drawableSize.height), 1)
        guard host.setup(layer: layer, width: w, height: h) else {
            print("[IroncladAE] EngineHost setup failed"); return view
        }
        host.setDisplayScale(Double(NSScreen.main?.backingScaleFactor ?? 1.0))

        context.coordinator.host  = host
        context.coordinator.stats = stats
        view.engineHost           = host
        view.delegate             = context.coordinator
        return view
    }

    func updateNSView(_ nsView: GameMTKView, context: Context) {}
}

final class EngineStats: ObservableObject {
    @Published var fps: Int = 0
    @Published var frameTimeMs: Double = 0
    @Published var drawCalls: Int = 0
    @Published var gpuTimeMs: Double = 0

    @Published var presetName: String = "idle"
    @Published var fieldLabels: [String] = []
    @Published var values: [Float] = []
    @Published var valuesText: [String] = []
    @Published var phases: [Float] = []
    @Published var phasesText: [String] = []
    @Published var requestCycle: Bool = false
    @Published var requestSave: Bool = false
}
