/*
 * PLY Point Cloud Viewer for Windows
 * OpenGL 3.3 shader-based viewer with Open/Close buttons and app icon.
 * Build: windres ply_viewer.rc -O coff -o ply_viewer_res.o
 *        x86_64-w64-mingw32-gcc -o PLY_Viewer.exe ply_viewer.c ply_viewer_res.o -lopengl32 -lglu32 -lgdi32 -mwindows
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <commdlg.h>

#define MAX_POINTS 10000000
#define TOOLBAR_HEIGHT 40
#define IDC_OPEN_BTN  1001
#define IDC_CLOSE_BTN 1002
#ifndef IDI_ICON1
#define IDI_ICON1 101
#endif

// Point structure
typedef struct {
    float x, y, z;
    unsigned char r, g, b;
} Point;

// Global variables
static Point* points = NULL;
static int numPoints = 0;
static float rotX = 0.0f, rotY = 0.0f;
static float zoom = 1.0f;
static float panX = 0.0f, panY = 0.0f;
static int lastMouseX, lastMouseY;
static int mouseLeftDown = 0, mouseRightDown = 0;
static HGLRC hRC = NULL;
static HDC hDC = NULL;
static HWND hWndMain = NULL;   /* main window (toolbar + title) */
static HWND hWndGL = NULL;     /* OpenGL child window */
static char currentFile[MAX_PATH] = "No file loaded";
static float centerX = 0, centerY = 0, centerZ = 0;
static float scale = 1.0f;

/* OpenGL 3.3 / shader helpers */
static unsigned int progPoints = 0, progLines = 0;
static unsigned int vboPoints = 0, vboLines = 0;
static unsigned int vaoPoints = 0, vaoLines = 0;
static int uMVP_points = -1, uMVP_lines = -1;
static int uPointSize = -1;
static int uDepthScale = -1;
static float avgPointSpacing = 1.0f;
static int numLineVerts = 0;

/* Function pointers for OpenGL 3.x (loaded at runtime) */
typedef void (*PFNGLGENBUFFERSPROC)(int, unsigned int*);
typedef void (*PFNGLBINDBUFFERPROC)(unsigned int, unsigned int);
typedef void (*PFNGLBUFFERDATAPROC)(unsigned int, ptrdiff_t, const void*, unsigned int);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(int, unsigned int*);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(unsigned int);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(unsigned int, int, unsigned int, int, int, const void*);
typedef unsigned int (*PFNGLCREATESHADERPROC)(unsigned int);
typedef void (*PFNGLSHADERSOURCEPROC)(unsigned int, int, const char* const*, const int*);
typedef void (*PFNGLCOMPILESHADERPROC)(unsigned int);
typedef void (*PFNGLGETSHADERIVPROC)(unsigned int, unsigned int, int*);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(unsigned int, int, int*, char*);
typedef unsigned int (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(unsigned int, unsigned int);
typedef void (*PFNGLLINKPROGRAMPROC)(unsigned int);
typedef void (*PFNGLGETPROGRAMIVPROC)(unsigned int, unsigned int, int*);
typedef void (*PFNGLUSEPROGRAMPROC)(unsigned int);
typedef int (*PFNGLGETUNIFORMLOCATIONPROC)(unsigned int, const char*);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(int, int, int, const float*);
typedef void (*PFNGLUNIFORM1FPROC)(int, float);
static PFNGLGENBUFFERSPROC glGenBuffers;
static PFNGLBINDBUFFERPROC glBindBuffer;
static PFNGLBUFFERDATAPROC glBufferData;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
static PFNGLCREATESHADERPROC glCreateShader;
static PFNGLSHADERSOURCEPROC glShaderSource;
static PFNGLCOMPILESHADERPROC glCompileShader;
static PFNGLGETSHADERIVPROC glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC glCreateProgram;
static PFNGLATTACHSHADERPROC glAttachShader;
static PFNGLLINKPROGRAMPROC glLinkProgram;
static PFNGLGETPROGRAMIVPROC glGetProgramiv;
static PFNGLUSEPROGRAMPROC glUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
static PFNGLUNIFORM1FPROC glUniform1f;

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001

// Function prototypes
LRESULT CALLBACK WndProcMain(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProcGL(HWND, UINT, WPARAM, LPARAM);
void EnableOpenGL(HWND hwnd, HDC* hDC, HGLRC* hRC);
void DisableOpenGL(HWND hwnd, HDC hDC, HGLRC hRC);
void DrawScene(void);
int LoadPLY(const char* filename);
void ClearPLY(void);
void OpenFileDialog(HWND hwnd);
void NormalizePointCloud(void);
void ComputeAverageSpacing(void);
void PrefilterColors(void);
void UpdateTitle(HWND hwnd);
int InitShaders(void);
void UploadPointVBO(void);
void BuildLineVBO(void);

typedef enum {
    PROP_UNKNOWN = 0,
    PROP_FLOAT,
    PROP_DOUBLE,
    PROP_INT,
    PROP_UINT,
    PROP_UCHAR,
} PropType;

static const char* vertex_shader_points =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "uniform float uPointSize;\n"
    "uniform float uDepthScale;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "  gl_PointSize = uPointSize * uDepthScale;\n"
    "  vColor = aColor;\n"
    "}\n";

static const char* fragment_shader_points =
    "#version 330 core\n"
    "in vec3 vColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "  vec2 coord = gl_PointCoord - vec2(0.5);\n"
    "  float r2 = dot(coord, coord) * 4.0;\n"
    "  if(r2 > 1.0) discard;\n"
    "  float w = 1.0 - r2 * r2;\n"
    "  w = sqrt(w);\n"
    "  w = clamp(w, 0.05, 1.0);\n"
    "  FragColor = vec4(vColor, w * 0.5);\n"
    "}\n";

static const char* vertex_shader_lines =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "  vColor = aColor;\n"
    "}\n";

static const char* fragment_shader_lines =
    "#version 330 core\n"
    "in vec3 vColor;\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(vColor, 1.0); }\n";

static void loadGLFuncs(void) {
    glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
    glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
}

static unsigned int compile_shader(unsigned int type, const char* src) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &src, NULL);
    glCompileShader(id);
    int ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(id, sizeof(log), NULL, log);
        MessageBoxA(NULL, log, "Shader compile error", MB_OK | MB_ICONERROR);
        return 0;
    }
    return id;
}

