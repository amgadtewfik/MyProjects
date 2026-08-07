//
//  ProcessInfoModel.swift
//  memoryMonitor
//
//  Plain data model for a single process row. We keep the model dumb
//  (no business logic) so the UI layer can drive it and the data
//  layer can rebuild it freely on every refresh.
//

import Foundation

struct ProcessInfo: Identifiable, Hashable {
    /// Stable id across refreshes so SwiftUI can animate row changes
    /// instead of recreating every row each tick.
    let id: Int32  // pid

    /// Human-readable process name, e.g. "Safari" or "python3".
    let name: String

    /// Process identifier, duplicated in `id` but kept here so it
    /// shows up in the row and the detail pane uniformly.
    let pid: Int32

    /// Resident memory in bytes (RSS). Kept as `Int64` to fit 32-bit
    /// processes on Linux-style platforms; macOS only needs Int32 but
    /// the cost of using Int64 is zero.
    let memoryBytes: Int64

    /// Open listening/in-use ports, deduplicated. May be empty for
    /// processes that simply don't bind any port.
    let ports: [Int]

    /// CPU usage as a fraction in `0.0...1.0+` of one logical core.
    /// `nil` on the first snapshot (no prior reading to diff against)
    /// or when the process is brand-new / vanished between snapshots.
    let cpuUsage: Double?

    /// Best-effort bundle identifier (e.g. "com.apple.Safari"). Useful
    /// for displaying a real app name and icon. Optional because
    /// CLI tools and daemons usually don't have one.
    let bundleIdentifier: String?
}
