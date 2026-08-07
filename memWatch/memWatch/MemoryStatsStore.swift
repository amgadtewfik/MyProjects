//
//  MemoryStatsStore.swift
//  memWatch
//
//  ObservableObject that owns total physical memory and used memory from processes.
//

import Combine
import Foundation
import SwiftUI

@MainActor
final class MemoryStatsStore: ObservableObject {
    @Published var totalMemory: UInt64 = 0
    @Published var usedMemory: UInt64 = 0
    
    func update(usedByProcesses: UInt64) {
        totalMemory = MemoryStats.getTotalPhysicalMemory()
        usedMemory = usedByProcesses
    }
}