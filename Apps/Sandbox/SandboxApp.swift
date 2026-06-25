// SandboxApp.swift — Ironclad engine sandbox: blank Metal window for engine verification.

import SwiftUI

@main
struct IroncladSandboxApp: App {
    var body: some Scene {
        WindowGroup("Ironclad Sandbox") {
            ContentView()
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1280, height: 800)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}