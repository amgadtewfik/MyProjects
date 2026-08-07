# memWatch Improvement Ideas

## Overview

`memWatch` is a macOS SwiftUI application that monitors system processes, showing their memory usage, CPU usage, PID, name, and open ports. It allows users to sort by different metrics, refresh manually or automatically, and terminate/kill processes.

The current implementation uses:
- Swift/SwiftUI for UI
- Darwin APIs (`proc_listpids`, `proc_pidinfo`) for process enumeration and memory/CPU data
- `lsof` command for port information
- No entitlements needed (uses standard macOS APIs)

---

## 1. UI/UX Improvements

### 1.1 Process Icons & App Identification
**Current:** Uses a generic "app.fill" system icon for all processes.

**Improvement:**
- Resolve real app icons using `bundleIdentifier` or process path
- Use `NSWorkspace.shared.image(forFile:)` to get the actual application icon
- Fallback to process name initials or custom generated icons for CLI tools/daemons
- Display bundle identifiers when available in a subtle tooltip

### 1.2 Memory Visualization
**Current:** Shows memory as text (e.g., "1.4 GB").

**Improvement:**
- Add a visual bar or chart showing relative memory usage compared to total system memory
- Color-code memory bars: green for low, yellow for medium, red for high usage
- Show memory trends over time (mini sparkline chart)

### 1.3 CPU Visualization
**Current:** Shows CPU as percentage text ("12.3%").

**Improvement:**
- Add a visual gauge or bar for CPU usage
- Color-code: green (<5%), yellow (5-20%), red (>20%)
- Show per-thread CPU breakdown if available via `proc_pidinfo(PROC_PIDTHREADINFO)`

### 1.4 Search & Filter Functionality
**Current:** No search or filtering capabilities.

**Improvement:**
- Add a search field to filter processes by name, PID, or bundle identifier
- Add toggle filters for:
  - System processes vs user processes
  - Processes with open ports vs without
  - High memory/CPU consumers
- Save filter preferences in Settings

### 1.5 Process Details Panel
**Current:** Table view only shows basic info.

**Improvement:**
- Add a right-side panel or popover showing detailed process information when selected:
  - Full executable path
  - Parent PID
  - User/owner of the process
  - Start time
  - Memory breakdown (RSS, Virtual Size, Shared Size)
  - Open files and network connections

### 1.6 Customizable Column Visibility & Widths
**Current:** All columns are fixed with specific widths.

