#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gl3_win.h"
#include <GL/wglext.h>

#include "ply_core.h"
#include "png_write.h"

#define APP_TITLE "PLY Viewer"
#define WND_CLASS "PLYViewerWindow"
#define DUMMY_CLASS "PLYViewerDummy"
#define MAX_RECENT 8

#define WM_PLY_LOADED (WM_APP + 1)

enum {
    ID_FILE_OPEN = 1000, ID_FILE_CLOSE, ID_FILE_SNAPSHOT, ID_FILE_EXIT,
    ID_RECENT_BASE = 1100, ID_RECENT_CLEAR = 1199,

    ID_MODE_POINTS = 1200, ID_MODE_SPLATS,
    ID_PROJ_PERSP, ID_PROJ_ORTHO,
    ID_VIEW_FRONT, ID_VIEW_BACK, ID_VIEW_LEFT, ID_VIEW_RIGHT,
    ID_VIEW_TOP, ID_VIEW_BOTTOM, ID_VIEW_ISO,
    ID_CAM_FIT, ID_CAM_RESET,
    ID_TOG_GRID, ID_TOG_AXES, ID_TOG_EDL, ID_TOG_SHADING,

    ID_SIZE_UP = 1300, ID_SIZE_DOWN,
    ID_SPLAT_UP, ID_SPLAT_DOWN,
    ID_CUTOFF_UP, ID_CUTOFF_DOWN,
    ID_EDL_UP, ID_EDL_DOWN,
    ID_FOV_UP, ID_FOV_DOWN,
    ID_COLOR_RGB, ID_COLOR_HEIGHT, ID_COLOR_NORMAL, ID_COLOR_UNIFORM,
    ID_BG_STUDIO, ID_BG_BLACK, ID_BG_GRAY, ID_BG_WHITE,
    ID_UP_POSY, ID_UP_POSZ, ID_UP_NEGY, ID_UP_NEGZ,

    ID_HELP_KEYS = 1400, ID_HELP_ABOUT
};

typedef struct {
    HWND  hwnd;
    HDC   hdc;
    HGLRC hrc;
    HMENU menu;
    HMENU recentMenu;

    int   ready;
    int   dirty;
    int   loading;
    int   orbiting;
    int   panning;
    POINT lastMouse;

    double lastDrawTime;
    double fps;

    char  recent[MAX_RECENT][PLY_MAX_PATH];
    int   recentCount;

    char  pendingSnapshot[PLY_MAX_PATH];
    int   snapshotQuits;

    char  cliSnapshot[PLY_MAX_PATH];
    char  cliMode[32];
    char  cliView[32];
    char  cliFile[PLY_MAX_PATH];

    volatile LONG progressPermille;
    char  loadName[256];
    char  loadErr[512];
} AppState;

static AppState g;

static double now_sec(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

static void request_redraw(void) { g.dirty = 1; }

/* ------------------------------------------------------------------ */
/* recent files                                                        */

static void recent_load(void) {
    HKEY key;
    g.recentCount = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\PLY Viewer", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    for (int i = 0; i < MAX_RECENT; i++) {
        char name[32];
        sprintf(name, "Recent%d", i);
        DWORD type = 0, len = PLY_MAX_PATH;
        char buf[PLY_MAX_PATH];
        if (RegQueryValueExA(key, name, NULL, &type, (LPBYTE)buf, &len) != ERROR_SUCCESS) break;
        if (type != REG_SZ || len == 0) break;
        buf[len < PLY_MAX_PATH ? len : PLY_MAX_PATH - 1] = 0;
        strncpy(g.recent[g.recentCount], buf, PLY_MAX_PATH - 1);
        g.recent[g.recentCount][PLY_MAX_PATH - 1] = 0;
        g.recentCount++;
    }
    RegCloseKey(key);
}

static void recent_save(void) {
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\PLY Viewer", 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    for (int i = 0; i < MAX_RECENT; i++) {
        char name[32];
        sprintf(name, "Recent%d", i);
        if (i < g.recentCount)
            RegSetValueExA(key, name, 0, REG_SZ, (const BYTE*)g.recent[i],
                           (DWORD)strlen(g.recent[i]) + 1);
        else
            RegDeleteValueA(key, name);
    }
    RegCloseKey(key);
}

static void recent_rebuild_menu(void) {
    while (GetMenuItemCount(g.recentMenu) > 0) DeleteMenu(g.recentMenu, 0, MF_BYPOSITION);
    if (g.recentCount == 0) {
        AppendMenuA(g.recentMenu, MF_STRING | MF_GRAYED, 0, "(none)");
        return;
    }
    for (int i = 0; i < g.recentCount; i++) {
        const char* base = strrchr(g.recent[i], '\\');
        base = base ? base + 1 : g.recent[i];
        char label[320];
        _snprintf(label, sizeof label - 1, "&%d  %s", i + 1, base);
        label[sizeof label - 1] = 0;
        AppendMenuA(g.recentMenu, MF_STRING, ID_RECENT_BASE + i, label);
    }
    AppendMenuA(g.recentMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g.recentMenu, MF_STRING, ID_RECENT_CLEAR, "Clear Menu");
}

