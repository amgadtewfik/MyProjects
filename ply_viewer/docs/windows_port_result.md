# Windows Build — Shared Renderer Core

User-facing summary of the Windows side of PLY Viewer.
Repo: `/Users/amgad/Desktop/Ai/ply_viewer`

## What changed

The Windows executable no longer uses the old v5 design in
`/Users/amgad/Desktop/Ai/ply_viewer/backup/ply_viewer_v5.c`. It now links the
same renderer core as the macOS app, `src/mac/ply_core.c`, so the four macOS
rendering fixes apply to Windows by construction rather than being
re-implemented:

1. Points mode draws opaque discs with the depth test on — no alpha blending
   combined with depth writes, which was the source of the angle-dependent noise.
2. Splat mode sorts back to front (background counting sort) with premultiplied
   alpha and depth writes off.
3. 3DGS decode is `0.5 + 0.28209479 * f_dc` for color, `sigmoid(opacity)` for
   opacity, `exp(scale_*)` for scale.
4. Point radius comes from the grid-based median nearest-neighbour spacing times
   `pointSize`, projected with true perspective.

## Files

| Path | Role |
| --- | --- |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/mac/ply_core.c` | Shared renderer core; only its include block is platform-conditional |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/win/gl3_win.h` | Declares the 38 non-GL-1.1 entrypoints and macro-renames them onto loaded pointers |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/win/gl3_win.c` | Resolves them via `wglGetProcAddress`, falling back to `opengl32.dll` |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/win/main_win.c` | Win32 glue: WGL core context, menus, mouse/keyboard, file dialog, drag & drop, background loader, snapshot |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/win/png_write.c` | Minimal PNG writer (stored-deflate) for snapshots, replacing `NSBitmapImageRep` |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/win/ply_viewer.rc` | Icon and version resource |
| `/Users/amgad/Desktop/Ai/ply_viewer/src/win/build_win.sh` | mingw-w64 cross-compile |
| `/Users/amgad/Desktop/Ai/ply_viewer/dist/PLY_Viewer.exe` | Output, 468 KB, PE32+ GUI x86-64 |

## Build

```sh
brew install mingw-w64
bash /Users/amgad/Desktop/Ai/ply_viewer/src/win/build_win.sh
```

Links `-static -static-libgcc`, so the only imports are system DLLs
(`OPENGL32`, `GDI32`, `USER32`, `SHELL32`, `COMDLG32`, `ADVAPI32`, `KERNEL32`
and the Windows 10 UCRT stubs). No `libwinpthread-1.dll` to ship alongside.

## Interaction parity with the macOS app

| Input | Action |
| --- | --- |
| Left drag | Orbit |
| Right / middle drag, Ctrl+Left, Alt+Left | Pan |
| Wheel | Zoom at cursor |
| Double click | Pick pivot |
| `R` `F` `G` `E` `P` `M` | Reset, fit, grid, EDL, projection, points/splats |
| `1`–`7` | Front, back, left, right, top, bottom, iso |
| `+` `-` | Zoom |
| `[` `]` | Point and splat size |
| Arrows | Orbit 12° |

Settings live in the menu bar (View / Display / Color) instead of a docked
inspector, and are persisted to `HKCU\Software\PLY Viewer`, together with the
recent-files list. Window title carries name, point count, mode and FPS, and
doubles as the load progress readout.

CLI options match `main.m`: a positional `.ply` path, `--snapshot <out.png>`,
`--mode points|splats`, `--view front|back|left|right|top|bottom|iso`. As on
macOS, a snapshot waits for `ply_sort_settled()` so the first unsorted splat
frame is never captured.

## Verification status

| Check | Result |
| --- | --- |
| `x86_64-w64-mingw32-gcc` compiles `ply_core.c` unchanged for Windows | Pass |
| Full link | Pass, `dist/PLY_Viewer.exe`, PE32+ executable (GUI) x86-64 |
| `-Wall -Wextra` | Only unavoidable `GetProcAddress` function-pointer casts |
| Import table free of non-system DLLs | Pass |
| macOS app still builds after the include-block change | Pass |
| Runtime behaviour on Windows | **Not verified** — no Windows machine or Wine on this host |

The last row is the open item: the binary needs a run on real Windows hardware
to confirm the WGL 3.3/4.1 core context path and the splat pipeline.

## Fixed after first run on Windows (2026-09-26)

The first build exited immediately on launch with no window and no error.
Cause: `create_gl_context` created its throwaway WGL probe window with the main
window class, so `DestroyWindow` on it ran the app's `WM_DESTROY` handler and
`PostQuitMessage` put `WM_QUIT` in the queue before the real window was shown.
The probe window now uses its own `DefWindowProc` class, `WM_DESTROY` is guarded
on the main window, `ply_gl_shutdown` moved into `WM_CLOSE` while the context is
still valid, and startup failures now report instead of returning silently.
