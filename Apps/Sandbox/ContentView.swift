// ContentView.swift — Root SwiftUI layout with game view + debug overlay.

import SwiftUI

struct ContentView: View {

    @StateObject private var stats = EngineStats()

    var body: some View {
        ZStack(alignment: .topLeading) {
            MetalGameView(stats: stats)
                .ignoresSafeArea()

            DebugOverlayView(stats: stats)
                .padding(10)
        }
        .frame(minWidth: 800, minHeight: 600)
    }
}

// MARK: - Debug Overlay

struct DebugOverlayView: View {
    @ObservedObject var stats: EngineStats

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Group {
                statRow("FPS",            "\(stats.fps)")
                statRow("Frame",          String(format: "%.2f ms", stats.frameTimeMs))
                statRow("GPU",            String(format: "%.2f ms", stats.gpuTimeMs))
                statRow("Draw calls",     "\(stats.drawCalls)")
                statRow("Entities",       "\(stats.visibleEntities)")
                statRow("Projectiles",    "\(stats.projectileCount)")
            }
        }
        .font(.system(size: 11, design: .monospaced))
        .padding(6)
        .background(.black.opacity(0.55))
        .foregroundStyle(.green)
        .cornerRadius(5)
        .allowsHitTesting(false)
    }

    private func statRow(_ label: String, _ value: String) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .frame(width: 80, alignment: .leading)
                .foregroundStyle(.green.opacity(0.7))
            Text(value)
                .frame(width: 60, alignment: .trailing)
        }
    }
}

#Preview {
    ContentView()
}