static void recent_add(const char* path) {
    for (int i = 0; i < g.recentCount; i++) {
        if (_stricmp(g.recent[i], path) == 0) {
            for (int j = i; j > 0; j--) memcpy(g.recent[j], g.recent[j - 1], PLY_MAX_PATH);
            strncpy(g.recent[0], path, PLY_MAX_PATH - 1);
            g.recent[0][PLY_MAX_PATH - 1] = 0;
            recent_save();
            recent_rebuild_menu();
            return;
        }
    }
    if (g.recentCount < MAX_RECENT) g.recentCount++;
    for (int j = g.recentCount - 1; j > 0; j--) memcpy(g.recent[j], g.recent[j - 1], PLY_MAX_PATH);
    strncpy(g.recent[0], path, PLY_MAX_PATH - 1);
    g.recent[0][PLY_MAX_PATH - 1] = 0;
    recent_save();
    recent_rebuild_menu();
}

/* ------------------------------------------------------------------ */
/* settings persistence                                                */

static void settings_load(void) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\PLY Viewer", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    PlySettings s;
    DWORD len = sizeof s, type = 0;
    if (RegQueryValueExA(key, "Settings", NULL, &type, (LPBYTE)&s, &len) == ERROR_SUCCESS &&
        type == REG_BINARY && len == sizeof s) {
        *ply_settings() = s;
    }
    RegCloseKey(key);
}

static void settings_save(void) {
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\PLY Viewer", 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    RegSetValueExA(key, "Settings", 0, REG_BINARY, (const BYTE*)ply_settings(), sizeof(PlySettings));
    RegCloseKey(key);
}

/* ------------------------------------------------------------------ */
/* menus                                                               */

