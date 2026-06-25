// AnimEditorApp.swift — Ironclad AE: standalone procedural-animation editor.

import SwiftUI

@main
struct IroncladAEApp: App {
    var body: some Scene {
        WindowGroup("Ironclad AE") {
            ContentView()
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1280, height: 800)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
