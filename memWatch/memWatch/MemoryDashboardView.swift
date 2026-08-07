//
//  MemoryDashboardView.swift
//  memWatch
//
//  A visual dashboard showing total memory usage compared to physical memory.
//

import SwiftUI
import Foundation

struct MemoryDashboardView: View {
    @EnvironmentObject var memoryStats: MemoryStatsStore
    @EnvironmentObject var processStore: ProcessStore
    
    private var totalMemory: UInt64 { memoryStats.totalMemory }
    private var usedMemory: UInt64 { memoryStats.usedMemory }
    private var freeMemory: UInt64 {
        totalMemory > usedMemory ? totalMemory - usedMemory : 0
    }
    private var usagePercentage: Double {
        guard totalMemory > 0 else { return 0.0 }
        return Double(usedMemory) / Double(totalMemory)
    }
    
    var body: some View {
        VStack(spacing: 12) {
            HStack(spacing: 16) {
                MemoryGaugeCircle(percentage: usagePercentage)
                    .frame(width: 100, height: 100)
                
                VStack(alignment: .leading, spacing: 6) {
                    Text("Memory Usage")
                        .font(.headline)
                    Text("\(formattedBytes(usedMemory)) / \(formattedBytes(totalMemory))")
                        .font(.title3)
                        .monospacedDigit()
                        .foregroundStyle(getMemoryColor(for: usagePercentage))
                    Text(String(format: "%.1f%% used", usagePercentage * 100))
                        .font(.callout)
                        .foregroundStyle(getMemoryColor(for: usagePercentage))
                }
            }
        }
        .padding(12)
        .background(Color(white: 1).opacity(0.8))
        .onChange(of: processStore.processes) { _ in
            let total = processStore.processes.reduce(UInt64(0)) { $0 + UInt64(max(0, $1.memoryBytes)) }
            memoryStats.update(usedByProcesses: total)
        }
        .onAppear {
            let total = processStore.processes.reduce(UInt64(0)) { $0 + UInt64(max(0, $1.memoryBytes)) }
            memoryStats.update(usedByProcesses: total)
        }
    }
    
    private func getMemoryColor(for percentage: Double) -> Color {
        if percentage < 0.5 { return .green }
        else if percentage < 0.75 { return .yellow }
        else { return .red }
    }
    
    private func formattedBytes(_ bytes: UInt64) -> String {
        let formatter = ByteCountFormatter()
        formatter.allowedUnits = [.useKB, .useMB, .useGB]
        formatter.countStyle = .binary
        return formatter.string(fromByteCount: Int64(bytes))
    }
}

struct MemoryGaugeCircle: View {
    let percentage: Double
    
    var body: some View {
        ZStack {
            Circle()
                .stroke(Color.gray.opacity(0.3), lineWidth: 8)
            Circle()
                .trim(from: 0, to: CGFloat(min(percentage, 1.0)))
                .stroke(getMemoryColor(for: percentage), lineWidth: 8)
                .rotationEffect(.degrees(-90))
                .animation(.easeOut, value: percentage)
            Text(String(format: "%.0f%%", min(percentage, 1.0) * 100))
                .font(.system(size: 14, weight: .bold))
                .foregroundStyle(getMemoryColor(for: percentage))
        }
    }
    
    private func getMemoryColor(for percentage: Double) -> Color {
        if percentage < 0.5 { return .green }
        else if percentage < 0.75 { return .yellow }
        else { return .red }
    }
}