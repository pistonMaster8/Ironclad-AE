// ContentView.swift — Ironclad AE: soldier viewport + always-open animation editor.

import SwiftUI

struct ContentView: View {
    @StateObject private var stats = EngineStats()

    var body: some View {
        ZStack(alignment: .topLeading) {
            MetalGameView(stats: stats)
                .ignoresSafeArea()
            HStack(alignment: .top, spacing: 8) {
                statsPanel
                aPanel
            }
            .padding(10)
        }
        .frame(minWidth: 900, minHeight: 680)
    }

    // ── Stats (green) ─────────────────────────────────────────────────────────
    private var statsPanel: some View {
        VStack(alignment: .leading, spacing: 2) {
            statRow("FPS",   "\(stats.fps)")
            statRow("Frame", String(format: "%.2f ms", stats.frameTimeMs))
            statRow("GPU",   String(format: "%.2f ms", stats.gpuTimeMs))
            statRow("Draws", "\(stats.drawCalls)")
        }
        .font(.system(size: 11, design: .monospaced))
        .padding(6)
        .background(.black.opacity(0.55))
        .foregroundStyle(.green)
        .cornerRadius(5)
        .allowsHitTesting(false)
    }

    // ── A panel (orange, always open) ─────────────────────────────────────────
    private var aPanel: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text("Animation Editor")
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundStyle(.orange)
                .allowsHitTesting(false)

            // Cycle button: idle → walk → jog → run
            Button(action: { stats.requestCycle = true }) {
                Text("◀ \(stats.presetName.uppercased()) ▶")
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 5)
                    .background(Color.orange.opacity(0.9))
                    .foregroundStyle(.black)
                    .cornerRadius(4)
            }
            .buttonStyle(.plain)
            .font(.system(size: 12, weight: .bold, design: .monospaced))

            HStack(spacing: 6) {
                Text("").frame(width: 86, alignment: .leading)
                Text("value").frame(width: 56, alignment: .leading)
                Text("φ shift°").frame(width: 48, alignment: .leading)
            }
            .font(.system(size: 8, design: .monospaced))
            .foregroundStyle(.orange.opacity(0.6))
            .allowsHitTesting(false)
            Divider().background(.orange.opacity(0.3))

            ForEach(stats.values.indices, id: \.self) { i in animRow(i) }

            Divider().background(.orange.opacity(0.3))
            Button(action: { stats.requestSave = true }) {
                Text("SAVE")
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 3)
                    .background(Color.orange.opacity(0.7))
                    .foregroundStyle(.black)
                    .cornerRadius(4)
            }
            .buttonStyle(.plain)
            .font(.system(size: 10, weight: .bold, design: .monospaced))
        }
        .frame(width: 210)
        .padding(6)
        .background(.black.opacity(0.6))
        .foregroundStyle(.orange)
        .cornerRadius(5)
    }

    private func animRow(_ i: Int) -> some View {
        let label = i < stats.fieldLabels.count ? stats.fieldLabels[i] : "\(i)"
        let valText = Binding<String>(
            get: { i < stats.valuesText.count ? stats.valuesText[i] : "" },
            set: { if i < stats.valuesText.count { stats.valuesText[i] = $0 } })
        let phText = Binding<String>(
            get: { i < stats.phasesText.count ? stats.phasesText[i] : "" },
            set: { if i < stats.phasesText.count { stats.phasesText[i] = $0 } })
        return HStack(spacing: 6) {
            Text(label)
                .frame(width: 86, alignment: .leading)
                .foregroundStyle(.orange.opacity(0.75))
            TextField("", text: valText)
                .textFieldStyle(.roundedBorder).frame(width: 56)
                .colorScheme(.dark).foregroundStyle(.orange)
                .onSubmit { commitValue(i) }
            TextField("φ°", text: phText)
                .textFieldStyle(.roundedBorder).frame(width: 48)
                .colorScheme(.dark).foregroundStyle(.orange.opacity(0.85))
                .onSubmit { commitPhase(i) }
        }
    }

    private func commitValue(_ i: Int) {
        guard i < stats.valuesText.count, i < stats.values.count else { return }
        if let v = Float(stats.valuesText[i]) { stats.values[i] = v }
        else { stats.valuesText[i] = String(format: "%g", stats.values[i]) }
    }
    private func commitPhase(_ i: Int) {
        guard i < stats.phasesText.count, i < stats.phases.count else { return }
        if let v = Float(stats.phasesText[i]) { stats.phases[i] = v }
        else { stats.phasesText[i] = String(format: "%g", stats.phases[i]) }
    }

    private func statRow(_ label: String, _ value: String) -> some View {
        HStack(spacing: 6) {
            Text(label).frame(width: 44, alignment: .leading).foregroundStyle(.green.opacity(0.7))
            Text(value).frame(width: 64, alignment: .trailing)
        }
    }
}
