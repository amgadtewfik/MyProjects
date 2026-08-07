//
//  memoryMonitorApp.swift
//  memWatch
//
//  Top-level app entry point. Wires up the ProcessStore as a single
//  source of truth for the running process list and the UI.
//

import SwiftUI

@main
struct memWatchApp: App {
    // Single shared instance — the same store backs every view in the app.
    @StateObject private var store = ProcessStore()
    @StateObject private var memoryStatsStore = MemoryStatsStore()

    var body: some Scene {
        // Main window. ⌘, is bound automatically to the Settings scene below.
        WindowGroup {
            ContentView()
                .environmentObject(store)
                .environmentObject(memoryStatsStore)
                .frame(minWidth: 720, minHeight: 420)
        }

        // Settings window — opens with ⌘, .
        Settings {
            SettingsView()
                .environmentObject(store)
        }
    }
}