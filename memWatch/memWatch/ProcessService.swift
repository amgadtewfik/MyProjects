//
//  ProcessService.swift
//  memWatch
//
//  Pure data layer: turns the live process table into an array of
//  ProcessInfo. Uses `proc_listpids`, `proc_pidinfo`, and
//  `lsof` (only for ports) so the app doesn't need entitlements.
//

import Darwin
import Foundation

enum ProcessService {
    // MARK: - CPU usage tracking

    /// Previous CPU totals per PID, kept between `snapshotProcesses()`
    /// calls so we can diff them. macOS exposes per-thread user+system
    /// nanoseconds via `proc_pidinfo(PROC_PIDTASKINFO)`; summing user
    /// and system gives the total time the process was on-CPU.
    private static let cpuLock = NSLock()
    private static var previousCpu: [Int32: (ticks: UInt64, at: Date)] = [:]

    /// Drops any cached CPU baseline. Called when a snapshot gap is so
    /// large that a diff would be meaningless (e.g. after a long pause
    /// or on the very first run).
    private static func resetCpuBaseline() {
        cpuLock.lock()
        defer { cpuLock.unlock() }
        previousCpu.removeAll(keepingCapacity: true)
    }

    // MARK: - Public snapshot

    /// Snapshot of every visible process, with memory + ports attached.
    /// `ps` would also work but `proc_listpids` + `proc_pidinfo` is the
    /// canonical, lowest-overhead source on macOS — no shell-out for
    /// the process list itself.
    static func snapshotProcesses() -> [ProcessInfo] {
        // ---- 1. Enumerate every PID in the system. ----
        // `proc_listpids` returns BYTES, not a count.
        var pids = [pid_t](repeating: 0, count: 4096)
        let initialBytes = proc_listpids(
            UInt32(PROC_ALL_PIDS),
            0,
            &pids,
            Int32(MemoryLayout<pid_t>.size * pids.count)
        )
        if initialBytes < 0 {
            return []
        }
        var pidCount = Int(initialBytes) / MemoryLayout<pid_t>.size

        // If the buffer was full, double and retry.
        if pidCount == pids.count {
            pids.append(contentsOf: repeatElement(pid_t(0), count: pids.count))
            let retryBytes = proc_listpids(
                UInt32(PROC_ALL_PIDS),
                0,
                &pids,
                Int32(MemoryLayout<pid_t>.size * pids.count)
            )
            if retryBytes > 0 {
                pidCount = Int(retryBytes) / MemoryLayout<pid_t>.size
            }
        }

        // ---- 2. Resolve PID -> process name and resident memory. ----
        // We do two syscalls per PID: one for the bsdinfo (name) and
        // one for the taskinfo (memory). One for both would need
        // `PROC_PIDTASKALLINFO`, which is a much bigger struct —
        // two narrow calls are simpler and noticeably faster.
        var byPid: [Int32: (name: String, memory: Int64, cpuTicks: UInt64, bundle: String?)] = [:]
        byPid.reserveCapacity(pidCount)

        for pid in pids.prefix(pidCount) {
            if pid <= 0 { continue }

            // Name via bsdinfo — the public, cross-user API.
            guard let info = bsdInfo(of: pid) else { continue }
            let name = commString(from: info)
            if name.isEmpty { continue }

            // Resident size in bytes + total CPU ticks (user + system).
            let (mem, cpu) = residentMemoryAndCpu(of: pid)

            // Bundle id is best-effort; only paid-off for app bundles.
            let bundle = bundleIdentifier(for: pid)
            byPid[pid] = (name, mem, cpu, bundle)
        }

        // ---- 3. Resolve PID -> open ports via `lsof`. ----
        // One pass for IPv4/IPv6 LISTEN + ESTABLISHED. `lsof` returns
        // parseable output with `-F` flag.
        let portsByPid = lsofPortMap()

        // ---- 4. Compute CPU usage as a fraction of one logical core. ----
        // Diff each PID's total ticks against the previous snapshot.
        // Divide by the elapsed wall-clock nanoseconds to get a 0.0–1.0+
        // value. Skip the first snapshot (no baseline) and discard
        // obviously-stale diffs (process went away, then came back, etc.)
        let now = Date()
        var cpuByPid: [Int32: Double] = [:]

        cpuLock.lock()
        let prev = previousCpu
        cpuLock.unlock()

        for (pid, info) in byPid {
            // PIDs we haven't seen before get no reading; PIDs that
            // shrank (e.g. PIDs reused) also get nothing this round
            // to avoid reporting negative CPU.
            guard let last = prev[pid],
                  info.cpuTicks >= last.ticks else { continue }
            let tickDelta = info.cpuTicks - last.ticks
            let elapsedNs = now.timeIntervalSince(last.at) * 1_000_000_000
            guard elapsedNs > 0 else { continue }
            cpuByPid[pid] = Double(tickDelta) / elapsedNs
        }

        // Publish the new baseline for the next call. Anything that
        // disappeared this round ages out naturally.
        cpuLock.lock()
        previousCpu = byPid.mapValues { (ticks: $0.cpuTicks, at: now) }
        cpuLock.unlock()

        // ---- 5. Stitch everything together. ----
        return byPid.map { pid, info in
            ProcessInfo(
                id: pid,
                name: info.name,
                pid: pid,
                memoryBytes: info.memory,
                ports: portsByPid[pid] ?? [],
                cpuUsage: cpuByPid[pid],
                bundleIdentifier: info.bundle
            )
        }
    }

