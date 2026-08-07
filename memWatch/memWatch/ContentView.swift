//
//  ContentView.swift
//  memWatch
//
//  The main window: a sortable, refreshable table of every visible
//  process on the system, with a kill menu in the toolbar.
//

import SwiftUI

struct ContentView: View {
    @EnvironmentObject var store: ProcessStore
    @State private var selection: Set<ProcessInfo.ID> = []

    var body: some View {
        VStack(spacing: 0) {
            memoryDashboard
            Divider()
            table
            Divider()
            footer
        }
        .toolbar { toolbarContent }
        .navigationTitle("memWatch")
    }

    // MARK: - Dashboard

    private var memoryDashboard: some View {
        MemoryDashboardView()
    }

    // MARK: - Table

    private var table: some View {
        Table(store.sortedProcesses, selection: $selection) {
            TableColumn("Name") { row in
                HStack(spacing: 8) {
                    Image(systemName: "app.fill")
                        .foregroundStyle(.tertiary)
                    Text(row.name)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
            }
            .width(min: 160, ideal: 220)

            TableColumn("Memory") { row in
                Text(formattedBytes(row.memoryBytes))
                    .monospacedDigit()
            }
            .width(min: 90, ideal: 110)

            TableColumn("Memory %") { row in
                let totalMemory = getTotalPhysicalMemory()
                if totalMemory > 0 {
                    let percent = Double(row.memoryBytes) / Double(totalMemory) * 100
                    Text(String(format: "%.1f%%", percent))
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                } else {
                    Text("—")
                        .foregroundStyle(.tertiary)
                }
            }
            .width(min: 80, ideal: 90)

            TableColumn("CPU") { row in
                if let cpu = row.cpuUsage {
                    Text(formattedPercent(cpu))
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                } else {
                    Text("—")
                        .foregroundStyle(.tertiary)
                }
            }
            .width(min: 60, ideal: 80)

            TableColumn("PID") { row in
                Text("\(row.pid)")
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            .width(min: 60, ideal: 80)

            TableColumn("Port") { row in
                if row.ports.isEmpty {
                    Text("—")
                        .foregroundStyle(.tertiary)
                } else {
                    Text(row.ports.map(String.init).joined(separator: ", "))
                        .monospacedDigit()
                        .lineLimit(1)
                        .truncationMode(.tail)
                        .help(row.ports.map(String.init).joined(separator: ", "))
                }
            }
            .width(min: 80, ideal: 160)
        }
    }

    // MARK: - Toolbar

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItemGroup(placement: .primaryAction) {
            Picker("Sort by", selection: $store.sortKey) {
                ForEach(ProcessStore.SortKey.allCases) { key in
                    Text(key.label).tag(key)
                }
            }
            .pickerStyle(.menu)

            Button {
                store.sortAscending.toggle()
            } label: {
                Image(systemName: store.sortAscending
                      ? "arrow.up.circle.fill"
                      : "arrow.down.circle.fill")
            }
            .help(store.sortAscending ? "Sort ascending" : "Sort descending")

            Button {
                store.refresh()
            } label: {
                Image(systemName: "arrow.clockwise")
            }
            .keyboardShortcut("r", modifiers: [.command])
            .help("Refresh now (⌘R)")

            Menu {
                Button("Terminate (SIGTERM)") { killSelected(force: false) }
                    .disabled(selection.isEmpty)
                Button("Force Kill (SIGKILL)", role: .destructive) { killSelected(force: true) }
                    .disabled(selection.isEmpty)
                Divider()
                Button("Terminate & Force if Needed") {
                    killSelectedWithEscalation()
                }
                .disabled(selection.isEmpty)
            } label: {
                Label("Kill", systemImage: "xmark.octagon")
            }
            .menuStyle(.borderlessButton)
            .disabled(selection.isEmpty)
            .help("Kill the selected process")
        }
    }

    // MARK: - Footer

    private var footer: some View {
        HStack(spacing: 12) {
            if store.isRefreshing {
                ProgressView()
                    .controlSize(.small)
                Text("Refreshing…")
                    .foregroundStyle(.secondary)
            } else {
                Image(systemName: "checkmark.circle")
                    .foregroundStyle(.tertiary)
                Text("Updated \(store.lastRefresh, style: .relative) ago")
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Text("\(store.processes.count) processes")
                .foregroundStyle(.secondary)
                .monospacedDigit()
        }
        .font(.callout)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(.bar)
    }

    // MARK: - Kill actions

    private func killSelected(force: Bool) {
        // Snapshot the selected ids — the array can change underneath us
        // when refresh() runs after the first kill.
        let pids = selectedPids()
        for pid in pids {
            if force {
                store.forceKill(pid: pid)
            } else {
                store.terminate(pid: pid)
            }
        }
        // Trigger an immediate refresh so the dead processes disappear
        // instead of waiting for the next auto-refresh tick.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            store.refresh()
        }
    }

    private func killSelectedWithEscalation() {
        let pids = selectedPids()
        var remaining = pids.count
        guard remaining > 0 else { return }
        for pid in pids {
            store.terminateWithEscalation(pid: pid) { _ in
                remaining -= 1
                if remaining == 0 {
                    store.refresh()
                }
            }
        }
    }

    private func selectedPids() -> [Int32] {
        selection.compactMap { id in
            store.processes.first(where: { $0.id == id })?.pid
        }
    }

    // MARK: - Formatting

    /// Compact, human-readable byte count: 1.4 GB / 312 MB / 8.7 KB.
    /// Used in the table and in the detail pane.
    private func formattedBytes(_ bytes: Int64) -> String {
        let formatter = ByteCountFormatter()
        formatter.allowedUnits = [.useKB, .useMB, .useGB]
        formatter.countStyle = .binary
        return formatter.string(fromByteCount: bytes)
    }

    /// Compact CPU percentage, e.g. "12.3%". Treats a missing value as 0
    /// for display purposes (caller is expected to render "—" instead
    /// when the underlying reading is nil).
    private func formattedPercent(_ fraction: Double) -> String {
        let pct = fraction * 100
        return String(format: "%.1f%%", pct)
    }

    /// Returns total physical memory size in bytes using sysctlbyname("hw.memsize").
    private func getTotalPhysicalMemory() -> UInt64 {
        var memSize: UInt64 = 0
        return "hw.memsize".withCString { cname in
            var len = size_t(MemoryLayout<UInt64>.size)
            if sysctlbyname(cname, &memSize, &len, nil, 0) == 0 {
                return memSize
            }
            return 0
        }
    }
}