static unsigned int create_program(const char* vs_src, const char* fs_src) {
    unsigned int vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramiv(prog, 0x8B84, &ok); /* GL_INFO_LOG_LENGTH */
        if (ok > 0 && ok < (int)sizeof(log)) {
            glGetProgramiv(prog, 0x8B84, &ok);
            MessageBoxA(NULL, "Program link failed", "Shader error", MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    return prog;
}

int InitShaders(void) {
    loadGLFuncs();
    if (!glCreateShader || !glGenBuffers || !glGenVertexArrays) {
        MessageBox(NULL, "OpenGL 3.3 not supported. Missing function pointers.", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    progPoints = create_program(vertex_shader_points, fragment_shader_points);
    progLines = create_program(vertex_shader_lines, fragment_shader_lines);
    if (!progPoints || !progLines) return 0;
    uMVP_points = glGetUniformLocation(progPoints, "uMVP");
    uMVP_lines = glGetUniformLocation(progLines, "uMVP");
    uPointSize = glGetUniformLocation(progPoints, "uPointSize");
    uDepthScale = glGetUniformLocation(progPoints, "uDepthScale");

    glGenVertexArrays(1, &vaoPoints);
    glGenVertexArrays(1, &vaoLines);
    glGenBuffers(1, &vboPoints);
    glGenBuffers(1, &vboLines);
    BuildLineVBO();
    return 1;
}

void BuildLineVBO(void) {
    /* Axes + grid: interleaved pos (3 float) + color (3 float) */
    float axisLen = 1.0f;
    typedef struct { float x,y,z; float r,g,b; } LineVert;
    LineVert lineVerts[6 + 44*2]; /* 3 axes * 2 + grid lines */
    int n = 0;
    lineVerts[n].x=0; lineVerts[n].y=0; lineVerts[n].z=0; lineVerts[n].r=1; lineVerts[n].g=0.3f; lineVerts[n].b=0.3f; n++;
    lineVerts[n].x=axisLen; lineVerts[n].y=0; lineVerts[n].z=0; lineVerts[n].r=1; lineVerts[n].g=0.3f; lineVerts[n].b=0.3f; n++;
    lineVerts[n].x=0; lineVerts[n].y=0; lineVerts[n].z=0; lineVerts[n].r=0.3f; lineVerts[n].g=1; lineVerts[n].b=0.3f; n++;
    lineVerts[n].x=0; lineVerts[n].y=axisLen; lineVerts[n].z=0; lineVerts[n].r=0.3f; lineVerts[n].g=1; lineVerts[n].b=0.3f; n++;
    lineVerts[n].x=0; lineVerts[n].y=0; lineVerts[n].z=0; lineVerts[n].r=0.3f; lineVerts[n].g=0.3f; lineVerts[n].b=1; n++;
    lineVerts[n].x=0; lineVerts[n].y=0; lineVerts[n].z=axisLen; lineVerts[n].r=0.3f; lineVerts[n].g=0.3f; lineVerts[n].b=1; n++;
    for (float gi = -5.0f; gi <= 5.0f; gi += 1.0f) {
        lineVerts[n].x=gi; lineVerts[n].y=0; lineVerts[n].z=-5; lineVerts[n].r=0.25f; lineVerts[n].g=0.25f; lineVerts[n].b=0.35f; n++;
        lineVerts[n].x=gi; lineVerts[n].y=0; lineVerts[n].z= 5; lineVerts[n].r=0.25f; lineVerts[n].g=0.25f; lineVerts[n].b=0.35f; n++;
        lineVerts[n].x=-5; lineVerts[n].y=0; lineVerts[n].z=gi; lineVerts[n].r=0.25f; lineVerts[n].g=0.25f; lineVerts[n].b=0.35f; n++;
        lineVerts[n].x= 5; lineVerts[n].y=0; lineVerts[n].z=gi; lineVerts[n].r=0.25f; lineVerts[n].g=0.25f; lineVerts[n].b=0.35f; n++;
    }
    numLineVerts = n;
    glBindVertexArray(vaoLines);
    glBindBuffer(GL_ARRAY_BUFFER, vboLines);
    glBufferData(GL_ARRAY_BUFFER, (ptrdiff_t)(n * sizeof(LineVert)), lineVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

void UploadPointVBO(void) {
    if (numPoints <= 0) return;
    /* Interleaved: x,y,z (float), r,g,b (float 0..1) */
    size_t stride = 6 * sizeof(float);
    size_t size = (size_t)numPoints * stride;
    float* buf = (float*)malloc(size);
    if (!buf) return;
    for (int i = 0; i < numPoints; i++) {
        buf[i*6+0] = points[i].x;
        buf[i*6+1] = points[i].y;
        buf[i*6+2] = points[i].z;
        buf[i*6+3] = points[i].r / 255.0f;
        buf[i*6+4] = points[i].g / 255.0f;
        buf[i*6+5] = points[i].b / 255.0f;
    }
    glBindVertexArray(vaoPoints);
    glBindBuffer(GL_ARRAY_BUFFER, vboPoints);
    glBufferData(GL_ARRAY_BUFFER, (ptrdiff_t)size, buf, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (int)stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (int)stride, (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    free(buf);
}

/* Column-major 4x4 matrix for MVP */
static void mat4_perspective(float* out, float fov_deg, float aspect, float znear, float zfar) {
    float f = 1.0f / (float)tan(fov_deg * 3.14159265f / 180.0f * 0.5f);
    float nf = 1.0f / (znear - zfar);
    memset(out, 0, 16 * sizeof(float));
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (zfar + znear) * nf;
    out[11] = -1.0f;
    out[14] = 2.0f * zfar * znear * nf;
}

static void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0]=m[5]=m[10]=m[15]=1.0f;
}

static void mat4_translate(float* m, float x, float y, float z) {
    m[12] += x; m[13] += y; m[14] += z;
}

static void mat4_rotate_x(float* m, float deg) {
    float r = deg * 3.14159265f / 180.0f;
    float c = (float)cos(r), s = (float)sin(r);
    float m1 = m[1], m2 = m[2], m5 = m[5], m6 = m[6], m9 = m[9], m10 = m[10];
    m[1] = m1*c - m2*s; m[2] = m1*s + m2*c;
    m[5] = m5*c - m6*s; m[6] = m5*s + m6*c;
    m[9] = m9*c - m10*s; m[10] = m9*s + m10*c;
}

static void mat4_rotate_y(float* m, float deg) {
    float r = deg * 3.14159265f / 180.0f;
    float c = (float)cos(r), s = (float)sin(r);
    float m0 = m[0], m2 = m[2], m4 = m[4], m6 = m[6], m8 = m[8], m10 = m[10];
    m[0] = m0*c + m2*s; m[2] = -m0*s + m2*c;
    m[4] = m4*c + m6*s; m[6] = -m4*s + m6*c;
    m[8] = m8*c + m10*s; m[10] = -m8*s + m10*c;
}

static void mat4_scale(float* m, float sx, float sy, float sz) {
    m[0] *= sx; m[5] *= sy; m[10] *= sz;
}

static void mat4_mul(float* out, const float* a, const float* b) {
    float t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            t[i + j*4] = a[i + 0*4] * b[0 + j*4]
                      + a[i + 1*4] * b[1 + j*4]
                      + a[i + 2*4] * b[2 + j*4]
                      + a[i + 3*4] * b[3 + j*4];
        }
    memcpy(out, t, 16 * sizeof(float));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wcex;
    MSG msg;
    BOOL bQuit = FALSE;

    points = (Point*)malloc(MAX_POINTS * sizeof(Point));
    if (!points) {
        MessageBox(NULL, "Failed to allocate memory!", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Load app icon from resource if linked with .res */
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);

    /* Main window class (frame with buttons) */
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = 0;
    wcex.lpfnWndProc = WndProcMain;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = hIcon;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "PLYViewerMain";
    wcex.hIconSm = hIcon;

    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, "Failed to register main window class!", "Error", MB_OK | MB_ICONERROR);
        free(points);
        return 1;
    }

    /* OpenGL child window class */
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = WndProcGL;
    wcex.lpszClassName = "PLYViewerGL";
    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, "Failed to register GL window class!", "Error", MB_OK | MB_ICONERROR);
        free(points);
        return 1;
    }

    hWndMain = CreateWindowEx(
        0, "PLYViewerMain", "PLY Point Cloud Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1600, 1000,
        NULL, NULL, hInstance, NULL);

    if (!hWndMain) {
        MessageBox(NULL, "Failed to create window!", "Error", MB_OK | MB_ICONERROR);
        free(points);
        return 1;
    }

    /* Toolbar buttons */
    CreateWindowEx(0, "BUTTON", "Open PLY",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        12, 8, 100, 24, hWndMain, (HMENU)(INT_PTR)IDC_OPEN_BTN, hInstance, NULL);
    CreateWindowEx(0, "BUTTON", "Close PLY",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 8, 100, 24, hWndMain, (HMENU)(INT_PTR)IDC_CLOSE_BTN, hInstance, NULL);

    /* OpenGL child window (below toolbar) */
    hWndGL = CreateWindowEx(0, "PLYViewerGL", "",
        WS_CHILD | WS_VISIBLE,
        0, TOOLBAR_HEIGHT, 1600, 1000 - TOOLBAR_HEIGHT,
        hWndMain, NULL, hInstance, NULL);

    if (!hWndGL) {
        MessageBox(NULL, "Failed to create GL window!", "Error", MB_OK | MB_ICONERROR);
        DestroyWindow(hWndMain);
        free(points);
        return 1;
    }

    DragAcceptFiles(hWndMain, TRUE);
    ShowWindow(hWndMain, nCmdShow);

    EnableOpenGL(hWndGL, &hDC, &hRC);
    if (!InitShaders()) {
        DisableOpenGL(hWndGL, hDC, hRC);
        DestroyWindow(hWndMain);
        free(points);
        return 1;
    }

    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        char* filename = lpCmdLine;
        if (filename[0] == '"') {
            filename++;
            char* end = strchr(filename, '"');
            if (end) *end = '\0';
        }
        if (LoadPLY(filename)) {
            UploadPointVBO();
            UpdateTitle(hWndMain);
        }
    }

    while (!bQuit) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) bQuit = TRUE;
            else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else {
            DrawScene();
            SwapBuffers(hDC);
            Sleep(16);
        }
    }

    DisableOpenGL(hWndGL, hDC, hRC);
    DestroyWindow(hWndMain);
    free(points);
    return (int)msg.wParam;
}