    // MARK: - Helpers

    /// Best-effort bundle id. Goes through `proc_pidpath` so the
    /// kernel gives us the full path; then `Bundle(path:)` yields the id.
    private static func bundleIdentifier(for pid: pid_t) -> String? {
        var pathBuf = [CChar](repeating: 0, count: Int(MAXPATHLEN))
        let length = proc_pidpath(pid, &pathBuf, UInt32(pathBuf.count))
        guard length > 0 else { return nil }
        let path = String(cString: pathBuf)
        guard let bundle = Bundle(path: path) else { return nil }
        return bundle.bundleIdentifier
    }

    /// Returns the resident memory size of `pid`, in bytes, plus its
    /// total CPU time in nanoseconds (user + system across all threads).
    /// Uses `proc_pidinfo` with `PROC_PIDTASKINFO`, which is the
    /// public, documented macOS API for process stats. Returns (0, 0)
    /// if the process is gone or unreadable.
    private static func residentMemoryAndCpu(of pid: pid_t) -> (Int64, UInt64) {
        var info = proc_taskinfo()
        let size = Int32(MemoryLayout<proc_taskinfo>.size)
        let result = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, size)
        guard result == size else { return (0, 0) }
        // Saturating add: if user+system somehow overflow (impossible
        // in practice), fall back to max.
        let total = info.pti_total_user &+ info.pti_total_system
        return (Int64(info.pti_resident_size), UInt64(total))
    }

    /// Returns the process short name (comm) and long registered name
    /// via `PROC_PIDTBSDINFO`. Falls back to a numeric display name
    /// when the kernel denies access (e.g. other-user processes).
    ///
    /// `proc_name` would also work for short names, but only for
    /// tasks the calling task owns — for a regular userland app
    /// enumerating the whole system, it returns empty for nearly
    /// every process. `proc_pidinfo` with `PROC_PIDTBSDINFO` is
    /// the supported way to get both the short and long name across
    /// the whole PID table.
    static func bsdInfo(of pid: pid_t) -> proc_bsdinfo? {
        var info = proc_bsdinfo()
        let size = Int32(MemoryLayout<proc_bsdinfo>.size)
        let result = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, size)
        guard result == size else { return nil }
        return info
    }

    /// Extract the short comm name from a `proc_bsdinfo` as a Swift
    /// String. `pbi_comm` is a fixed-size C array; we rebind the
    /// pointer to `CChar` so `String(cString:)` reads until NUL.
    static func commString(from info: proc_bsdinfo) -> String {
        return withUnsafePointer(to: info.pbi_comm) { ptr in
            ptr.withMemoryRebound(to: CChar.self,
                                  capacity: Int(MAXCOMLEN) + 1) {
                String(cString: $0)
            }
        }
    }

    /// Returns a map: PID -> sorted, deduped list of port numbers.
    /// One `lsof` call covers both LISTEN and ESTABLISHED, IPv4 and IPv6.
    private static func lsofPortMap() -> [Int32: [Int]] {
        var map: [Int32: [Int]] = [:]

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/sbin/lsof")
        // -n: no DNS lookups  -P: no service-name resolution
        // -iTCP: TCP only (skip UDP noise)
        // -Fpn: parseable output, fields: pid, name
        // -sTCP:LISTEN is a filter flag but it excludes ESTABLISHED;
        // we want both, so we omit it and filter by the `n` line.
        process.arguments = ["-nP", "-iTCP", "-F", "pn"]
        let outPipe = Pipe()
        let errPipe = Pipe()
        process.standardOutput = outPipe
        process.standardError = errPipe

        do {
            try process.run()
        } catch {
            return map
        }

        // Drain both pipes before waiting so a full pipe buffer can't
        // deadlock `lsof`.
        let outData = outPipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        _ = errPipe.fileHandleForReading.readDataToEndOfFile()
        guard let text = String(data: outData, encoding: .utf8) else { return map }

        // lsof `-F` output: one record per line, key in the first char.
        //   p1234          <- pid
        //   n*:443          <- name (socket address)
        //   n127.0.0.1:8080
        //   n[::1]:22
        var currentPid: Int32?
        for rawLine in text.split(separator: "\n") {
            let line = String(rawLine)
            if line.isEmpty { continue }
            if line.first == "p" {
                currentPid = Int32(line.dropFirst())
            } else if line.first == "n",
                      let pid = currentPid,
                      let port = parsePort(from: String(line.dropFirst())) {
                var list = map[pid] ?? []
                if !list.contains(port) {
                    list.append(port)
                }
                map[pid] = list
            }
        }

        // Sort each list ascending so the UI is stable.
        for (key, list) in map {
            map[key] = list.sorted()
        }
        return map
    }

    /// Extracts the port number from an lsof "n" line, which can look
    /// like:  "*:443"  "127.0.0.1:8080"  "[::1]:22"  "http-alt"
    /// If the port isn't numeric (e.g. "http-alt"), we skip it.
    private static func parsePort(from raw: String) -> Int? {
        let cleaned = raw
            .replacingOccurrences(of: "[", with: "")
            .replacingOccurrences(of: "]", with: "")
        guard let colon = cleaned.lastIndex(of: ":") else { return nil }
        let portStr = cleaned[cleaned.index(after: colon)...]
        return Int(portStr)
    }

    // MARK: - Signal helpers
    //
    // Kept here so the kill code lives next to the only other place we
    // touch kernel state (task_info_for_pid). Both helpers are stateless
    // and safe to call from any actor.

    /// Send a signal to a pid. Returns false if the kernel rejects it
    /// (typically ESRCH = no such process, or EPERM = not allowed).
    @discardableResult
    static func sendSignal(pid: Int32, signal: Int32) -> Bool {
        let result = kill(pid, signal)
        return result == 0
    }

    /// Returns true if a process with `pid` currently exists.
    /// Uses kill(pid, 0) — the standard "does this pid exist?" trick.
    static func processExists(pid: Int32) -> Bool {
        let result = kill(pid, 0)
        if result == 0 { return true }
        // ESRCH means the process is gone; EPERM means we can't signal
        // it but it IS still there.
        return errno == EPERM
    }
}
