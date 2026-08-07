//
//  MemoryStats.swift
//  memWatch
//
//  Provides total physical memory via sysctl.
//

import Darwin
import Foundation

enum MemoryStats {
    static func getTotalPhysicalMemory() -> UInt64 {
        var memSize: UInt64 = 0
        let success = "hw.memsize".withCString { cname in
            var len = size_t(MemoryLayout<UInt64>.size)
            return sysctlbyname(cname, &memSize, &len, nil, 0) == 0
        }
        return success ? memSize : 0
    }
}