void UpdateTitle(HWND hwnd) {
    char title[256];
    if (numPoints > 0)
        sprintf(title, "PLY Viewer - %s (%d points)", currentFile, numPoints);
    else
        sprintf(title, "PLY Point Cloud Viewer");
    SetWindowText(hwnd, title);
}

void ClearPLY(void) {
    numPoints = 0;
    strcpy(currentFile, "No file loaded");
    rotX = 0.0f; rotY = 0.0f;
    zoom = 1.0f; panX = panY = 0.0f;
    centerX = centerY = centerZ = 0.0f;
    scale = 1.0f;
    UpdateTitle(hWndMain);
}

LRESULT CALLBACK WndProcMain(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (h > TOOLBAR_HEIGHT && hWndGL)
                MoveWindow(hWndGL, 0, TOOLBAR_HEIGHT, w, h - TOOLBAR_HEIGHT, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_OPEN_BTN) {
                OpenFileDialog(hwnd);
                if (numPoints > 0) UploadPointVBO();
                return 0;
            }
            if (LOWORD(wParam) == IDC_CLOSE_BTN) {
                ClearPLY();
                return 0;
            }
            return 0;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char filename[MAX_PATH];
            DragQueryFile(hDrop, 0, filename, MAX_PATH);
            DragFinish(hDrop);
            char* ext = strrchr(filename, '.');
            if (ext && (_stricmp(ext, ".ply") == 0)) {
                if (LoadPLY(filename)) {
                    UploadPointVBO();
                    UpdateTitle(hwnd);
                }
            } else {
                MessageBox(hwnd, "Please drop a .ply file", "Invalid File", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK WndProcGL(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (h == 0) h = 1;
            if (hRC) {
                glViewport(0, 0, w, h);
            }
            return 0;
        }
        case WM_KEYDOWN:
            switch (wParam) {
                case 'O':
                    OpenFileDialog(hWndMain);
                    if (numPoints > 0) UploadPointVBO();
                    UpdateTitle(hWndMain);
                    break;
                case 'R':
                    rotX = 0.0f; rotY = 0.0f;
                    zoom = 1.0f; panX = panY = 0.0f;
                    break;
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
                case VK_ADD:
                case 187:
                    zoom *= 0.9f;
                    break;
                case VK_SUBTRACT:
                case 189:
                    zoom *= 1.1f;
                    break;
            }
            return 0;
        case WM_MOUSEWHEEL:
            zoom *= (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 0.9f : 1.1f;
            if (zoom < 0.1f) zoom = 0.1f;
            if (zoom > 100.0f) zoom = 100.0f;
            return 0;
        case WM_LBUTTONDOWN:
            mouseLeftDown = 1;
            lastMouseX = LOWORD(lParam);
            lastMouseY = HIWORD(lParam);
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            mouseLeftDown = 0;
            ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
            mouseRightDown = 1;
            lastMouseX = LOWORD(lParam);
            lastMouseY = HIWORD(lParam);
            SetCapture(hwnd);
            return 0;
        case WM_RBUTTONUP:
            mouseRightDown = 0;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (mouseLeftDown) {
                int x = LOWORD(lParam), y = HIWORD(lParam);
                rotY += (x - lastMouseX) * 0.5f;
                rotX += (y - lastMouseY) * 0.5f;
                lastMouseX = x; lastMouseY = y;
            }
            if (mouseRightDown) {
                int x = LOWORD(lParam), y = HIWORD(lParam);
                panX += (x - lastMouseX) * 0.01f * zoom;
                panY -= (y - lastMouseY) * 0.01f * zoom;
                lastMouseX = x; lastMouseY = y;
            }
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

/* WGL context creation for OpenGL 3.3 */
typedef HGLRC (WINAPI * PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
static PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB;

void EnableOpenGL(HWND hwnd, HDC* hDC, HGLRC* hRC) {
    PIXELFORMATDESCRIPTOR pfd;
    int iFormat;

    *hDC = GetDC(hwnd);
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    iFormat = ChoosePixelFormat(*hDC, &pfd);
    SetPixelFormat(*hDC, iFormat, &pfd);

    HGLRC tempRC = wglCreateContext(*hDC);
    wglMakeCurrent(*hDC, tempRC);
    wglCreateContextAttribsARB = (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");
    wglMakeCurrent(*hDC, NULL);
    wglDeleteContext(tempRC);

    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            0x2091, 3, /* WGL_CONTEXT_MAJOR_VERSION_ARB */
            0x2092, 3, /* WGL_CONTEXT_MINOR_VERSION_ARB */
            0x2094, 0x00000001, /* WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB */
            0
        };
        *hRC = wglCreateContextAttribsARB(*hDC, NULL, attribs);
    }
    if (!*hRC)
        *hRC = wglCreateContext(*hDC);
    wglMakeCurrent(*hDC, *hRC);

    glEnable(GL_DEPTH_TEST);    glEnable(GL_PROGRAM_POINT_SIZE);    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
}

void DisableOpenGL(HWND hwnd, HDC hDC, HGLRC hRC) {
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRC);
    ReleaseDC(hwnd, hDC);
}

void DrawScene(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int w = 1600, h = 1000 - TOOLBAR_HEIGHT;
    RECT r;
    if (hWndGL && GetClientRect(hWndGL, &r)) {
        w = r.right - r.left;
        h = r.bottom - r.top;
    }
    if (h == 0) h = 1;
    float aspect = (float)w / (float)h;

    float proj[16], view[16], model[16], mvp[16];
    mat4_identity(proj);
    mat4_perspective(proj, 45.0f, aspect, 0.1f, 10000.0f);

    float camDist = (numPoints > 0) ? (zoom * scale * 1.5f) : (zoom * 4.0f);
    mat4_identity(view);
    mat4_translate(view, panX, panY, -camDist);
    mat4_rotate_x(view, rotX);
    mat4_rotate_y(view, rotY);
    mat4_translate(view, -centerX, -centerY, -centerZ);

    /* Draw axes (scaled when a cloud is loaded) */
    mat4_identity(model);
    float axisScale = (numPoints > 0) ? (scale * 0.5f) : 1.0f;
    mat4_scale(model, axisScale, axisScale, axisScale);
    mat4_mul(mvp, proj, view);
    mat4_mul(mvp, mvp, model);
    glUseProgram(progLines);
    glUniformMatrix4fv(uMVP_lines, 1, GL_FALSE, mvp);
    glBindVertexArray(vaoLines);
    glDrawArrays(GL_LINES, 0, 6);

    /* Draw grid only when no file loaded */
    if (numPoints == 0) {
        mat4_identity(model);
        mat4_mul(mvp, proj, view);
        mat4_mul(mvp, mvp, model);
        glUniformMatrix4fv(uMVP_lines, 1, GL_FALSE, mvp);
        glDrawArrays(GL_LINES, 6, numLineVerts - 6);
    }

    /* Draw point cloud with shader */
    if (numPoints > 0) {
        mat4_identity(model);
        mat4_mul(mvp, proj, view);
        mat4_mul(mvp, mvp, model);
        glUseProgram(progPoints);
        glUniformMatrix4fv(uMVP_points, 1, GL_FALSE, mvp);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        float baseSize = (avgPointSpacing > 1e-6f) ? (avgPointSpacing * 500.0f) : 4.0f;
        if (baseSize < 4.0f) baseSize = 4.0f;
        if (baseSize > 8.0f) baseSize = 8.0f;
        glUniform1f(uPointSize, baseSize);
        float halfFovTan = tanf(45.0f * 3.14159265f / 180.0f * 0.5f);
        float depthScale = (halfFovTan * (float)h) / (camDist * 2.0f);
        if (depthScale < 0.01f) depthScale = 0.01f;
        if (depthScale > 1.0f) depthScale = 1.0f;
        glUniform1f(uDepthScale, depthScale);
        glBindVertexArray(vaoPoints);
        glDrawArrays(GL_POINTS, 0, numPoints);
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0);
}

void NormalizePointCloud(void) {
    if (numPoints == 0) return;
    float minX = points[0].x, maxX = points[0].x;
    float minY = points[0].y, maxY = points[0].y;
    float minZ = points[0].z, maxZ = points[0].z;
    for (int i = 1; i < numPoints; i++) {
        if (points[i].x < minX) minX = points[i].x;
        if (points[i].x > maxX) maxX = points[i].x;
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
        if (points[i].z < minZ) minZ = points[i].z;
        if (points[i].z > maxZ) maxZ = points[i].z;
    }
    centerX = (minX + maxX) / 2.0f;
    centerY = (minY + maxY) / 2.0f;
    centerZ = (minZ + maxZ) / 2.0f;
    float sizeX = maxX - minX, sizeY = maxY - minY, sizeZ = maxZ - minZ;
    scale = sizeX;
    if (sizeY > scale) scale = sizeY;
    if (sizeZ > scale) scale = sizeZ;
    if (scale < 0.001f) scale = 1.0f;
}

void ComputeAverageSpacing(void) {
    if (numPoints < 2) { avgPointSpacing = 1.0f; return; }
    int samples = 64;
    if (numPoints < samples) samples = numPoints;
    float step = (float)numPoints / (float)samples;
    float totalDist = 0.0f;
    int counted = 0;
    for (int s = 0; s < samples; s++) {
        int i = (int)(s * step);
        if (i >= numPoints) i = numPoints - 1;
        float bestDist = 1e30f;
        for (int t = 0; t < samples; t++) {
            if (t == s) continue;
            int j = (int)(t * step);
            if (j >= numPoints) j = numPoints - 1;
            float dx = points[i].x - points[j].x;
            float dy = points[i].y - points[j].y;
            float dz = points[i].z - points[j].z;
            float d = dx*dx + dy*dy + dz*dz;
            if (d < bestDist) bestDist = d;
        }
        if (bestDist < 1e29f) {
            totalDist += sqrtf(bestDist);
            counted++;
        }
    }
    if (counted > 0) {
        avgPointSpacing = totalDist / (float)counted;
        if (avgPointSpacing < 1e-6f) avgPointSpacing = 1e-6f;
    } else {
        avgPointSpacing = 1.0f;
    }
}

void PrefilterColors(void) {
    if (numPoints < 8) return;
    float cellSize = avgPointSpacing * 2.0f;
    if (cellSize < 1e-5f) return;

    float minX = points[0].x, minY = points[0].y, minZ = points[0].z;
    float maxX = minX, maxY = minY, maxZ = minZ;
    for (int i = 1; i < numPoints; i++) {
        if (points[i].x < minX) minX = points[i].x;
        if (points[i].x > maxX) maxX = points[i].x;
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
        if (points[i].z < minZ) minZ = points[i].z;
        if (points[i].z > maxZ) maxZ = points[i].z;
    }
    int gx = (int)ceilf((maxX - minX) / cellSize) + 1;
    int gy = (int)ceilf((maxY - minY) / cellSize) + 1;
    int gz = (int)ceilf((maxZ - minZ) / cellSize) + 1;
    if (gx < 1) gx = 1;
    if (gy < 1) gy = 1;
    if (gz < 1) gz = 1;
    long long totalCells = (long long)gx * (long long)gy * (long long)gz;
    if (totalCells > 2000000LL) {
        cellSize = cbrtf((float)totalCells / 2000000.0f) * cellSize;
        gx = (int)ceilf((maxX - minX) / cellSize) + 1;
        gy = (int)ceilf((maxY - minY) / cellSize) + 1;
        gz = (int)ceilf((maxZ - minZ) / cellSize) + 1;
        if (gx < 1) gx = 1;
        if (gy < 1) gy = 1;
        if (gz < 1) gz = 1;
        totalCells = (long long)gx * (long long)gy * (long long)gz;
    }

    int* cellCounts = (int*)calloc((size_t)totalCells, sizeof(int));
    long long* cellSums = (long long*)calloc((size_t)totalCells * 3, sizeof(long long));
    if (!cellCounts || !cellSums) {
        free(cellCounts); free(cellSums);
        return;
    }

    for (int i = 0; i < numPoints; i++) {
        int cx = (int)((points[i].x - minX) / cellSize);
        int cy = (int)((points[i].y - minY) / cellSize);
        int cz = (int)((points[i].z - minZ) / cellSize);
        if (cx < 0) cx = 0; if (cx >= gx) cx = gx - 1;
        if (cy < 0) cy = 0; if (cy >= gy) cy = gy - 1;
        if (cz < 0) cz = 0; if (cz >= gz) cz = gz - 1;
        long long idx = ((long long)cx * gy + cy) * gz + cz;
        cellCounts[idx]++;
        cellSums[idx*3+0] += points[i].r;
        cellSums[idx*3+1] += points[i].g;
        cellSums[idx*3+2] += points[i].b;
    }

    for (int i = 0; i < numPoints; i++) {
        int cx = (int)((points[i].x - minX) / cellSize);
        int cy = (int)((points[i].y - minY) / cellSize);
        int cz = (int)((points[i].z - minZ) / cellSize);
        if (cx < 0) cx = 0; if (cx >= gx) cx = gx - 1;
        if (cy < 0) cy = 0; if (cy >= gy) cy = gy - 1;
        if (cz < 0) cz = 0; if (cz >= gz) cz = gz - 1;
        long long sumR = 0, sumG = 0, sumB = 0, sumN = 0;
        for (int dx = -1; dx <= 1; dx++) {
            int ncx = cx + dx;
            if (ncx < 0 || ncx >= gx) continue;
            for (int dy = -1; dy <= 1; dy++) {
                int ncy = cy + dy;
                if (ncy < 0 || ncy >= gy) continue;
                for (int dz = -1; dz <= 1; dz++) {
                    int ncz = cz + dz;
                    if (ncz < 0 || ncz >= gz) continue;
                    long long nidx = ((long long)ncx * gy + ncy) * gz + ncz;
                    int cnt = cellCounts[nidx];
                    if (cnt > 0) {
                        sumR += cellSums[nidx*3+0];
                        sumG += cellSums[nidx*3+1];
                        sumB += cellSums[nidx*3+2];
                        sumN += cnt;
                    }
                }
            }
        }
        if (sumN > 0) {
            float mr = (float)sumR / (float)sumN;
            float mg = (float)sumG / (float)sumN;
            float mb = (float)sumB / (float)sumN;
            float mixK = 0.30f;
            float fr = (float)points[i].r;
            float fg = (float)points[i].g;
            float fb = (float)points[i].b;
            int nr = (int)(fr * (1.0f - mixK) + mr * mixK + 0.5f);
            int ng = (int)(fg * (1.0f - mixK) + mg * mixK + 0.5f);
            int nb = (int)(fb * (1.0f - mixK) + mb * mixK + 0.5f);
            if (nr < 0) nr = 0; if (nr > 255) nr = 255;
            if (ng < 0) ng = 0; if (ng > 255) ng = 255;
            if (nb < 0) nb = 0; if (nb > 255) nb = 255;
            points[i].r = (unsigned char)nr;
            points[i].g = (unsigned char)ng;
            points[i].b = (unsigned char)nb;
        }
    }

    free(cellCounts);
    free(cellSums);
}

static float sigmoid(float x) {
    return 1.0f / (1.0f + (float)exp(-x));
}

static void ConvertFloatColor(float fr, float fg, float fb, int* r, int* g, int* b) {
    fr = sigmoid(fr);
    fg = sigmoid(fg);
    fb = sigmoid(fb);
    *r = (int)(fr * 255.0f + 0.5f);
    *g = (int)(fg * 255.0f + 0.5f);
    *b = (int)(fb * 255.0f + 0.5f);
    if (*r < 0) *r = 0;
    if (*g < 0) *g = 0;
    if (*b < 0) *b = 0;
    if (*r > 255) *r = 255;
    if (*g > 255) *g = 255;
    if (*b > 255) *b = 255;
}

int LoadPLY(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        MessageBox(NULL, "Failed to open file!", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    char line[1024];
    int vertexCount = 0;
    int isBinary = 0;
    int isBinaryLE = 1;
    int propCount = 0;
    int readingVertex = 0;
    char propNames[64][32] = {{0}};
    PropType propTypes[64] = {0};

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "element vertex", 14) == 0) {
            sscanf(line, "element vertex %d", &vertexCount);
            readingVertex = 1;
        } else if (strncmp(line, "element ", 8) == 0) {
            readingVertex = 0;
        } else if (strncmp(line, "property ", 9) == 0 && readingVertex) {
            char typeName[32] = {0};
            char name[32] = {0};
            if (sscanf(line, "property %31s %31s", typeName, name) >= 2) {
                PropType type = PROP_UNKNOWN;
                if (strcmp(typeName, "float") == 0 || strcmp(typeName, "float32") == 0 || strcmp(typeName, "float32_t") == 0) type = PROP_FLOAT;
                else if (strcmp(typeName, "double") == 0 || strcmp(typeName, "float64") == 0 || strcmp(typeName, "double64") == 0) type = PROP_DOUBLE;
                else if (strcmp(typeName, "int") == 0 || strcmp(typeName, "int32") == 0 || strcmp(typeName, "int32_t") == 0) type = PROP_INT;
                else if (strcmp(typeName, "uint") == 0 || strcmp(typeName, "uint32") == 0 || strcmp(typeName, "uint32_t") == 0) type = PROP_UINT;
                else if (strcmp(typeName, "uchar") == 0 || strcmp(typeName, "uint8") == 0 || strcmp(typeName, "uint8_t") == 0 || strcmp(typeName, "char") == 0 || strcmp(typeName, "int8") == 0 || strcmp(typeName, "int8_t") == 0) type = PROP_UCHAR;
                else if (strcmp(typeName, "short") == 0 || strcmp(typeName, "int16") == 0 || strcmp(typeName, "int16_t") == 0 || strcmp(typeName, "ushort") == 0 || strcmp(typeName, "uint16") == 0 || strcmp(typeName, "uint16_t") == 0) type = PROP_INT;
                if (propCount < 64) {
                    strncpy(propNames[propCount], name, sizeof(propNames[propCount]) - 1);
                    propTypes[propCount] = type;
                    propCount++;
                }
            }
        } else if (strncmp(line, "format binary", 13) == 0) {
            isBinary = 1;
            if (strstr(line, "big_endian")) isBinaryLE = 0;
        } else if (strncmp(line, "end_header", 10) == 0) {
            break;
        }
    }

    if (vertexCount > MAX_POINTS) vertexCount = MAX_POINTS;
    numPoints = 0;

    if (isBinary) {
        for (int i = 0; i < vertexCount && numPoints < MAX_POINTS; i++) {
            float x = 0, y = 0, z = 0;
            float fr = 0.0f, fg = 0.0f, fb = 0.0f;
            int r = 200, g = 200, b = 200;
            int gotColor = 0;
            int gotFloatColor = 0;
            for (int p = 0; p < propCount; p++) {
                switch (propTypes[p]) {
                    case PROP_FLOAT: {
                        float value;
                        if (fread(&value, sizeof(value), 1, file) != 1) goto load_done;
                        if (!isBinaryLE) {
                            unsigned char* bytes = (unsigned char*)&value;
                            unsigned char tmp = bytes[0]; bytes[0] = bytes[3]; bytes[3] = tmp;
                            tmp = bytes[1]; bytes[1] = bytes[2]; bytes[2] = tmp;
                        }
                        if (strcmp(propNames[p], "x") == 0) x = value;
                        else if (strcmp(propNames[p], "y") == 0) y = value;
                        else if (strcmp(propNames[p], "z") == 0) z = value;
                        else if (strcmp(propNames[p], "red") == 0 || strcmp(propNames[p], "r") == 0 || strstr(propNames[p], "red") != NULL || strcmp(propNames[p], "f_dc_0") == 0) { fr = value; gotColor = 1; gotFloatColor = 1; }
                        else if (strcmp(propNames[p], "green") == 0 || strcmp(propNames[p], "g") == 0 || strstr(propNames[p], "green") != NULL || strcmp(propNames[p], "f_dc_1") == 0) { fg = value; gotColor = 1; gotFloatColor = 1; }
                        else if (strcmp(propNames[p], "blue") == 0 || strcmp(propNames[p], "b") == 0 || strstr(propNames[p], "blue") != NULL || strcmp(propNames[p], "f_dc_2") == 0) { fb = value; gotColor = 1; gotFloatColor = 1; }
                        break;
                    }
                    case PROP_DOUBLE: {
                        double value;
                        if (fread(&value, sizeof(value), 1, file) != 1) goto load_done;
                        if (!isBinaryLE) {
                            unsigned char* bytes = (unsigned char*)&value;
                            for (int j = 0; j < 4; j++) {
                                unsigned char tmp = bytes[j];
                                bytes[j] = bytes[7 - j];
                                bytes[7 - j] = tmp;
                            }
                        }
                        if (strcmp(propNames[p], "x") == 0) x = (float)value;
                        else if (strcmp(propNames[p], "y") == 0) y = (float)value;
                        else if (strcmp(propNames[p], "z") == 0) z = (float)value;
                        else if (strcmp(propNames[p], "red") == 0 || strcmp(propNames[p], "r") == 0 || strstr(propNames[p], "red") != NULL || strcmp(propNames[p], "f_dc_0") == 0) { fr = (float)value; gotColor = 1; gotFloatColor = 1; }
                        else if (strcmp(propNames[p], "green") == 0 || strcmp(propNames[p], "g") == 0 || strstr(propNames[p], "green") != NULL || strcmp(propNames[p], "f_dc_1") == 0) { fg = (float)value; gotColor = 1; gotFloatColor = 1; }
                        else if (strcmp(propNames[p], "blue") == 0 || strcmp(propNames[p], "b") == 0 || strstr(propNames[p], "blue") != NULL || strcmp(propNames[p], "f_dc_2") == 0) { fb = (float)value; gotColor = 1; gotFloatColor = 1; }
                        break;
                    }
                    case PROP_INT: {
                        int value;
                        if (fread(&value, sizeof(value), 1, file) != 1) goto load_done;
                        break;
                    }
                    case PROP_UINT: {
                        unsigned int value;
                        if (fread(&value, sizeof(value), 1, file) != 1) goto load_done;
                        break;
                    }
                    case PROP_UCHAR: {
                        unsigned char value;
                        if (fread(&value, sizeof(value), 1, file) != 1) goto load_done;
                        if (strcmp(propNames[p], "red") == 0 || strcmp(propNames[p], "r") == 0 || strstr(propNames[p], "red") != NULL) { r = value; gotColor = 1; }
                        else if (strcmp(propNames[p], "green") == 0 || strcmp(propNames[p], "g") == 0 || strstr(propNames[p], "green") != NULL) { g = value; gotColor = 1; }
                        else if (strcmp(propNames[p], "blue") == 0 || strcmp(propNames[p], "b") == 0 || strstr(propNames[p], "blue") != NULL) { b = value; gotColor = 1; }
                        break;
                    }
                    default:
                        break;
                }
            }
            if (gotColor && gotFloatColor) {
                ConvertFloatColor(fr, fg, fb, &r, &g, &b);
            }
            if (!gotColor) {
                r = 200;
                g = 200;
                b = 200;
            }
            points[numPoints].x = x;
            points[numPoints].y = y;
            points[numPoints].z = z;
            points[numPoints].r = (unsigned char)r;
            points[numPoints].g = (unsigned char)g;
            points[numPoints].b = (unsigned char)b;
            numPoints++;
        }
    } else {
        for (int i = 0; i < vertexCount && numPoints < MAX_POINTS; i++) {
            if (!fgets(line, sizeof(line), file)) break;
            char* token = strtok(line, " \t\r\n");
            float x = 0, y = 0, z = 0;
            float fr = 0.0f, fg = 0.0f, fb = 0.0f;
            int r = 200, g = 200, b = 200;
            int gotColor = 0;
            int gotFloatColor = 0;
            for (int p = 0; p < propCount && token; p++) {
                if (propTypes[p] == PROP_FLOAT || propTypes[p] == PROP_DOUBLE) {
                    float value = (float)atof(token);
                    if (strcmp(propNames[p], "x") == 0) x = value;
                    else if (strcmp(propNames[p], "y") == 0) y = value;
                    else if (strcmp(propNames[p], "z") == 0) z = value;
                    else if (strcmp(propNames[p], "red") == 0 || strcmp(propNames[p], "r") == 0 || strstr(propNames[p], "red") != NULL || strcmp(propNames[p], "f_dc_0") == 0) { fr = value; gotColor = 1; gotFloatColor = 1; }
                    else if (strcmp(propNames[p], "green") == 0 || strcmp(propNames[p], "g") == 0 || strstr(propNames[p], "green") != NULL || strcmp(propNames[p], "f_dc_1") == 0) { fg = value; gotColor = 1; gotFloatColor = 1; }
                    else if (strcmp(propNames[p], "blue") == 0 || strcmp(propNames[p], "b") == 0 || strstr(propNames[p], "blue") != NULL || strcmp(propNames[p], "f_dc_2") == 0) { fb = value; gotColor = 1; gotFloatColor = 1; }
                } else if (propTypes[p] == PROP_UCHAR || propTypes[p] == PROP_INT || propTypes[p] == PROP_UINT) {
                    int value = atoi(token);
                    if (strcmp(propNames[p], "red") == 0 || strcmp(propNames[p], "r") == 0 || strstr(propNames[p], "red") != NULL) { r = value; gotColor = 1; }
                    else if (strcmp(propNames[p], "green") == 0 || strcmp(propNames[p], "g") == 0 || strstr(propNames[p], "green") != NULL) { g = value; gotColor = 1; }
                    else if (strcmp(propNames[p], "blue") == 0 || strcmp(propNames[p], "b") == 0 || strstr(propNames[p], "blue") != NULL) { b = value; gotColor = 1; }
                }
                token = strtok(NULL, " \t\r\n");
            }
            if (gotColor && gotFloatColor) {
                ConvertFloatColor(fr, fg, fb, &r, &g, &b);
            }
            if (!gotColor) {
                r = 200;
                g = 200;
                b = 200;
            }
            points[numPoints].x = x;
            points[numPoints].y = y;
            points[numPoints].z = z;
            points[numPoints].r = (unsigned char)r;
            points[numPoints].g = (unsigned char)g;
            points[numPoints].b = (unsigned char)b;
            numPoints++;
        }
    }

load_done:
    fclose(file);
    const char* name = strrchr(filename, '\\');
    if (!name) name = strrchr(filename, '/');
    if (name) name++; else name = filename;
    strncpy(currentFile, name, MAX_PATH - 1);
    currentFile[MAX_PATH - 1] = '\0';

    NormalizePointCloud();
    ComputeAverageSpacing();
    PrefilterColors();
    rotX = 0.0f; rotY = 0.0f;
    zoom = 1.0f; panX = panY = 0.0f;
    return 1;
}

void OpenFileDialog(HWND hwnd) {
    OPENFILENAME ofn;
    char filename[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PLY Files (*.ply)\0*.ply\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Open PLY Point Cloud File";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        if (LoadPLY(filename)) {
            UploadPointVBO();
            UpdateTitle(hwnd);
        }
    }
}