static void build_menu(HWND hwnd) {
    g.menu = CreateMenu();

    HMENU file = CreatePopupMenu();
    g.recentMenu = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ID_FILE_OPEN, "&Open PLY...\tCtrl+O");
    AppendMenuA(file, MF_POPUP, (UINT_PTR)g.recentMenu, "Open &Recent");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_CLOSE, "&Close Cloud\tCtrl+W");
    AppendMenuA(file, MF_STRING, ID_FILE_SNAPSHOT, "Save &Snapshot...\tCtrl+S");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_EXIT, "E&xit\tAlt+F4");
    AppendMenuA(g.menu, MF_POPUP, (UINT_PTR)file, "&File");

    HMENU view = CreatePopupMenu();
    AppendMenuA(view, MF_STRING, ID_MODE_POINTS, "&Points\tM");
    AppendMenuA(view, MF_STRING, ID_MODE_SPLATS, "&Splats\tM");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, ID_PROJ_PERSP, "P&erspective\tP");
    AppendMenuA(view, MF_STRING, ID_PROJ_ORTHO, "&Orthographic\tP");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, ID_VIEW_FRONT, "&Front\t1");
    AppendMenuA(view, MF_STRING, ID_VIEW_BACK, "&Back\t2");
    AppendMenuA(view, MF_STRING, ID_VIEW_LEFT, "&Left\t3");
    AppendMenuA(view, MF_STRING, ID_VIEW_RIGHT, "&Right\t4");
    AppendMenuA(view, MF_STRING, ID_VIEW_TOP, "&Top\t5");
    AppendMenuA(view, MF_STRING, ID_VIEW_BOTTOM, "Botto&m\t6");
    AppendMenuA(view, MF_STRING, ID_VIEW_ISO, "&Isometric\t7");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, ID_CAM_FIT, "Fit to &Cloud\tF");
    AppendMenuA(view, MF_STRING, ID_CAM_RESET, "Reset Camera\tR");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, ID_TOG_GRID, "Show &Grid\tG");
    AppendMenuA(view, MF_STRING, ID_TOG_AXES, "Show &Axes");
    AppendMenuA(view, MF_STRING, ID_TOG_EDL, "Eye-Dome Lighting\tE");
    AppendMenuA(view, MF_STRING, ID_TOG_SHADING, "Normal Shading");
    AppendMenuA(g.menu, MF_POPUP, (UINT_PTR)view, "&View");

    HMENU disp = CreatePopupMenu();
    AppendMenuA(disp, MF_STRING, ID_SIZE_UP, "Point Size &Up\t]");
    AppendMenuA(disp, MF_STRING, ID_SIZE_DOWN, "Point Size &Down\t[");
    AppendMenuA(disp, MF_SEPARATOR, 0, NULL);
    AppendMenuA(disp, MF_STRING, ID_SPLAT_UP, "Splat Scale Up");
    AppendMenuA(disp, MF_STRING, ID_SPLAT_DOWN, "Splat Scale Down");
    AppendMenuA(disp, MF_SEPARATOR, 0, NULL);
    AppendMenuA(disp, MF_STRING, ID_CUTOFF_UP, "Opacity Cutoff Up");
    AppendMenuA(disp, MF_STRING, ID_CUTOFF_DOWN, "Opacity Cutoff Down");
    AppendMenuA(disp, MF_SEPARATOR, 0, NULL);
    AppendMenuA(disp, MF_STRING, ID_EDL_UP, "EDL Strength Up");
    AppendMenuA(disp, MF_STRING, ID_EDL_DOWN, "EDL Strength Down");
    AppendMenuA(disp, MF_SEPARATOR, 0, NULL);
    AppendMenuA(disp, MF_STRING, ID_FOV_UP, "Field of View Up");
    AppendMenuA(disp, MF_STRING, ID_FOV_DOWN, "Field of View Down");
    AppendMenuA(g.menu, MF_POPUP, (UINT_PTR)disp, "&Display");

    HMENU color = CreatePopupMenu();
    AppendMenuA(color, MF_STRING, ID_COLOR_RGB, "&RGB");
    AppendMenuA(color, MF_STRING, ID_COLOR_HEIGHT, "&Height Ramp");
    AppendMenuA(color, MF_STRING, ID_COLOR_NORMAL, "&Normals");
    AppendMenuA(color, MF_STRING, ID_COLOR_UNIFORM, "&Uniform");
    AppendMenuA(color, MF_SEPARATOR, 0, NULL);
    AppendMenuA(color, MF_STRING, ID_BG_STUDIO, "Background: &Studio");
    AppendMenuA(color, MF_STRING, ID_BG_BLACK, "Background: &Black");
    AppendMenuA(color, MF_STRING, ID_BG_GRAY, "Background: &Gray");
    AppendMenuA(color, MF_STRING, ID_BG_WHITE, "Background: &White");
    AppendMenuA(color, MF_SEPARATOR, 0, NULL);
    AppendMenuA(color, MF_STRING, ID_UP_POSY, "Up Axis: +Y");
    AppendMenuA(color, MF_STRING, ID_UP_POSZ, "Up Axis: +Z");
    AppendMenuA(color, MF_STRING, ID_UP_NEGY, "Up Axis: -Y");
    AppendMenuA(color, MF_STRING, ID_UP_NEGZ, "Up Axis: -Z");
    AppendMenuA(g.menu, MF_POPUP, (UINT_PTR)color, "&Color");

    HMENU help = CreatePopupMenu();
    AppendMenuA(help, MF_STRING, ID_HELP_KEYS, "&Keyboard && Mouse");
    AppendMenuA(help, MF_STRING, ID_HELP_ABOUT, "&About");
    AppendMenuA(g.menu, MF_POPUP, (UINT_PTR)help, "&Help");

    SetMenu(hwnd, g.menu);
    recent_rebuild_menu();
}

static void check_radio(int first, int last, int sel) {
    CheckMenuRadioItem(g.menu, first, last, sel, MF_BYCOMMAND);
}

