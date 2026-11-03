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
                Button("New Tab") { }         // handled by NSEvent monitor in ContentView
                    .keyboardShortcut("t", modifiers: .command)
                Button("Close Tab") { }
                    .keyboardShortcut("w", modifiers: .command)
            }
            CommandGroup(after: .toolbar) {
                Button("Focus Address Bar") { }
                    .keyboardShortcut("l", modifiers: .command)
                Divider()
                Button("Back") { }
                    .keyboardShortcut("[", modifiers: .command)
                Button("Forward") { }
                    .keyboardShortcut("]", modifiers: .command)
                Button("Reload") { }
                    .keyboardShortcut("r", modifiers: .command)
            }
        }
    }
}
