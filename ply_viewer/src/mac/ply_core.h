#ifndef PLY_CORE_H
#define PLY_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define PLY_MAX_POINTS 50000000
#define PLY_MAX_PATH 1024

enum { PLY_MODE_POINTS = 0, PLY_MODE_SPLATS = 1 };
enum { PLY_COLOR_RGB = 0, PLY_COLOR_HEIGHT = 1, PLY_COLOR_NORMAL = 2, PLY_COLOR_UNIFORM = 3 };
enum { PLY_BG_STUDIO = 0, PLY_BG_BLACK = 1, PLY_BG_GRAY = 2, PLY_BG_WHITE = 3 };
enum { PLY_UP_POS_Y = 0, PLY_UP_POS_Z = 1, PLY_UP_NEG_Y = 2, PLY_UP_NEG_Z = 3 };
enum {
    PLY_VIEW_FRONT = 0, PLY_VIEW_BACK, PLY_VIEW_LEFT, PLY_VIEW_RIGHT,
    PLY_VIEW_TOP, PLY_VIEW_BOTTOM, PLY_VIEW_ISO
};

typedef struct {
    int   renderMode;
    float pointSize;
    float splatScale;
    float opacityCutoff;
    int   colorMode;
    int   edl;
    float edlStrength;
    int   background;
    int   showGrid;
    int   showAxes;
    int   ortho;
    float fov;
    int   upAxis;
    int   shading;
} PlySettings;

typedef struct {
    int    numPoints;
    int    hasColor;
    int    hasNormals;
    int    isSplat;
    int    format;
    float  bmin[3], bmax[3];
    float  spacing;
    double loadSeconds;
    char   name[256];
    char   path[PLY_MAX_PATH];
} PlyInfo;

typedef struct {
    double sortMs;
    double drawMs;
    int    splatCapable;
} PlyFrameStats;

typedef struct PlyCloud PlyCloud;
typedef void (*PlyProgressFn)(float fraction, void* user);

PlyCloud* ply_load(const char* path, PlyProgressFn progress, void* user, char* err, int errLen);
void      ply_cloud_free(PlyCloud* c);

int  ply_gl_init(void);
void ply_gl_shutdown(void);
void ply_gl_set_cloud(PlyCloud* c);
void ply_gl_clear(void);

const PlyInfo*  ply_info(void);
PlySettings*    ply_settings(void);
PlyFrameStats*  ply_stats(void);
void            ply_settings_default(PlySettings* s);

void ply_draw(int width, int height, float pixelScale);
int  ply_needs_redraw(void);
int  ply_sort_settled(void);
int  ply_read_pixels(unsigned char* rgba, int width, int height);

void ply_cam_orbit(float dx, float dy);
void ply_cam_pan(float dx, float dy);
void ply_cam_zoom(float factor, float nx, float ny, int useCursor);
void ply_cam_reset(void);
void ply_cam_fit(void);
void ply_cam_view(int preset);
void ply_cam_set_up_axis(int upAxis);
int  ply_cam_pick_pivot(float nx, float ny);

#ifdef __cplusplus
}
#endif

#endif