static void sync_menu(void) {
    PlySettings* s = ply_settings();
    const PlyInfo* info = ply_info();
    check_radio(ID_MODE_POINTS, ID_MODE_SPLATS,
                s->renderMode == PLY_MODE_SPLATS ? ID_MODE_SPLATS : ID_MODE_POINTS);
    check_radio(ID_PROJ_PERSP, ID_PROJ_ORTHO, s->ortho ? ID_PROJ_ORTHO : ID_PROJ_PERSP);
    check_radio(ID_COLOR_RGB, ID_COLOR_UNIFORM, ID_COLOR_RGB + s->colorMode);
    check_radio(ID_BG_STUDIO, ID_BG_WHITE, ID_BG_STUDIO + s->background);
    check_radio(ID_UP_POSY, ID_UP_NEGZ, ID_UP_POSY + s->upAxis);
    CheckMenuItem(g.menu, ID_TOG_GRID, MF_BYCOMMAND | (s->showGrid ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g.menu, ID_TOG_AXES, MF_BYCOMMAND | (s->showAxes ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g.menu, ID_TOG_EDL, MF_BYCOMMAND | (s->edl ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g.menu, ID_TOG_SHADING, MF_BYCOMMAND | (s->shading ? MF_CHECKED : MF_UNCHECKED));

    UINT splatFlag = (info && info->isSplat) ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(g.menu, ID_MODE_SPLATS, MF_BYCOMMAND | splatFlag);
    UINT hasCloud = (info && info->numPoints > 0) ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(g.menu, ID_FILE_CLOSE, MF_BYCOMMAND | hasCloud);
    EnableMenuItem(g.menu, ID_FILE_SNAPSHOT, MF_BYCOMMAND | hasCloud);
    DrawMenuBar(g.hwnd);
}

static void update_title(void) {
    const PlyInfo* info = ply_info();
    char title[512];
    if (g.loading) {
        int pct = (int)(InterlockedCompareExchange(&g.progressPermille, 0, 0) / 10);
        _snprintf(title, sizeof title - 1, "Opening %s... %d%%  -  %s", g.loadName, pct, APP_TITLE);
    } else if (info && info->numPoints > 0) {
        PlySettings* s = ply_settings();
        _snprintf(title, sizeof title - 1, "%s  -  %d pts  -  %s  -  %.0f fps  -  %s",
                  info->name, info->numPoints,
                  s->renderMode == PLY_MODE_SPLATS ? "splats" : "points",
                  g.fps, APP_TITLE);
    } else {
        _snprintf(title, sizeof title - 1, "%s  -  open a .ply file or drop one here", APP_TITLE);
    }
    title[sizeof title - 1] = 0;
    SetWindowTextA(g.hwnd, title);
}

/* ------------------------------------------------------------------ */
/* OpenGL context                                                      */

static int create_gl_context(HWND hwnd) {
    WNDCLASSA dwc;
    memset(&dwc, 0, sizeof dwc);
    dwc.style = CS_OWNDC;
    dwc.lpfnWndProc = DefWindowProcA;
    dwc.hInstance = GetModuleHandle(NULL);
    dwc.lpszClassName = DUMMY_CLASS;
    RegisterClassA(&dwc);

    HWND dummy = CreateWindowExA(0, DUMMY_CLASS, "dummy", WS_OVERLAPPED,
                                 0, 0, 1, 1, NULL, NULL, dwc.hInstance, NULL);
    if (!dummy) return 0;
    HDC ddc = GetDC(dummy);

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;

    int dpf = ChoosePixelFormat(ddc, &pfd);
    if (!dpf || !SetPixelFormat(ddc, dpf, &pfd)) {
        ReleaseDC(dummy, ddc); DestroyWindow(dummy); return 0;
    }
    HGLRC drc = wglCreateContext(ddc);
    if (!drc || !wglMakeCurrent(ddc, drc)) {
        if (drc) wglDeleteContext(drc);
        ReleaseDC(dummy, ddc); DestroyWindow(dummy); return 0;
    }

    PFNWGLCHOOSEPIXELFORMATARBPROC choosePF =
        (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    PFNWGLCREATECONTEXTATTRIBSARBPROC createCtx =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    PFNWGLSWAPINTERVALEXTPROC swapInterval =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

    g.hdc = GetDC(hwnd);
    int pf = 0;

    if (choosePF) {
        const int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
            WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
            WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
            WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB,     24,
            WGL_ALPHA_BITS_ARB,     8,
            WGL_DEPTH_BITS_ARB,     24,
            0
        };
        UINT count = 0;
        int found = 0;
        if (choosePF(g.hdc, attribs, NULL, 1, &found, &count) && count > 0) pf = found;
    }
    if (!pf) pf = ChoosePixelFormat(g.hdc, &pfd);
    if (!pf || !SetPixelFormat(g.hdc, pf, &pfd)) {
        wglMakeCurrent(NULL, NULL); wglDeleteContext(drc);
        ReleaseDC(dummy, ddc); DestroyWindow(dummy);
        return 0;
    }

    if (createCtx) {
        const int versions[][2] = { { 4, 1 }, { 3, 3 } };
        for (int i = 0; i < 2 && !g.hrc; i++) {
            const int ctxAttribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, versions[i][0],
                WGL_CONTEXT_MINOR_VERSION_ARB, versions[i][1],
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            g.hrc = createCtx(g.hdc, NULL, ctxAttribs);
        }
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(drc);
    ReleaseDC(dummy, ddc);
    DestroyWindow(dummy);
    UnregisterClassA(DUMMY_CLASS, dwc.hInstance);

    if (!g.hrc) return 0;
    if (!wglMakeCurrent(g.hdc, g.hrc)) return 0;
    if (!ply_gl_load_win()) return 0;
    if (swapInterval) swapInterval(1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* loading                                                             */

static void progress_cb(float fraction, void* user) {
    (void)user;
    LONG v = (LONG)(fraction * 1000.0f);
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    InterlockedExchange(&g.progressPermille, v);
}

typedef struct { char path[PLY_MAX_PATH]; } LoadReq;

static DWORD WINAPI load_thread(LPVOID param) {
    LoadReq* req = (LoadReq*)param;
    char err[512] = "";
    PlyCloud* cloud = ply_load(req->path, progress_cb, NULL, err, sizeof err);
    strncpy(g.loadErr, err, sizeof g.loadErr - 1);
    g.loadErr[sizeof g.loadErr - 1] = 0;
    PostMessageA(g.hwnd, WM_PLY_LOADED, cloud != NULL, (LPARAM)cloud);
    free(req);
    return 0;
}

static void apply_cli_options(void) {
    PlySettings* s = ply_settings();
    if (g.cliMode[0]) s->renderMode = _stricmp(g.cliMode, "splats") == 0 ? PLY_MODE_SPLATS : PLY_MODE_POINTS;
    if (g.cliView[0]) {
        static const char* names[] = { "front", "back", "left", "right", "top", "bottom", "iso" };
        for (int i = 0; i < 7; i++) {
            if (_stricmp(g.cliView, names[i]) == 0) { ply_cam_view(i); break; }
        }
    }
}

static void load_file(const char* path) {
    if (g.loading || !path || !path[0]) return;
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    strncpy(g.loadName, base, sizeof g.loadName - 1);
    g.loadName[sizeof g.loadName - 1] = 0;

    LoadReq* req = (LoadReq*)calloc(1, sizeof(LoadReq));
    if (!req) return;
    strncpy(req->path, path, PLY_MAX_PATH - 1);
    req->path[PLY_MAX_PATH - 1] = 0;

    g.loading = 1;
    InterlockedExchange(&g.progressPermille, 0);
    update_title();

    HANDLE th = CreateThread(NULL, 0, load_thread, req, 0, NULL);
    if (!th) {
        g.loading = 0;
        free(req);
        MessageBoxA(g.hwnd, "Could not start the loader thread.", APP_TITLE, MB_ICONWARNING);
        return;
    }
    CloseHandle(th);
}

static void open_file_dialog(void) {
    char file[PLY_MAX_PATH] = "";
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = "PLY point clouds (*.ply)\0*.ply\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrTitle = "Open PLY";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) load_file(file);
}

static void snapshot_dialog(void) {
    const PlyInfo* info = ply_info();
    char file[PLY_MAX_PATH] = "snapshot.png";
    if (info && info->name[0]) {
        _snprintf(file, sizeof file - 1, "%s.png", info->name);
        file[sizeof file - 1] = 0;
    }
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = "PNG image (*.png)\0*.png\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrTitle = "Save Snapshot";
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return;
    strncpy(g.pendingSnapshot, file, PLY_MAX_PATH - 1);
    g.pendingSnapshot[PLY_MAX_PATH - 1] = 0;
    g.snapshotQuits = 0;
    request_redraw();
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */

static float pixel_scale(void) {
    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
        if (fn) dpi = fn(g.hwnd);
    }
    if (dpi == 0) dpi = 96;
    return (float)dpi / 96.0f;
}

static void draw_frame(void) {
    if (!g.ready) return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    wglMakeCurrent(g.hdc, g.hrc);
    ply_draw(w, h, pixel_scale());

    if (g.pendingSnapshot[0]) {
        if (!ply_sort_settled()) {
            g.dirty = 1;
        } else {
            unsigned char* pixels = (unsigned char*)malloc((size_t)w * h * 4);
            int ok = 0;
            if (pixels && ply_read_pixels(pixels, w, h))
                ok = png_write_rgba(g.pendingSnapshot, pixels, w, h, 1);
            free(pixels);
            if (g.snapshotQuits) {
                fprintf(ok ? stdout : stderr, ok ? "snapshot written\n" : "snapshot failed\n");
                g.pendingSnapshot[0] = 0;
                PostQuitMessage(ok ? 0 : 1);
                return;
            }
            if (!ok) MessageBoxA(g.hwnd, "Could not write the snapshot.", APP_TITLE, MB_ICONWARNING);
            g.pendingSnapshot[0] = 0;
        }
    }

    SwapBuffers(g.hdc);

    double now = now_sec();
    if (g.lastDrawTime > 0) {
        double dt = now - g.lastDrawTime;
        if (dt > 0 && dt < 0.25) g.fps = g.fps * 0.8 + (1.0 / dt) * 0.2;
    }
    g.lastDrawTime = now;
    g.dirty = 0;
}

/* ------------------------------------------------------------------ */
/* input                                                               */

static void normalized_point(int x, int y, float* nx, float* ny) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    *nx = w > 0 ? (float)x / (float)w : 0.5f;
    *ny = h > 0 ? 1.0f - (float)y / (float)h : 0.5f;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void handle_command(int id) {
    PlySettings* s = ply_settings();
    switch (id) {
        case ID_FILE_OPEN: open_file_dialog(); return;
        case ID_FILE_CLOSE: ply_gl_clear(); break;
        case ID_FILE_SNAPSHOT: snapshot_dialog(); return;
        case ID_FILE_EXIT: PostMessageA(g.hwnd, WM_CLOSE, 0, 0); return;
        case ID_RECENT_CLEAR: g.recentCount = 0; recent_save(); recent_rebuild_menu(); return;

        case ID_MODE_POINTS: s->renderMode = PLY_MODE_POINTS; break;
        case ID_MODE_SPLATS: s->renderMode = PLY_MODE_SPLATS; break;
        case ID_PROJ_PERSP: s->ortho = 0; ply_cam_fit(); break;
        case ID_PROJ_ORTHO: s->ortho = 1; ply_cam_fit(); break;

        case ID_VIEW_FRONT: ply_cam_view(PLY_VIEW_FRONT); break;
        case ID_VIEW_BACK: ply_cam_view(PLY_VIEW_BACK); break;
        case ID_VIEW_LEFT: ply_cam_view(PLY_VIEW_LEFT); break;
        case ID_VIEW_RIGHT: ply_cam_view(PLY_VIEW_RIGHT); break;
        case ID_VIEW_TOP: ply_cam_view(PLY_VIEW_TOP); break;
        case ID_VIEW_BOTTOM: ply_cam_view(PLY_VIEW_BOTTOM); break;
        case ID_VIEW_ISO: ply_cam_view(PLY_VIEW_ISO); break;
        case ID_CAM_FIT: ply_cam_fit(); break;
        case ID_CAM_RESET: ply_cam_reset(); break;

        case ID_TOG_GRID: s->showGrid = !s->showGrid; break;
        case ID_TOG_AXES: s->showAxes = !s->showAxes; break;
        case ID_TOG_EDL: s->edl = !s->edl; break;
        case ID_TOG_SHADING: s->shading = !s->shading; break;

        case ID_SIZE_UP: s->pointSize = clampf(s->pointSize * 1.2f, 0.2f, 6.0f); break;
        case ID_SIZE_DOWN: s->pointSize = clampf(s->pointSize / 1.2f, 0.2f, 6.0f); break;
        case ID_SPLAT_UP: s->splatScale = clampf(s->splatScale * 1.2f, 0.2f, 3.0f); break;
        case ID_SPLAT_DOWN: s->splatScale = clampf(s->splatScale / 1.2f, 0.2f, 3.0f); break;
        case ID_CUTOFF_UP: s->opacityCutoff = clampf(s->opacityCutoff + 0.02f, 0.0f, 0.99f); break;
        case ID_CUTOFF_DOWN: s->opacityCutoff = clampf(s->opacityCutoff - 0.02f, 0.0f, 0.99f); break;
        case ID_EDL_UP: s->edlStrength = clampf(s->edlStrength + 0.1f, 0.0f, 2.0f); break;
        case ID_EDL_DOWN: s->edlStrength = clampf(s->edlStrength - 0.1f, 0.0f, 2.0f); break;
        case ID_FOV_UP: s->fov = clampf(s->fov + 5.0f, 15.0f, 90.0f); break;
        case ID_FOV_DOWN: s->fov = clampf(s->fov - 5.0f, 15.0f, 90.0f); break;

        case ID_COLOR_RGB: s->colorMode = PLY_COLOR_RGB; break;
        case ID_COLOR_HEIGHT: s->colorMode = PLY_COLOR_HEIGHT; break;
        case ID_COLOR_NORMAL: s->colorMode = PLY_COLOR_NORMAL; break;
        case ID_COLOR_UNIFORM: s->colorMode = PLY_COLOR_UNIFORM; break;
        case ID_BG_STUDIO: s->background = PLY_BG_STUDIO; break;
        case ID_BG_BLACK: s->background = PLY_BG_BLACK; break;
        case ID_BG_GRAY: s->background = PLY_BG_GRAY; break;
        case ID_BG_WHITE: s->background = PLY_BG_WHITE; break;
        case ID_UP_POSY: ply_cam_set_up_axis(PLY_UP_POS_Y); break;
        case ID_UP_POSZ: ply_cam_set_up_axis(PLY_UP_POS_Z); break;
        case ID_UP_NEGY: ply_cam_set_up_axis(PLY_UP_NEG_Y); break;
        case ID_UP_NEGZ: ply_cam_set_up_axis(PLY_UP_NEG_Z); break;

        case ID_HELP_KEYS:
            MessageBoxA(g.hwnd,
                "Mouse\n"
                "  Left drag - orbit\n"
                "  Right / middle drag, or Ctrl+Left - pan\n"
                "  Wheel - zoom at cursor\n"
                "  Double click - set pivot on a point\n\n"
                "Keyboard\n"
                "  R reset    F fit    G grid    E eye-dome lighting\n"
                "  P perspective / orthographic    M points / splats\n"
                "  1-7 front, back, left, right, top, bottom, iso\n"
                "  + / -  zoom      [ / ]  point and splat size\n"
                "  Arrow keys - orbit",
                "Keyboard & Mouse", MB_ICONINFORMATION);
            return;
        case ID_HELP_ABOUT:
            MessageBoxA(g.hwnd,
                APP_TITLE "\n\nPoint cloud and 3D Gaussian Splatting viewer.\n"
                "Windows build, shared renderer core with the macOS app.",
                "About " APP_TITLE, MB_ICONINFORMATION);
            return;
        default:
            if (id >= ID_RECENT_BASE && id < ID_RECENT_BASE + MAX_RECENT) {
                int i = id - ID_RECENT_BASE;
                if (i < g.recentCount) {
                    char path[PLY_MAX_PATH];
                    strncpy(path, g.recent[i], PLY_MAX_PATH - 1);
                    path[PLY_MAX_PATH - 1] = 0;
                    load_file(path);
                }
                return;
            }
            return;
    }
    settings_save();
    sync_menu();
    update_title();
    request_redraw();
}

static int handle_key(int vk) {
    PlySettings* s = ply_settings();
    switch (vk) {
        case 'R': ply_cam_reset(); break;
        case 'F': ply_cam_fit(); break;
        case 'G': s->showGrid = !s->showGrid; break;
        case 'E': s->edl = !s->edl; break;
        case 'P': s->ortho = !s->ortho; ply_cam_fit(); break;
        case 'M': s->renderMode = s->renderMode == PLY_MODE_POINTS ? PLY_MODE_SPLATS : PLY_MODE_POINTS; break;
        case '1': ply_cam_view(PLY_VIEW_FRONT); break;
        case '2': ply_cam_view(PLY_VIEW_BACK); break;
        case '3': ply_cam_view(PLY_VIEW_LEFT); break;
        case '4': ply_cam_view(PLY_VIEW_RIGHT); break;
        case '5': ply_cam_view(PLY_VIEW_TOP); break;
        case '6': ply_cam_view(PLY_VIEW_BOTTOM); break;
        case '7': ply_cam_view(PLY_VIEW_ISO); break;
        case VK_OEM_PLUS: case VK_ADD: ply_cam_zoom(0.85f, 0, 0, 0); break;
        case VK_OEM_MINUS: case VK_SUBTRACT: ply_cam_zoom(1.0f / 0.85f, 0, 0, 0); break;
        case VK_OEM_4:
            s->pointSize = clampf(s->pointSize / 1.2f, 0.2f, 6.0f);
            s->splatScale = clampf(s->splatScale / 1.2f, 0.2f, 3.0f);
            break;
        case VK_OEM_6:
            s->pointSize = clampf(s->pointSize * 1.2f, 0.2f, 6.0f);
            s->splatScale = clampf(s->splatScale * 1.2f, 0.2f, 3.0f);
            break;
        case VK_LEFT: ply_cam_orbit(-12, 0); break;
        case VK_RIGHT: ply_cam_orbit(12, 0); break;
        case VK_UP: ply_cam_orbit(0, 12); break;
        case VK_DOWN: ply_cam_orbit(0, -12); break;
        default: return 0;
    }
    settings_save();
    sync_menu();
    update_title();
    request_redraw();
    return 1;
}

/* ------------------------------------------------------------------ */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            return 0;

        case WM_SIZE:
            request_redraw();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            draw_frame();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_COMMAND:
            if (HIWORD(wp) == 0 || HIWORD(wp) == 1) handle_command(LOWORD(wp));
            return 0;

        case WM_KEYDOWN:
            if (handle_key((int)wp)) return 0;
            break;

        case WM_LBUTTONDBLCLK: {
            float nx, ny;
            normalized_point(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &nx, &ny);
            if (ply_cam_pick_pivot(nx, ny)) request_redraw();
            return 0;
        }

        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) g.panning = 1;
            else g.orbiting = 1;
            g.lastMouse.x = GET_X_LPARAM(lp);
            g.lastMouse.y = GET_Y_LPARAM(lp);
            SetCapture(hwnd);
            return 0;

        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetFocus(hwnd);
            g.panning = 1;
            g.lastMouse.x = GET_X_LPARAM(lp);
            g.lastMouse.y = GET_Y_LPARAM(lp);
            SetCapture(hwnd);
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            g.orbiting = 0;
            g.panning = 0;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE: {
            if (!g.orbiting && !g.panning) return 0;
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            float dx = (float)(x - g.lastMouse.x);
            float dy = -(float)(y - g.lastMouse.y);
            if (g.orbiting) ply_cam_orbit(dx, dy);
            else ply_cam_pan(dx, dy);
            g.lastMouse.x = x;
            g.lastMouse.y = y;
            request_redraw();
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (delta == 0) return 0;
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &p);
            float nx, ny;
            normalized_point(p.x, p.y, &nx, &ny);
            float factor = powf(1.1f, (float)(-delta) / 120.0f);
            ply_cam_zoom(factor, nx, ny, 1);
            request_redraw();
            return 0;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            char path[PLY_MAX_PATH] = "";
            if (DragQueryFileA(drop, 0, path, sizeof path)) {
                const char* dot = strrchr(path, '.');
                if (dot && _stricmp(dot, ".ply") == 0) load_file(path);
                else MessageBoxA(hwnd, "Drop a file with the .ply extension.", "Not a PLY file", MB_ICONWARNING);
            }
            DragFinish(drop);
            return 0;
        }

        case WM_PLY_LOADED: {
            g.loading = 0;
            PlyCloud* cloud = (PlyCloud*)lp;
            if (!cloud) {
                char msg[640];
                _snprintf(msg, sizeof msg - 1, "Could not open %s\n\n%s", g.loadName, g.loadErr);
                msg[sizeof msg - 1] = 0;
                update_title();
                MessageBoxA(hwnd, msg, APP_TITLE, MB_ICONWARNING);
                if (g.cliSnapshot[0]) PostQuitMessage(1);
                return 0;
            }
            wglMakeCurrent(g.hdc, g.hrc);
            ply_gl_set_cloud(cloud);
            apply_cli_options();
            {
                const PlyInfo* info = ply_info();
                if (info && info->path[0]) recent_add(info->path);
            }
            sync_menu();
            update_title();
            if (g.cliSnapshot[0]) {
                strncpy(g.pendingSnapshot, g.cliSnapshot, PLY_MAX_PATH - 1);
                g.pendingSnapshot[PLY_MAX_PATH - 1] = 0;
                g.snapshotQuits = 1;
            }
            request_redraw();
            return 0;
        }

        case WM_TIMER:
            if (g.loading) update_title();
            return 0;

        case WM_CLOSE:
            settings_save();
            if (g.ready) {
                wglMakeCurrent(g.hdc, g.hrc);
                ply_gl_shutdown();
                g.ready = 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (hwnd == g.hwnd) {
                g.ready = 0;
                PostQuitMessage(0);
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void parse_args(void) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return;
    for (int i = 1; i < argc; i++) {
        char a[PLY_MAX_PATH] = "";
        WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, a, sizeof a - 1, NULL, NULL);
        if (strcmp(a, "--snapshot") == 0 && i + 1 < argc) {
            WideCharToMultiByte(CP_ACP, 0, wargv[++i], -1, g.cliSnapshot, PLY_MAX_PATH - 1, NULL, NULL);
        } else if (strcmp(a, "--mode") == 0 && i + 1 < argc) {
            WideCharToMultiByte(CP_ACP, 0, wargv[++i], -1, g.cliMode, sizeof g.cliMode - 1, NULL, NULL);
        } else if (strcmp(a, "--view") == 0 && i + 1 < argc) {
            WideCharToMultiByte(CP_ACP, 0, wargv[++i], -1, g.cliView, sizeof g.cliView - 1, NULL, NULL);
        } else if (a[0] != '-' && !g.cliFile[0]) {
            strncpy(g.cliFile, a, PLY_MAX_PATH - 1);
            g.cliFile[PLY_MAX_PATH - 1] = 0;
        }
    }
    LocalFree(wargv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrev; (void)lpCmdLine;

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WND_CLASS;
    wc.hIcon = LoadIconA(hInstance, "APPICON");
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Could not register the window class.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    ply_settings_default(ply_settings());
    settings_load();
    recent_load();
    parse_args();

    g.hwnd = CreateWindowExA(WS_EX_ACCEPTFILES, WND_CLASS, APP_TITLE,
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             1280, 800, NULL, NULL, hInstance, NULL);
    if (!g.hwnd) {
        MessageBoxA(NULL, "Could not create the main window.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    build_menu(g.hwnd);

    if (!create_gl_context(g.hwnd)) {
        MessageBoxA(g.hwnd,
            "Could not create an OpenGL 3.3 core context.\n"
            "Update your graphics driver and try again.",
            APP_TITLE, MB_ICONERROR);
        return 1;
    }
    if (!ply_gl_init()) {
        MessageBoxA(g.hwnd, "Could not initialise the renderer.", APP_TITLE, MB_ICONERROR);
        return 1;
    }
    g.ready = 1;

    DragAcceptFiles(g.hwnd, TRUE);
    ShowWindow(g.hwnd, nCmdShow ? nCmdShow : SW_SHOW);
    SetFocus(g.hwnd);
    sync_menu();
    update_title();
    SetTimer(g.hwnd, 1, 100, NULL);

    if (g.cliFile[0]) load_file(g.cliFile);

    MSG msg;
    int running = 1;
    while (running) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;
        if (g.dirty || ply_needs_redraw()) draw_frame();
        else Sleep(4);
    }

    if (g.ready) {
        wglMakeCurrent(g.hdc, g.hrc);
        ply_gl_shutdown();
        g.ready = 0;
    }
    wglMakeCurrent(NULL, NULL);
    if (g.hrc) wglDeleteContext(g.hrc);
    if (g.hdc) ReleaseDC(g.hwnd, g.hdc);
    return 0;
}