**Improvement:**
- Allow users to hide/show custom columns via Settings or a column visibility menu
- Support resizable columns (currently Table view doesn't support this easily in SwiftUI)

---

## 2. Performance & Optimization

### 2.1 `lsof` Command Optimization
**Current:** Uses `lsof -nP -iTCP -F pn` which can be slow with many processes.

**Improvement:**
- Cache port data between refreshes to avoid re-running `lsof` every time
- Only run `lsof` when a new process appears or disappears
- Consider using `netstat -anp tcp` or BSD socket APIs as an alternative/faster option
- Implement rate-limiting for the `lsof` command (e.g., only run it every 5 seconds instead of every refresh)

### 2.2 Incremental Process Updates
**Current:** Re-fetches all processes on every refresh.

**Improvement:**
- Track which PIDs appeared/disappeared between snapshots
- Only fetch detailed info for new or changed PIDs
- Use the snapshot's `previousCpu` tracking to only update CPU for active processes

### 2.3 Background Processing Optimization
**Current:** Uses `Task.detached(priority: .userInitiated)` for syscalls.

**Improvement:**
- Consider using a dedicated background thread with proper QoS
- Add a mechanism to pause syscalls when the app is in the background (via `NSApplicationDelegate.applicationDidResignActive`)

### 2.4 Memory Footprint Reduction
**Current:** Stores full process list in memory on each refresh.

**Improvement:**
- Implement a history limit for tracked processes (e.g., keep only last 100 snapshots)
- Use more efficient data structures for port mapping

---

## 3. Feature Enhancements

### 3.1 Process History & Trends
**Current:** No historical tracking of process memory/CPU usage.

**Improvement:**
- Store historical data points (memory, CPU over time)
- Add a "History" view showing graphs/trends for selected processes
- Detect and alert on memory leaks (processes that continuously grow in memory)

### 3.2 Custom Alert Rules & Notifications
**Current:** No alerts or notifications for high resource usage.

**Improvement:**
- Allow users to set thresholds:
  - "Alert when process uses > X GB of memory"
  - "Alert when CPU usage exceeds Y%"
- Send macOS native notifications (via `UNUserNotificationCenter`)
- Add visual indicators in the toolbar or footer for alerts

### 3.3 Process Grouping & Hierarchy
**Current:** Flat list of all processes.

**Improvement:**
- Show process hierarchy (parent-child relationships) via `proc_bsdinfo.pbi_ppid`
- Allow grouping by parent process (e.g., all Chrome tabs under "Google Chrome")
- Expandable/collapsible rows for process trees

### 3.4 Process Labels & Custom Names
**Current:** Uses kernel-provided names only.

**Improvement:**
- Allow users to tag/custom-name processes in Settings
- Store custom labels in a local plist or UserDefaults
- Display custom names instead of or alongside kernel names

### 3.5 Export & Reporting
**Current:** No export functionality.

**Improvement:**
- Add "Export" button to save process list as CSV/JSON
- Include timestamp, process name, memory, CPU, ports in export
- Support sharing reports via macOS Share API

---

## 4. Security & Permissions

### 4.1 Enhanced Process Visibility
**Current:** May not show all system processes due to permission restrictions.

**Improvement:**
- Add a "Request Full Access" option that guides users to add the app to Accessibility or Full Disk Access in System Settings
- Explain why certain processes appear as "Unknown" or have missing data

### 4.2 Kill Process Confirmation & Safety
**Current:** Allows killing processes directly from the UI.

**Improvement:**
- Add a confirmation dialog before killing processes, especially system ones
- Warn users if they attempt to kill essential macOS processes (e.g., `kernel_task`, `sysmond`, `mds`)
- Show process importance/role in the details panel to help users decide

### 4.3 App Sandbox Compliance
**Current:** Built without Xcode, uses ad-hoc signing.

**Improvement:**
- Consider submitting to the Mac App Store with proper sandbox entitlements
- If sandboxed, handle port detection via alternative APIs (e.g., `getsockopt` or custom socket scanning)

---

## 5. Data Visualization & Analytics

### 5.1 Memory Distribution Chart
**Improvement:**
- Add a pie chart or stacked bar showing memory distribution across:
  - User processes
  - System processes
  - Cache/buffer memory
- Use `vm_statistics` or `host_statistics` to get system-level memory breakdown

### 5.2 Top Consumers Dashboard
**Improvement:**
- Add a "Top 10 Memory Consumers" view/dashboard
- Add a "Top 10 CPU Consumers" view/dashboard
- Quick access to these views from the main toolbar

### 5.3 Resource Usage Over Time (Graphs)
**Improvement:**
- Implement line charts showing memory/CPU trends over the last hour/day
- Use `SwiftUI.Chart` or a custom drawing view for trend visualization

---

## 6. Code Quality & Architecture

### 6.1 Actor & Concurrency Improvements
**Current:** `ProcessStore` is marked with `@MainActor`, but `ProcessService.snapshotProcesses()` runs on detached task.

**Improvement:**
- Consider making `ProcessService` an `@Actor` (e.g., `DataActor`) to separate data fetching from UI state
- Use proper `async/await` patterns throughout instead of mixed `Task.detached` and `MainActor.run`

### 6.2 Error Handling & Resilience
**Current:** Fails silently if `lsof` or syscalls fail.

**Improvement:**
- Add error logging/catching for failed syscalls
- Show a subtle warning in the UI if data is incomplete due to permissions or errors
- Implement retry logic for transient failures

### 6.3 Unit Testing & Test Coverage
**Current:** No test files visible.

**Improvement:**
- Add XCTest suite for:
  - `ProcessInfo` hashing and equality
  - `ProcessStore` sorting logic
  - `ProcessService.port parsing` (mock `lsof` output)
- Use Swift Testing framework for modern test organization

### 6.4 Configuration & Preferences Persistence
**Current:** Settings are stored in `ProcessStore` but may not persist across launches.

**Improvement:**
- Persist `sortKey`, `sortAscending`, `autoRefresh`, `refreshInterval` to `UserDefaults`
- Load saved preferences on app launch
- Add a "Reset to Defaults" option in Settings

---

## 7. macOS-Specific Enhancements

### 7.1 Status Menu Integration (Menu Bar App)
**Current:** Main window application only.

**Improvement:**
- Add a status bar icon version of memWatch
- Show top memory consumer or total process count in the menu
- Allow quick access to kill processes from the menu
- Toggle main window visibility from the menu

### 7.2 Dark/Light Mode Adaptation
**Current:** Uses standard SwiftUI colors.

**Improvement:**
- Ensure all charts, bars, and icons adapt properly to dark/light mode
- Use `Color.primary`, `Color.secondary`, `Color.accentColor` appropriately

### 7.3 Native macOS Notifications & Focus Modes
**Improvement:**
- Integrate with macOS Focus Modes (do not disturb) to suppress notifications during focused work
- Add "Quiet Mode" toggle in Settings that disables alerts but keeps monitoring

---

## Priority Recommendations

| Category | Idea | Impact | Effort | Priority |
|----------|------|--------|--------|----------|
| UI/UX | Process Icons & App Identification | High | Medium | **High** |
| UI/UX | Search & Filter Functionality | High | Medium | **High** |
| Performance | `lsof` Command Optimization | High | Low | **High** |
| Features | Process History & Trends | Medium | High | Medium |
| Features | Custom Alert Rules & Notifications | Medium | Medium | **Medium** |
| Architecture | Actor & Concurrency Improvements | Medium | Medium | **Medium** |
| macOS-Specific | Status Menu Integration (Menu Bar App) | High | High | **Low** (Phase 2) |
