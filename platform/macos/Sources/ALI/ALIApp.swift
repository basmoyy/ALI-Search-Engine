// ALIApp.swift — Application entry point.

import SwiftUI

@main
struct ALIApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified(showsTitle: false))
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("New Tab") {
                    // Handled inside ContentView via keyboard shortcut
                }
                .keyboardShortcut("t", modifiers: .command)
            }
        }
    }
}
