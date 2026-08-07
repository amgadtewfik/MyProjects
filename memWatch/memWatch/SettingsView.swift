//
//  SettingsView.swift
//  memWatch
//
//  Settings window: toggle auto-refresh and set its interval.
//

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var store: ProcessStore

    var body: some View {
        Form {
            Section("Refresh") {
                Toggle("Auto-refresh process list", isOn: Binding(
                    get: { store.autoRefresh },
                    set: { store.setAutoRefresh($0) }
                ))
                .help("When off, you can still refresh manually from the toolbar.")

                HStack {
                    Text("Interval")
                    Spacer()
                    TextField("seconds", value: Binding(
                        get: { store.refreshInterval },
                        set: { store.setRefreshInterval($0) }
                    ), format: .number.precision(.fractionLength(1)))
                    .multilineTextAlignment(.trailing)
                    .frame(width: 60)
                    .disabled(!store.autoRefresh)
                    Text("sec")
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(20)
        .frame(width: 360)
    }
}
