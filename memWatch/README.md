# memWatch

A native macOS app for monitoring system memory and running processes.
Built with SwiftUI + AppKit, no Xcode required — `build.sh` produces a
runnable `.app` bundle straight from the Swift command-line toolchain.

## Features

- **Memory dashboard** — circular gauge showing used vs. total physical
  memory (read from `hw.memsize` via `sysctl`), with used/free breakdown.
- **Process table** — sortable list of every visible process on the
  system with name, PID, resident memory, CPU usage, and open ports.
  Rows animate smoothly across refreshes (stable IDs).
- **Auto-refresh** — configurable interval (default 1 s); can be disabled
  in settings for manual refresh only.
- **Kill from the toolbar** — terminate selected processes.
- **Settings window** — `⌘,` toggles auto-refresh and interval.

## Requirements

- macOS 13 or newer (deployment target pulled from `.pbxproj`).
- Xcode Command Line Tools (provides Swift compiler and macOS SDK):
  ```sh
  xcode-select --install
  ```

## Build

From the project root:

```sh
./build.sh
```

This produces `./build/memWatch.app`. To run:

```sh
open ./build/memWatch.app
```

`build.sh` auto-detects the host architecture (`arm64` or `x86_64`),
reads the deployment target from `memWatch.xcodeproj/project.pbxproj`,
and links against the macOS SDK shipped with the Command Line Tools.

## Project Layout

```
memWatch/
├── build.sh                              # Build wrapper (Swift CLI toolchain)
├── generate_icon.py                      # Regenerates AppIcon.icns from PNGs
├── docs/improvement_ideas.md             # Design notes / TODOs
├── memWatch.xcodeproj/                   # Xcode project (project.pbxproj)
├── memWatch/
│   ├── memoryMonitorApp.swift            # @main entry point
│   ├── ContentView.swift                 # Main window + table + toolbar
│   ├── MemoryDashboardView.swift         # Top-of-window gauge
│   ├── MemoryStats.swift                 # sysctl total-memory read
│   ├── MemoryStatsStore.swift            # @Observable store for memory stats
│   ├── ProcessInfoModel.swift            # ProcessInfo struct (pid, RSS, CPU, ports)
│   ├── ProcessService.swift              # Process enumeration (libproc / proc_pidinfo)
│   ├── ProcessStore.swift                # @Observable store + refresh timer
│   ├── SettingsView.swift                # Settings window (⌘,)
│   └── Assets.xcassets/                  # AppIcon + AccentColor
└── README.md
```

## Architecture

Two `@StateObject` stores own the app's state:

- **`ProcessStore`** — drives the process table; holds the auto-refresh
  timer and exposes `setAutoRefresh` / `setRefreshInterval` to the
  Settings view.
- **`MemoryStatsStore`** — drives the dashboard; computes used memory
  from total physical memory.

Both are injected via `.environmentObject` from `memoryMonitorApp.swift`
and consumed in views with `@EnvironmentObject`. `ProcessInfo` keeps a
stable `pid` as its `Identifiable.id`, so SwiftUI animates row diffs
instead of recreating every row on each tick.

## License

Personal project — no license specified.