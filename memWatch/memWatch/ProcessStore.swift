//
//  ProcessStore.swift
//  memoryMonitor
//
//  ObservableObject that owns the process list and refresh cadence.
//  Views read from it; the only thing that writes to it is the
//  internal Timer (when auto-refresh is on) or the manual refresh
//  method.
//

import Combine
import Foundation
import SwiftUI

@MainActor
final class ProcessStore: ObservableObject {
    // MARK: - Published state
    @Published var processes: [ProcessInfo] = []
    @Published var sortKey: SortKey = .memory
    @Published var sortAscending: Bool = false  // biggest memory first by default
    @Published var autoRefresh: Bool = true
    @Published var refreshInterval: TimeInterval = 2.0
    @Published var lastRefresh: Date = .distantPast
    @Published var isRefreshing: Bool = false

    enum SortKey: String, CaseIterable, Identifiable {
        case name, pid, memory, cpu, ports
        var id: String { rawValue }
        var label: String {
            switch self {
            case .name:   return "Name"
            case .pid:    return "PID"
            case .memory: return "Memory"
            case .cpu:    return "CPU"
            case .ports:  return "Port"
            }
        }
    }

    // MARK: - Private state
    private var timer: Timer?
    private var snapshotInFlight = false  // coalesce overlapping refreshes

    init() {
        // Kick off an initial refresh right away so the UI isn't empty.
        refresh()
        startTimer()
    }

    deinit {
        timer?.invalidate()
    }

    // MARK: - Sorting

    /// View-facing list, already sorted per the current `sortKey`/`sortAscending`.
    var sortedProcesses: [ProcessInfo] {
        let key = sortKey
        let asc = sortAscending
        return processes.sorted { a, b in
            let result: Bool
            switch key {
            case .name:
                result = a.name.localizedCaseInsensitiveCompare(b.name) == .orderedAscending
            case .pid:
                result = a.pid < b.pid
            case .memory:
                result = a.memoryBytes < b.memoryBytes
            case .cpu:
                // Missing readings sort "low" so the first-tick rows
                // don't pop to the top when sorting descending.
                let av = a.cpuUsage ?? -1
                let bv = b.cpuUsage ?? -1
                result = av < bv
            case .ports:
                // Processes with no ports sort "low" by default — feels more useful
                // than having them clustered at the top of every sort.
                result = (a.ports.max() ?? -1) < (b.ports.max() ?? -1)
            }
            return asc ? result : !result
        }
    }

    // MARK: - Refresh

    func refresh() {
        // Avoid running two snapshots in parallel if a tick fires while a
        // slow `lsof` is still running.
        guard !snapshotInFlight else { return }
        snapshotInFlight = true
        isRefreshing = true

        // Hop off the main actor to do the syscalls; they can be slow
        // when there are thousands of PIDs.
        Task.detached(priority: .userInitiated) { [weak self] in
            let snapshot = ProcessService.snapshotProcesses()
            // Bind a strong reference once so the MainActor.closure
            // doesn't reach back through a mutable `self`.
            guard let store = self else { return }
            await MainActor.run {
                store.processes = snapshot
                store.lastRefresh = Date()
                store.isRefreshing = false
                store.snapshotInFlight = false
            }
        }
    }

    // MARK: - Auto-refresh

    /// (Re)start the timer. Called whenever the user changes the interval
    /// or toggles auto-refresh on.
    func startTimer() {
        timer?.invalidate()
        guard autoRefresh else { return }
        timer = Timer.scheduledTimer(withTimeInterval: refreshInterval, repeats: true) { [weak self] _ in
            // Timers fire on the main run loop; calling into the actor
            // is the same cost as a normal method call.
            Task { @MainActor in
                self?.refresh()
            }
        }
    }

    func setAutoRefresh(_ enabled: Bool) {
        autoRefresh = enabled
        if enabled {
            startTimer()
        } else {
            timer?.invalidate()
            timer = nil
        }
    }

    func setRefreshInterval(_ seconds: TimeInterval) {
        refreshInterval = max(0.5, seconds)
        if autoRefresh { startTimer() }
    }

    // MARK: - Killing

    /// Send SIGTERM (graceful). Returns true if the signal was posted.
    /// The process may not exit immediately — the UI just removes it
    /// on the next refresh once it's actually gone.
    @discardableResult
    func terminate(pid: Int32) -> Bool {
        return ProcessService.sendSignal(pid: pid, signal: SIGTERM)
    }

    /// Send SIGKILL (force). Use this for processes that ignore SIGTERM.
    @discardableResult
    func forceKill(pid: Int32) -> Bool {
        return ProcessService.sendSignal(pid: pid, signal: SIGKILL)
    }

    /// Try graceful first; if the process is still around after a short
    /// wait, escalate to SIGKILL. Returns whether the process is gone.
    func terminateWithEscalation(pid: Int32,
                                 gracePeriod: TimeInterval = 1.5,
                                 completion: @escaping (Bool) -> Void) {
        guard ProcessService.sendSignal(pid: pid, signal: SIGTERM) else {
            completion(false)
            return
        }
        // Poll for the process to disappear. We can't reliably observe
        // its death from user space, so a short retry loop is the
        // simplest correct approach.
        Task.detached(priority: .userInitiated) { [weak self] in
            guard let store = self else { return }
            let deadline = Date().addingTimeInterval(gracePeriod)
            while Date() < deadline {
                if !ProcessService.processExists(pid: pid) {
                    await MainActor.run {
                        store.refresh()
                        completion(true)
                    }
                    return
                }
                try? await Task.sleep(nanoseconds: 150_000_000)
            }
            // Still alive — escalate.
            _ = ProcessService.sendSignal(pid: pid, signal: SIGKILL)
            // Give the kernel a moment to actually reap it, then refresh.
            try? await Task.sleep(nanoseconds: 300_000_000)
            await MainActor.run {
                store.refresh()
                completion(ProcessService.processExists(pid: pid) == false)
            }
        }
    }
}
