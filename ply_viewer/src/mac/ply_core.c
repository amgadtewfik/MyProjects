#if defined(_WIN32)
#include "gl3_win.h"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <strings.h>
#endif
#include <math.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/time.h>

#include "ply_core.h"

#define PI_F 3.14159265358979f
#define SH_C0 0.28209479177387814f

struct PlyCloud {
    int n;
    float* pos;
    unsigned char* rgba;
    signed char* nrm;
    float* cov;
    PlyInfo info;
    float rmin[3], rmax[3];
};

typedef enum { T_NONE = 0, T_INT8, T_UINT8, T_INT16, T_UINT16, T_INT32, T_UINT32, T_FLOAT32, T_FLOAT64 } ScalarType;
static const int kTypeSize[] = { 0, 1, 1, 2, 2, 4, 4, 4, 8 };

typedef enum {
    R_NONE = 0, R_X, R_Y, R_Z, R_NX, R_NY, R_NZ, R_R, R_G, R_B, R_A,
    R_DC0, R_DC1, R_DC2, R_OPACITY, R_S0, R_S1, R_S2, R_Q0, R_Q1, R_Q2, R_Q3
} Role;

typedef struct { char name[64]; ScalarType type; int isList; ScalarType countType; Role role; long offset; } Prop;
typedef struct { char name[64]; long count; Prop props[128]; int nprops; int hasList; long recordSize; } Element;

static double now_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static void set_err(char* err, int len, const char* msg) {
    if (err && len > 0) { strncpy(err, msg, (size_t)len - 1); err[len - 1] = '\0'; }
}

static ScalarType parse_type(const char* t) {
    if (!strcmp(t, "char") || !strcmp(t, "int8") || !strcmp(t, "int8_t")) return T_INT8;
    if (!strcmp(t, "uchar") || !strcmp(t, "uint8") || !strcmp(t, "uint8_t")) return T_UINT8;
    if (!strcmp(t, "short") || !strcmp(t, "int16") || !strcmp(t, "int16_t")) return T_INT16;
    if (!strcmp(t, "ushort") || !strcmp(t, "uint16") || !strcmp(t, "uint16_t")) return T_UINT16;
    if (!strcmp(t, "int") || !strcmp(t, "int32") || !strcmp(t, "int32_t")) return T_INT32;
    if (!strcmp(t, "uint") || !strcmp(t, "uint32") || !strcmp(t, "uint32_t")) return T_UINT32;
    if (!strcmp(t, "float") || !strcmp(t, "float32") || !strcmp(t, "float32_t")) return T_FLOAT32;
    if (!strcmp(t, "double") || !strcmp(t, "float64") || !strcmp(t, "double64")) return T_FLOAT64;
    return T_NONE;
}

static Role classify(const char* n) {
    if (!strcasecmp(n, "x")) return R_X;
    if (!strcasecmp(n, "y")) return R_Y;
    if (!strcasecmp(n, "z")) return R_Z;
    if (!strcasecmp(n, "nx") || !strcasecmp(n, "normal_x")) return R_NX;
    if (!strcasecmp(n, "ny") || !strcasecmp(n, "normal_y")) return R_NY;
    if (!strcasecmp(n, "nz") || !strcasecmp(n, "normal_z")) return R_NZ;
    if (!strcasecmp(n, "red") || !strcasecmp(n, "r") || !strcasecmp(n, "diffuse_red")) return R_R;
    if (!strcasecmp(n, "green") || !strcasecmp(n, "g") || !strcasecmp(n, "diffuse_green")) return R_G;
    if (!strcasecmp(n, "blue") || !strcasecmp(n, "b") || !strcasecmp(n, "diffuse_blue")) return R_B;
    if (!strcasecmp(n, "alpha") || !strcasecmp(n, "a")) return R_A;
    if (!strcmp(n, "f_dc_0")) return R_DC0;
    if (!strcmp(n, "f_dc_1")) return R_DC1;
    if (!strcmp(n, "f_dc_2")) return R_DC2;
    if (!strcmp(n, "opacity")) return R_OPACITY;
    if (!strcmp(n, "scale_0")) return R_S0;
    if (!strcmp(n, "scale_1")) return R_S1;
    if (!strcmp(n, "scale_2")) return R_S2;
    if (!strcmp(n, "rot_0")) return R_Q0;
    if (!strcmp(n, "rot_1")) return R_Q1;
    if (!strcmp(n, "rot_2")) return R_Q2;
    if (!strcmp(n, "rot_3")) return R_Q3;
    return R_NONE;
}

static double read_scalar(const unsigned char* p, ScalarType t, int bigEndian) {
    unsigned char b[8];
    int sz = kTypeSize[t];
    if (bigEndian) { for (int i = 0; i < sz; i++) b[i] = p[sz - 1 - i]; p = b; }
    switch (t) {
        case T_INT8:    return (double)(int8_t)p[0];
        case T_UINT8:   return (double)p[0];
        case T_INT16:   { int16_t v; memcpy(&v, p, 2); return v; }
        case T_UINT16:  { uint16_t v; memcpy(&v, p, 2); return v; }
        case T_INT32:   { int32_t v; memcpy(&v, p, 4); return v; }
        case T_UINT32:  { uint32_t v; memcpy(&v, p, 4); return v; }
        case T_FLOAT32: { float v; memcpy(&v, p, 4); return v; }
        case T_FLOAT64: { double v; memcpy(&v, p, 8); return v; }
        default: return 0.0;
    }
}

static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static unsigned char to_u8(float v) { int i = (int)(v * 255.0f + 0.5f); return (unsigned char)(i < 0 ? 0 : (i > 255 ? 255 : i)); }

static int cmp_float(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return (x > y) - (x < y);
}

static void robust_bounds(const float* pos, int n, float* rmin, float* rmax) {
    int stride = n / 200000; if (stride < 1) stride = 1;
    int m = n / stride;
    float* tmp = (float*)malloc((size_t)m * sizeof(float));
    if (!tmp || m < 50) {
        free(tmp);
        for (int a = 0; a < 3; a++) { rmin[a] = pos[a]; rmax[a] = pos[a]; }
        for (int i = 1; i < n; i++) for (int a = 0; a < 3; a++) {
            float v = pos[i * 3 + a];
            if (v < rmin[a]) rmin[a] = v;
            if (v > rmax[a]) rmax[a] = v;
        }
        return;
    }
    for (int a = 0; a < 3; a++) {
        for (int i = 0; i < m; i++) tmp[i] = pos[(size_t)i * stride * 3 + a];
        qsort(tmp, (size_t)m, sizeof(float), cmp_float);
        rmin[a] = tmp[(int)(m * 0.02)];
        rmax[a] = tmp[(int)(m * 0.98)];
    }
    free(tmp);
}

static float estimate_spacing(const float* pos, int n, const float* bmin, const float* bmax) {
    if (n < 2) return 1.0f;
    float ext[3], maxExt = 0.f;
    for (int a = 0; a < 3; a++) { ext[a] = bmax[a] - bmin[a]; if (ext[a] > maxExt) maxExt = ext[a]; }
    if (maxExt <= 0.f) return 1.0f;
    for (int a = 0; a < 3; a++) if (ext[a] < maxExt * 1e-4f) ext[a] = maxExt * 1e-4f;
    double target = n < 4000000 ? (double)n : 4000000.0;
    float cell = (float)cbrt((double)ext[0] * ext[1] * ext[2] / target);
    if (!(cell > 0.f)) cell = maxExt / 64.f;
    long gx, gy, gz;
    for (;;) {
        gx = (long)(ext[0] / cell) + 1; gy = (long)(ext[1] / cell) + 1; gz = (long)(ext[2] / cell) + 1;
        if (gx * gy * gz <= 8000000L) break;
        cell *= 1.25f;
    }
    long cells = gx * gy * gz;
    int* start = (int*)calloc((size_t)cells + 1, sizeof(int));
    int* order = (int*)malloc((size_t)n * sizeof(int));
    int* cellOf = (int*)malloc((size_t)n * sizeof(int));
    if (!start || !order || !cellOf) { free(start); free(order); free(cellOf); return maxExt / (float)cbrt((double)n); }
    for (int i = 0; i < n; i++) {
        long cx = (long)((pos[i*3+0] - bmin[0]) / cell); if (cx < 0) cx = 0; if (cx >= gx) cx = gx - 1;
        long cy = (long)((pos[i*3+1] - bmin[1]) / cell); if (cy < 0) cy = 0; if (cy >= gy) cy = gy - 1;
        long cz = (long)((pos[i*3+2] - bmin[2]) / cell); if (cz < 0) cz = 0; if (cz >= gz) cz = gz - 1;
        long id = (cx * gy + cy) * gz + cz;
        cellOf[i] = (int)id;
        start[id + 1]++;
    }
    for (long c = 0; c < cells; c++) start[c + 1] += start[c];
    int* fill = (int*)malloc((size_t)cells * sizeof(int));
    if (!fill) { free(start); free(order); free(cellOf); return maxExt / (float)cbrt((double)n); }
    memcpy(fill, start, (size_t)cells * sizeof(int));
    for (int i = 0; i < n; i++) order[fill[cellOf[i]]++] = i;
    free(fill);

    int q = n < 3000 ? n : 3000;
    int stride = n / q;
    float* d = (float*)malloc((size_t)q * sizeof(float));
    int found = 0;
    for (int s = 0; s < q && d; s++) {
        int i = s * stride;
        long id = cellOf[i];
        long cz = id % gz, cy = (id / gz) % gy, cx = id / (gz * gy);
        float best = 1e30f;
        for (long dx = -1; dx <= 1; dx++) for (long dy = -1; dy <= 1; dy++) for (long dz = -1; dz <= 1; dz++) {
            long nx = cx + dx, ny = cy + dy, nz = cz + dz;
            if (nx < 0 || ny < 0 || nz < 0 || nx >= gx || ny >= gy || nz >= gz) continue;
            long nid = (nx * gy + ny) * gz + nz;
            int lim = start[nid + 1];
            if (lim - start[nid] > 4096) lim = start[nid] + 4096;
            for (int k = start[nid]; k < lim; k++) {
                int j = order[k];
                if (j == i) continue;
                float ex = pos[j*3] - pos[i*3], ey = pos[j*3+1] - pos[i*3+1], ez = pos[j*3+2] - pos[i*3+2];
                float dd = ex*ex + ey*ey + ez*ez;
                if (dd < best) best = dd;
            }
        }
        if (best < 1e29f && best > 0.f) d[found++] = sqrtf(best);
    }
    float result;
    if (found > 0) { qsort(d, (size_t)found, sizeof(float), cmp_float); result = d[found / 2]; }
    else result = cell * 0.5f;
    free(d); free(start); free(order); free(cellOf);
    if (!(result > 0.f)) result = maxExt * 1e-3f;
    return result;
}

void ply_cloud_free(PlyCloud* c) {
    if (!c) return;
    free(c->pos); free(c->rgba); free(c->nrm); free(c->cov);
    free(c);
}

PlyCloud* ply_load(const char* path, PlyProgressFn progress, void* user, char* err, int errLen) {
    double t0 = now_sec();
    FILE* f = fopen(path, "rb");
    if (!f) { set_err(err, errLen, "The file could not be opened."); return NULL; }
    char line[8192];
    if (!fgets(line, sizeof line, f) || strncmp(line, "ply", 3) != 0) {
        fclose(f); set_err(err, errLen, "This is not a PLY file (missing 'ply' magic)."); return NULL;
    }
    int format = -1;
    Element* elems = (Element*)calloc(64, sizeof(Element));
    int nel = 0;
    while (fgets(line, sizeof line, f)) {
        size_t L = strlen(line);
        while (L > 0 && (line[L-1] == '\n' || line[L-1] == '\r' || line[L-1] == ' ')) line[--L] = '\0';
        if (!strncmp(line, "format ", 7)) {
            if (strstr(line, "ascii")) format = 0;
            else if (strstr(line, "binary_little_endian")) format = 1;
            else if (strstr(line, "binary_big_endian")) format = 2;
        } else if (!strncmp(line, "element ", 8)) {
            if (nel < 64) {
                Element* e = &elems[nel++];
                if (sscanf(line, "element %63s %ld", e->name, &e->count) != 2) { nel--; }
            }
        } else if (!strncmp(line, "property ", 9)) {
            if (nel == 0) continue;
            Element* e = &elems[nel - 1];
            if (e->nprops >= 128) continue;
            Prop* p = &e->props[e->nprops];
            char t1[32] = {0}, t2[32] = {0};
            if (!strncmp(line + 9, "list ", 5)) {
                if (sscanf(line, "property list %31s %31s %63s", t1, t2, p->name) == 3) {
                    p->isList = 1; p->countType = parse_type(t1); p->type = parse_type(t2);
                    e->hasList = 1; e->nprops++;
                }
            } else if (sscanf(line, "property %31s %63s", t1, p->name) == 2) {
                p->type = parse_type(t1);
                e->nprops++;
            }
        } else if (!strncmp(line, "end_header", 10)) {
            break;
        }
    }
    if (format < 0) { fclose(f); free(elems); set_err(err, errLen, "PLY header has no valid 'format' line."); return NULL; }
    int vi = -1;
    for (int k = 0; k < nel; k++) if (!strcmp(elems[k].name, "vertex")) { vi = k; break; }
    if (vi < 0) { fclose(f); free(elems); set_err(err, errLen, "PLY file has no vertex element."); return NULL; }
    Element* ve = &elems[vi];
    if (ve->hasList) { fclose(f); free(elems); set_err(err, errLen, "List properties on vertices are not supported."); return NULL; }
    if (ve->count <= 0) { fclose(f); free(elems); set_err(err, errLen, "The vertex element is empty."); return NULL; }
    for (int k = 0; k < nel; k++) {
        Element* e = &elems[k];
        e->recordSize = 0;
        for (int p = 0; p < e->nprops; p++) {
            e->props[p].offset = e->recordSize;
            e->props[p].role = classify(e->props[p].name);
            if (e->props[p].type == T_NONE && !e->props[p].isList) {
                fclose(f); free(elems); set_err(err, errLen, "Unknown property type in header."); return NULL;
            }
            e->recordSize += kTypeSize[e->props[p].type];
        }
    }
    for (int k = 0; k < vi; k++) {
        Element* e = &elems[k];
        if (format == 0) {
            for (long i = 0; i < e->count; i++) if (!fgets(line, sizeof line, f)) break;
        } else if (e->hasList) {
            fclose(f); free(elems); set_err(err, errLen, "Elements with list properties before the vertex block are not supported."); return NULL;
        } else {
            fseek(f, e->count * e->recordSize, SEEK_CUR);
        }
    }

    int hasX = 0, hasY = 0, hasZ = 0, hasN = 0, hasRGB = 0, hasDC = 0, hasScale = 0, hasRot = 0, hasOpacity = 0, hasAlpha = 0, colorRaw = 0;
    for (int p = 0; p < ve->nprops; p++) {
        Prop* pr = &ve->props[p];
        switch (pr->role) {
            case R_X: hasX = 1; break; case R_Y: hasY = 1; break; case R_Z: hasZ = 1; break;
            case R_NX: case R_NY: case R_NZ: hasN++; break;
            case R_R: case R_G: case R_B: hasRGB++; if (pr->type != T_UINT8) colorRaw = 1; break;
            case R_A: hasAlpha = 1; break;
            case R_DC0: case R_DC1: case R_DC2: hasDC++; break;
            case R_OPACITY: hasOpacity = 1; break;
            case R_S0: case R_S1: case R_S2: hasScale++; break;
            case R_Q0: case R_Q1: case R_Q2: case R_Q3: hasRot++; break;
            default: break;
        }
    }
    if (!hasX || !hasY || !hasZ) { fclose(f); free(elems); set_err(err, errLen, "Vertex element has no x, y, z properties."); return NULL; }
    hasN = (hasN == 3);
    hasRGB = (hasRGB == 3);
    hasDC = (hasDC == 3);
    int isSplat = (hasScale == 3 && hasRot == 4);
    long n = ve->count;
    if (n > PLY_MAX_POINTS) n = PLY_MAX_POINTS;

    PlyCloud* c = (PlyCloud*)calloc(1, sizeof(PlyCloud));
    c->pos = (float*)malloc((size_t)n * 3 * sizeof(float));
    c->rgba = (unsigned char*)malloc((size_t)n * 4);
    if (hasN) c->nrm = (signed char*)malloc((size_t)n * 4);
    if (isSplat) c->cov = (float*)malloc((size_t)n * 6 * sizeof(float));
    float* rawCol = (hasRGB && colorRaw) ? (float*)malloc((size_t)n * 3 * sizeof(float)) : NULL;
    if (!c->pos || !c->rgba || (hasN && !c->nrm) || (isSplat && !c->cov) || (hasRGB && colorRaw && !rawCol)) {
        fclose(f); free(elems); free(rawCol); ply_cloud_free(c);
        set_err(err, errLen, "Not enough memory to load this file."); return NULL;
    }

    double vals[128];
    memset(vals, 0, sizeof vals);
    long i = 0;
    int bigEndian = (format == 2);
    float bmin[3] = { 1e30f, 1e30f, 1e30f }, bmax[3] = { -1e30f, -1e30f, -1e30f };

#define PROCESS_RECORD() do { \
        float x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0, cr = 0, cg = 0, cb = 0, dc0 = 0, dc1 = 0, dc2 = 0; \
        float op = 0, s0 = 0, s1 = 0, s2 = 0, q0 = 1, q1 = 0, q2 = 0, q3 = 0, al = 255; \
        for (int p = 0; p < ve->nprops; p++) { \
            double v = vals[p]; \
            switch (ve->props[p].role) { \
                case R_X: x = (float)v; break; case R_Y: y = (float)v; break; case R_Z: z = (float)v; break; \
                case R_NX: nx = (float)v; break; case R_NY: ny = (float)v; break; case R_NZ: nz = (float)v; break; \
                case R_R: cr = (float)v; break; case R_G: cg = (float)v; break; case R_B: cb = (float)v; break; \
                case R_A: al = (float)v; break; \
                case R_DC0: dc0 = (float)v; break; case R_DC1: dc1 = (float)v; break; case R_DC2: dc2 = (float)v; break; \
                case R_OPACITY: op = (float)v; break; \
                case R_S0: s0 = (float)v; break; case R_S1: s1 = (float)v; break; case R_S2: s2 = (float)v; break; \
                case R_Q0: q0 = (float)v; break; case R_Q1: q1 = (float)v; break; case R_Q2: q2 = (float)v; break; case R_Q3: q3 = (float)v; break; \
                default: break; \
            } \
        } \
        if (!isfinite(x) || !isfinite(y) || !isfinite(z)) { x = y = z = 0.f; } \
        c->pos[i*3+0] = x; c->pos[i*3+1] = y; c->pos[i*3+2] = z; \
        if (x < bmin[0]) bmin[0] = x; if (x > bmax[0]) bmax[0] = x; \
        if (y < bmin[1]) bmin[1] = y; if (y > bmax[1]) bmax[1] = y; \
        if (z < bmin[2]) bmin[2] = z; if (z > bmax[2]) bmax[2] = z; \
        unsigned char* col = &c->rgba[i*4]; \
        if (hasDC) { \
            col[0] = to_u8(clamp01(0.5f + SH_C0 * dc0)); col[1] = to_u8(clamp01(0.5f + SH_C0 * dc1)); col[2] = to_u8(clamp01(0.5f + SH_C0 * dc2)); \
        } else if (hasRGB && rawCol) { \
            rawCol[i*3+0] = cr; rawCol[i*3+1] = cg; rawCol[i*3+2] = cb; \
        } else if (hasRGB) { \
            col[0] = (unsigned char)cr; col[1] = (unsigned char)cg; col[2] = (unsigned char)cb; \
        } else { col[0] = col[1] = col[2] = 200; } \
        if (hasOpacity) col[3] = to_u8(1.0f / (1.0f + expf(-op))); \
        else if (hasAlpha) col[3] = (unsigned char)(al < 0 ? 0 : (al > 255 ? 255 : al)); \
        else col[3] = 255; \
        if (hasN) { \
            float len = sqrtf(nx*nx + ny*ny + nz*nz); \
            if (len > 1e-12f) { nx /= len; ny /= len; nz /= len; } \
            c->nrm[i*4+0] = (signed char)(nx * 127.f); c->nrm[i*4+1] = (signed char)(ny * 127.f); c->nrm[i*4+2] = (signed char)(nz * 127.f); c->nrm[i*4+3] = 0; \
        } \
        if (isSplat) { \
            if (s0 > 20.f) s0 = 20.f; if (s1 > 20.f) s1 = 20.f; if (s2 > 20.f) s2 = 20.f; \
            float sx = expf(s0), sy = expf(s1), sz = expf(s2); \
            float ql = sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3); \
            if (ql < 1e-12f) { q0 = 1; q1 = q2 = q3 = 0; ql = 1; } \
            float w = q0/ql, qx = q1/ql, qy = q2/ql, qz = q3/ql; \
            float R[9] = { 1-2*(qy*qy+qz*qz), 2*(qx*qy-w*qz), 2*(qx*qz+w*qy), \
                           2*(qx*qy+w*qz), 1-2*(qx*qx+qz*qz), 2*(qy*qz-w*qx), \
                           2*(qx*qz-w*qy), 2*(qy*qz+w*qx), 1-2*(qx*qx+qy*qy) }; \
            float M[9]; \
            for (int r = 0; r < 3; r++) { M[r*3+0] = R[r*3+0]*sx; M[r*3+1] = R[r*3+1]*sy; M[r*3+2] = R[r*3+2]*sz; } \
            float* cv = &c->cov[i*6]; \
            cv[0] = M[0]*M[0] + M[1]*M[1] + M[2]*M[2]; \
            cv[1] = M[0]*M[3] + M[1]*M[4] + M[2]*M[5]; \
            cv[2] = M[0]*M[6] + M[1]*M[7] + M[2]*M[8]; \
            cv[3] = M[3]*M[3] + M[4]*M[4] + M[5]*M[5]; \
            cv[4] = M[3]*M[6] + M[4]*M[7] + M[5]*M[8]; \
            cv[5] = M[6]*M[6] + M[7]*M[7] + M[8]*M[8]; \
        } \
    } while (0)

    if (format == 0) {
        for (i = 0; i < n; i++) {
            int ok = 1;
            for (int p = 0; p < ve->nprops; p++) {
                if (fscanf(f, "%lf", &vals[p]) != 1) { ok = 0; break; }
            }
            if (!ok) break;
            PROCESS_RECORD();
            if (progress && (i & 0xFFFF) == 0) progress((float)i / (float)n, user);
        }
    } else {
        long recSize = ve->recordSize;
        long bufRecs = (8L << 20) / recSize; if (bufRecs < 1) bufRecs = 1; if (bufRecs > n) bufRecs = n;
        unsigned char* buf = (unsigned char*)malloc((size_t)bufRecs * (size_t)recSize);
        if (!buf) { fclose(f); free(elems); free(rawCol); ply_cloud_free(c); set_err(err, errLen, "Not enough memory to load this file."); return NULL; }
        int activeIdx[128]; int nActive = 0;
        for (int p = 0; p < ve->nprops; p++) if (ve->props[p].role != R_NONE) activeIdx[nActive++] = p;
        long remaining = n;
        i = 0;
        while (remaining > 0) {
            long want = remaining < bufRecs ? remaining : bufRecs;
            size_t got = fread(buf, (size_t)recSize, (size_t)want, f);
            if (got == 0) break;
            for (size_t r = 0; r < got; r++) {
                const unsigned char* rec = buf + r * (size_t)recSize;
                for (int a = 0; a < nActive; a++) {
                    Prop* pr = &ve->props[activeIdx[a]];
                    vals[activeIdx[a]] = read_scalar(rec + pr->offset, pr->type, bigEndian);
                }
                PROCESS_RECORD();
                i++;
            }
            remaining -= (long)got;
            if (progress) progress((float)i / (float)n, user);
        }
        free(buf);
    }
#undef PROCESS_RECORD
    fclose(f);
    free(elems);
    if (i <= 0) { free(rawCol); ply_cloud_free(c); set_err(err, errLen, "No vertex data could be read from the file."); return NULL; }
    n = i;
    c->n = (int)n;

    if (rawCol) {
        float mx = 0.f;
        for (long k = 0; k < n * 3; k++) if (rawCol[k] > mx) mx = rawCol[k];
        float scale = mx <= 1.0f ? 255.f : (mx <= 255.f ? 1.f : (255.f / 65535.f));
        for (long k = 0; k < n; k++) {
            c->rgba[k*4+0] = to_u8(clamp01(rawCol[k*3+0] * scale / 255.f));
            c->rgba[k*4+1] = to_u8(clamp01(rawCol[k*3+1] * scale / 255.f));
            c->rgba[k*4+2] = to_u8(clamp01(rawCol[k*3+2] * scale / 255.f));
        }
        free(rawCol);
    }

    PlyInfo* info = &c->info;
    info->numPoints = (int)n;
    info->hasColor = hasDC || hasRGB;
    info->hasNormals = hasN;
    info->isSplat = isSplat;
    info->format = format;
    memcpy(info->bmin, bmin, sizeof bmin);
    memcpy(info->bmax, bmax, sizeof bmax);
    robust_bounds(c->pos, (int)n, c->rmin, c->rmax);
    if (progress) progress(0.97f, user);
    info->spacing = estimate_spacing(c->pos, (int)n, bmin, bmax);
    strncpy(info->path, path, PLY_MAX_PATH - 1);
    const char* base = strrchr(path, '/');
    strncpy(info->name, base ? base + 1 : path, sizeof(info->name) - 1);
    info->loadSeconds = now_sec() - t0;
    if (progress) progress(1.0f, user);
    return c;
}

/* ------------------------------------------------------------------ */
/* GL state                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    GLuint prog;
    GLint uView, uProj, uModel, uRadius, uProjScale, uOrtho, uMinPx, uMaxPx, uCutoff;
    GLint uColorMode, uHeightRange, uUniformColor, uShading, uHasNormals, uPick;
} PointsProg;

typedef struct {
    GLuint prog;
    GLint uSplats, uView, uProj, uModel, uViewport, uFocal, uOrtho, uScale, uCutoff, uColorMode, uHeightRange, uUniformColor;
} SplatProg;

typedef struct {
    GLuint prog;
    GLint uMVP;
} LinesProg;

typedef struct {
    GLuint prog;
    GLint uColor, uDepth, uSize, uBg, uEdl, uEdlStrength, uEdlRadius, uNearFar, uOrtho;
} CompProg;

static PlySettings gSettings;
static PlyInfo gInfoEmpty;
static PlyFrameStats gStats;
static PlyCloud* gCloud = NULL;

static PointsProg gPP;
static SplatProg gSP;
static LinesProg gLP;
static CompProg gCP;

static GLuint vaoPoints, vboPoints;
static GLuint vaoSplat, vboSplatIdx, tboBuf, tboTex;
static int tboReady = 0;
static GLuint vaoLines, vboGrid, vboOverlay;
static int gridVerts = 0;
static GLuint vaoEmpty;
static GLuint fbo, texColor, texDepth;
static int fboW = 0, fboH = 0;
static GLint maxTexBufferTexels = 0;

typedef struct { float x, y, z, r, g, b, a; } LineVert;

/* camera */
static float camTarget[3] = { 0, 0, 0 };
static float camDist = 8.0f;
static float camYaw = 35.0f, camPitch = 22.0f;
static float sceneCenter[3] = { 0, 0, 0 };
static float sceneRadius = 1.0f;
static float fullCenter[3] = { 0, 0, 0 };
static float fullRadius = 1.0f;
static float heightMin = -1.f, heightMax = 1.f;
static float upModel[16];

typedef struct {
    float look[16], model[16], view[16], proj[16];
    float camPos[3], fwd[3], right[3], up[3];
    float nearZ, farZ, hh, hw, aspect, tanHalf;
    int w, h; float ps; float viewHPoints;
    int ortho;
    int valid;
} Frame;
static Frame gF;

/* sort */
static atomic_int sortBusy, sortDone;
static float sortRow[3];
static float sortedRow[3];
static int sortValid = 0;
static uint32_t* idxFront = NULL;
static uint32_t* idxBack = NULL;
static double sortStart = 0;

/* ------------------------------------------------------------------ */
/* math                                                               */
/* ------------------------------------------------------------------ */

static void m4_identity(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.f; }

static void m4_mul(float* out, const float* a, const float* b) {
    float t[16];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
        t[i + j*4] = a[i] * b[j*4] + a[i+4] * b[j*4+1] + a[i+8] * b[j*4+2] + a[i+12] * b[j*4+3];
    memcpy(out, t, 64);
}

static void m4_perspective(float* m, float fovDeg, float aspect, float n, float f) {
    float t = 1.f / tanf(fovDeg * PI_F / 360.f);
    memset(m, 0, 64);
    m[0] = t / aspect; m[5] = t;
    m[10] = (f + n) / (n - f); m[11] = -1.f;
    m[14] = 2.f * f * n / (n - f);
}

static void m4_ortho(float* m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 64);
    m[0] = 2.f / (r - l); m[5] = 2.f / (t - b); m[10] = -2.f / (f - n);
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n); m[15] = 1.f;
}

static void v3_norm(float* v) { float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); if (l > 1e-20f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
static void v3_cross(float* o, const float* a, const float* b) { o[0] = a[1]*b[2] - a[2]*b[1]; o[1] = a[2]*b[0] - a[0]*b[2]; o[2] = a[0]*b[1] - a[1]*b[0]; }
static float v3_dot(const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static void m4_lookat(float* m, const float* eye, const float* center, const float* upHint, float* fwd, float* right, float* up) {
    float f[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
    v3_norm(f);
    float s[3]; v3_cross(s, f, upHint); v3_norm(s);
    float u[3]; v3_cross(u, s, f);
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];  m[12] = -v3_dot(s, eye);
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];  m[13] = -v3_dot(u, eye);
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2]; m[14] = v3_dot(f, eye);
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
    memcpy(fwd, f, 12); memcpy(right, s, 12); memcpy(up, u, 12);
}

static void build_up_model(int upAxis, float* m) {
    m4_identity(m);
    switch (upAxis) {
        case PLY_UP_POS_Z: m[5] = 0; m[9] = 1; m[6] = -1; m[10] = 0; break;
        case PLY_UP_NEG_Y: m[5] = -1; m[10] = -1; break;
        case PLY_UP_NEG_Z: m[5] = 0; m[9] = -1; m[6] = 1; m[10] = 0; break;
        default: break;
    }
}

static void transform_box(const float* m, const float* bmin, const float* bmax, float* omin, float* omax) {
    omin[0] = omin[1] = omin[2] = 1e30f; omax[0] = omax[1] = omax[2] = -1e30f;
    for (int k = 0; k < 8; k++) {
        float p[3] = { (k & 1) ? bmax[0] : bmin[0], (k & 2) ? bmax[1] : bmin[1], (k & 4) ? bmax[2] : bmin[2] };
        for (int a = 0; a < 3; a++) {
            float v = m[a] * p[0] + m[4 + a] * p[1] + m[8 + a] * p[2];
            if (v < omin[a]) omin[a] = v;
            if (v > omax[a]) omax[a] = v;
        }
    }
}

static float nice_step(float x) {
    if (!(x > 0.f)) return 1.f;
    float e = floorf(log10f(x));
    float p = powf(10.f, e);
    float f = x / p;
    float nf = f < 1.5f ? 1.f : (f < 3.5f ? 2.f : (f < 7.5f ? 5.f : 10.f));
    return nf * p;
}

/* ------------------------------------------------------------------ */
/* shaders                                                            */
/* ------------------------------------------------------------------ */

static const char* kColorFuncs =
    "vec3 heightColor(float t) {\n"
    "  t = clamp(t, 0.0, 1.0);\n"
    "  vec3 c0 = vec3(0.19, 0.07, 0.42), c1 = vec3(0.12, 0.44, 0.85), c2 = vec3(0.10, 0.75, 0.62);\n"
    "  vec3 c3 = vec3(0.85, 0.85, 0.20), c4 = vec3(0.95, 0.35, 0.15);\n"
    "  if (t < 0.25) return mix(c0, c1, t / 0.25);\n"
    "  if (t < 0.5) return mix(c1, c2, (t - 0.25) / 0.25);\n"
    "  if (t < 0.75) return mix(c2, c3, (t - 0.5) / 0.25);\n"
    "  return mix(c3, c4, (t - 0.75) / 0.25);\n"
    "}\n";

static const char* vsPoints =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "layout(location = 2) in vec3 aNormal;\n"
    "uniform mat4 uView, uProj, uModel;\n"
    "uniform float uRadius, uProjScale, uMinPx, uMaxPx, uCutoff;\n"
    "uniform int uOrtho, uColorMode, uShading, uHasNormals, uPick;\n"
    "uniform vec2 uHeightRange; uniform vec3 uUniformColor;\n"
    "out vec4 vColor;\n"
    "%s"
    "void main() {\n"
    "  if (aColor.a < uCutoff) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); gl_PointSize = 0.0; vColor = vec4(0.0); return; }\n"
    "  vec4 eye = uView * vec4(aPos, 1.0);\n"
    "  gl_Position = uProj * eye;\n"
    "  float px = (uOrtho == 1) ? uRadius * uProjScale : uRadius * uProjScale / max(-eye.z, 1e-6);\n"
    "  gl_PointSize = (uPick == 1) ? uMinPx : clamp(px * 2.0, uMinPx, uMaxPx);\n"
    "  vec3 c = aColor.rgb;\n"
    "  vec3 wn = normalize(mat3(uModel) * aNormal + vec3(1e-9));\n"
    "  if (uColorMode == 1) { float wy = (uModel * vec4(aPos, 1.0)).y; c = heightColor((wy - uHeightRange.x) / max(uHeightRange.y - uHeightRange.x, 1e-9)); }\n"
    "  else if (uColorMode == 2) { c = (uHasNormals == 1) ? wn * 0.5 + 0.5 : vec3(0.8); }\n"
    "  else if (uColorMode == 3) { c = uUniformColor; }\n"
    "  if (uShading == 1 && uHasNormals == 1) { vec3 en = normalize(mat3(uView) * aNormal); c *= 0.35 + 0.65 * abs(en.z); }\n"
    "  vColor = vec4(c, 1.0);\n"
    "}\n";

static const char* fsPoints =
    "#version 330 core\n"
    "in vec4 vColor; out vec4 Frag;\n"
    "void main() {\n"
    "  vec2 d = gl_PointCoord * 2.0 - 1.0;\n"
    "  if (dot(d, d) > 1.0) discard;\n"
    "  Frag = vec4(vColor.rgb, 1.0);\n"
    "}\n";

static const char* vsSplat =
    "#version 330 core\n"
    "layout(location = 0) in uint aIndex;\n"
    "uniform samplerBuffer uSplats;\n"
    "uniform mat4 uView, uProj, uModel;\n"
    "uniform vec2 uViewport, uFocal, uHeightRange;\n"
    "uniform int uOrtho, uColorMode;\n"
    "uniform float uScale, uCutoff;\n"
    "uniform vec3 uUniformColor;\n"
    "out vec4 vColor; out vec2 vPos;\n"
    "%s"
    "void main() {\n"
    "  int i = int(aIndex) * 4;\n"
    "  vec4 t0 = texelFetch(uSplats, i);\n"
    "  vec4 t1 = texelFetch(uSplats, i + 1);\n"
    "  vec4 t2 = texelFetch(uSplats, i + 2);\n"
    "  vec4 t3 = texelFetch(uSplats, i + 3);\n"
    "  vec3 p = t0.xyz; float opacity = t0.w;\n"
    "  vec2 corner = vec2(((gl_VertexID & 1) == 0) ? -1.0 : 1.0, ((gl_VertexID & 2) == 0) ? -1.0 : 1.0);\n"
    "  vec4 cam = uView * vec4(p, 1.0);\n"
    "  vec4 clip = uProj * cam;\n"
    "  float lim = 1.3 * clip.w;\n"
    "  bool behind = (uOrtho == 0) && (cam.z > -1e-5);\n"
    "  if (opacity < uCutoff || behind || clip.x < -lim || clip.x > lim || clip.y < -lim || clip.y > lim) { gl_Position = vec4(0.0, 0.0, 2.0, 1.0); vPos = vec2(0.0); vColor = vec4(0.0); return; }\n"
    "  mat3 S = mat3(t1.w, t2.x, t2.y,  t2.x, t2.z, t2.w,  t2.y, t2.w, t3.x);\n"
    "  mat3 W = mat3(uView);\n"
    "  mat3 J;\n"
    "  if (uOrtho == 1) J = mat3(uFocal.x, 0.0, 0.0,  0.0, uFocal.y, 0.0,  0.0, 0.0, 0.0);\n"
    "  else { float iz = 1.0 / cam.z; J = mat3(uFocal.x * iz, 0.0, 0.0,  0.0, uFocal.y * iz, 0.0,  -uFocal.x * cam.x * iz * iz, -uFocal.y * cam.y * iz * iz, 0.0); }\n"
    "  mat3 T = J * W;\n"
    "  mat3 C = T * S * transpose(T);\n"
    "  float s2 = uScale * uScale;\n"
    "  float a = C[0][0] * s2 + 0.3, b = C[0][1] * s2, d = C[1][1] * s2 + 0.3;\n"
    "  float mid = 0.5 * (a + d);\n"
    "  float rad = length(vec2(0.5 * (a - d), b));\n"
    "  float l1 = mid + rad, l2 = max(mid - rad, 0.02);\n"
    "  vec2 dir = (abs(b) < 1e-7) ? ((a >= d) ? vec2(1.0, 0.0) : vec2(0.0, 1.0)) : normalize(vec2(b, l1 - a));\n"
    "  float r1 = min(3.0 * sqrt(l1), 2048.0), r2 = min(3.0 * sqrt(l2), 2048.0);\n"
    "  vec2 offs = corner.x * r1 * dir + corner.y * r2 * vec2(-dir.y, dir.x);\n"
    "  vec2 ndc = clip.xy / clip.w + offs * 2.0 / uViewport;\n"
    "  gl_Position = vec4(ndc * clip.w, clip.z, clip.w);\n"
    "  vPos = corner * 3.0;\n"
    "  vec3 c = t1.rgb;\n"
    "  if (uColorMode == 1) { float wy = (uModel * vec4(p, 1.0)).y; c = heightColor((wy - uHeightRange.x) / max(uHeightRange.y - uHeightRange.x, 1e-9)); }\n"
    "  else if (uColorMode == 2) { c = vec3(0.8); }\n"
    "  else if (uColorMode == 3) { c = uUniformColor; }\n"
    "  vColor = vec4(c, opacity);\n"
    "}\n";

static const char* fsSplat =
    "#version 330 core\n"
    "in vec4 vColor; in vec2 vPos; out vec4 Frag;\n"
    "void main() {\n"
    "  float r2 = dot(vPos, vPos);\n"
    "  if (r2 > 9.0) discard;\n"
    "  float alpha = min(0.99, vColor.a * exp(-0.5 * r2));\n"
    "  if (alpha < 1.0 / 255.0) discard;\n"
    "  Frag = vec4(vColor.rgb * alpha, alpha);\n"
    "}\n";

static const char* vsLines =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "out vec4 vColor;\n"
    "void main() { gl_Position = uMVP * vec4(aPos, 1.0); vColor = aColor; }\n";

static const char* fsLines =
    "#version 330 core\n"
    "in vec4 vColor; out vec4 Frag;\n"
    "void main() { Frag = vec4(vColor.rgb * vColor.a, vColor.a); }\n";

static const char* vsComp =
    "#version 330 core\n"
    "void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

static const char* fsComp =
    "#version 330 core\n"
    "out vec4 Frag;\n"
    "uniform sampler2D uColor, uDepth;\n"
    "uniform vec2 uSize, uNearFar;\n"
    "uniform int uBg, uEdl, uOrtho;\n"
    "uniform float uEdlStrength, uEdlRadius;\n"
    "float linDepth(float d) {\n"
    "  float n = uNearFar.x, f = uNearFar.y;\n"
    "  if (uOrtho == 1) return n + d * (f - n);\n"
    "  float z = d * 2.0 - 1.0;\n"
    "  return 2.0 * n * f / (f + n - z * (f - n));\n"
    "}\n"
    "void main() {\n"
    "  vec2 uv = gl_FragCoord.xy / uSize;\n"
    "  vec4 c = texture(uColor, uv);\n"
    "  vec3 bg;\n"
    "  if (uBg == 0) { float t = uv.y; vec2 q = uv - 0.5; bg = mix(vec3(0.075, 0.08, 0.10), vec3(0.17, 0.18, 0.22), t) * (1.0 - 0.35 * dot(q, q)); }\n"
    "  else if (uBg == 1) bg = vec3(0.0);\n"
    "  else if (uBg == 2) bg = vec3(0.42, 0.43, 0.45);\n"
    "  else bg = vec3(1.0);\n"
    "  float shade = 1.0;\n"
    "  if (uEdl == 1) {\n"
    "    float d = texture(uDepth, uv).r;\n"
    "    if (d < 1.0) {\n"
    "      float zc = log2(max(linDepth(d), 1e-7));\n"
    "      float sum = 0.0;\n"
    "      vec2 px = uEdlRadius / uSize;\n"
    "      for (int k = 0; k < 8; k++) {\n"
    "        float ang = float(k) * 0.78539816;\n"
    "        vec2 o = vec2(cos(ang), sin(ang)) * px;\n"
    "        float dn = texture(uDepth, uv + o).r;\n"
    "        float zn = (dn < 1.0) ? log2(max(linDepth(dn), 1e-7)) : 1e9;\n"
    "        sum += max(0.0, zc - zn);\n"
    "      }\n"
    "      shade = exp(-sum / 8.0 * 300.0 * uEdlStrength);\n"
    "    }\n"
    "  }\n"
    "  Frag = vec4(c.rgb * shade + bg * (1.0 - c.a), 1.0);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, NULL);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(id, sizeof log, NULL, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

static GLuint create_program(const char* vs, const char* fs) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static char* format_shader(const char* tmpl, const char* funcs) {
    size_t len = strlen(tmpl) + strlen(funcs) + 1;
    char* out = (char*)malloc(len);
    snprintf(out, len, tmpl, funcs);
    return out;
}

/* ------------------------------------------------------------------ */
/* settings / info                                                    */
/* ------------------------------------------------------------------ */

void ply_settings_default(PlySettings* s) {
    memset(s, 0, sizeof *s);
    s->renderMode = PLY_MODE_POINTS;
    s->pointSize = 1.0f;
    s->splatScale = 1.0f;
    s->opacityCutoff = 0.0f;
    s->colorMode = PLY_COLOR_RGB;
    s->edl = 1;
    s->edlStrength = 0.8f;
    s->background = PLY_BG_STUDIO;
    s->showGrid = 1;
    s->showAxes = 1;
    s->ortho = 0;
    s->fov = 50.0f;
    s->upAxis = PLY_UP_POS_Y;
    s->shading = 1;
}

PlySettings* ply_settings(void) { return &gSettings; }
PlyFrameStats* ply_stats(void) { return &gStats; }
const PlyInfo* ply_info(void) { return gCloud ? &gCloud->info : &gInfoEmpty; }

/* ------------------------------------------------------------------ */
/* scene setup                                                        */
/* ------------------------------------------------------------------ */

static void update_scene_bounds(void) {
    build_up_model(gSettings.upAxis, upModel);
    if (!gCloud) {
        sceneCenter[0] = sceneCenter[1] = sceneCenter[2] = 0.f;
        sceneRadius = 5.f;
        memcpy(fullCenter, sceneCenter, 12);
        fullRadius = 5.f;
        heightMin = -1.f; heightMax = 1.f;
        return;
    }
    float rmin[3], rmax[3], fmin[3], fmax[3];
    transform_box(upModel, gCloud->rmin, gCloud->rmax, rmin, rmax);
    transform_box(upModel, gCloud->info.bmin, gCloud->info.bmax, fmin, fmax);
    float rr = 0.f, fr = 0.f;
    for (int a = 0; a < 3; a++) {
        sceneCenter[a] = 0.5f * (rmin[a] + rmax[a]);
        fullCenter[a] = 0.5f * (fmin[a] + fmax[a]);
        float e = 0.5f * (rmax[a] - rmin[a]); rr += e * e;
        float g = 0.5f * (fmax[a] - fmin[a]); fr += g * g;
    }
    sceneRadius = sqrtf(rr);
    fullRadius = sqrtf(fr);
    if (sceneRadius < 1e-6f) sceneRadius = 1.f;
    if (fullRadius < sceneRadius) fullRadius = sceneRadius;
    heightMin = rmin[1]; heightMax = rmax[1];
    if (heightMax - heightMin < 1e-9f) heightMax = heightMin + 1.f;
}

static void build_grid(void) {
    float half, step, y;
    if (gCloud) {
        float rmin[3], rmax[3];
        transform_box(upModel, gCloud->rmin, gCloud->rmax, rmin, rmax);
        half = sceneRadius * 1.6f;
        step = nice_step(half * 2.f / 12.f);
        half = step * ceilf(half / step);
        y = rmin[1] - sceneRadius * 0.01f;
    } else {
        half = 6.f; step = 1.f; y = 0.f;
    }
    int linesPerAxis = (int)(2.f * half / step + 0.5f) + 1;
    int maxVerts = linesPerAxis * 4 + 8;
    LineVert* v = (LineVert*)malloc((size_t)maxVerts * sizeof(LineVert));
    int n = 0;
    for (int k = 0; k < linesPerAxis; k++) {
        float t = -half + step * (float)k;
        int major = (k % 5) == 0;
        float a = major ? 0.30f : 0.16f;
        float cr = 0.62f, cg = 0.66f, cb = 0.74f;
        if (fabsf(t) < step * 0.01f) { a = 0.55f; }
        v[n++] = (LineVert){ t, y, -half, cr, cg, cb, a };
        v[n++] = (LineVert){ t, y,  half, cr, cg, cb, a };
        v[n++] = (LineVert){ -half, y, t, cr, cg, cb, a };
        v[n++] = (LineVert){  half, y, t, cr, cg, cb, a };
    }
    gridVerts = n;
    glBindVertexArray(vaoLines);
    glBindBuffer(GL_ARRAY_BUFFER, vboGrid);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(LineVert)), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    free(v);
}

static void wait_for_sort(void) {
    while (atomic_load(&sortBusy) && !atomic_load(&sortDone)) usleep(500);
    atomic_store(&sortDone, 0);
    atomic_store(&sortBusy, 0);
    sortValid = 0;
}

static void release_cloud_gpu(void) {
    wait_for_sort();
    free(idxFront); free(idxBack);
    idxFront = idxBack = NULL;
    if (tboReady) {
        glBindBuffer(GL_TEXTURE_BUFFER, tboBuf);
        glBufferData(GL_TEXTURE_BUFFER, 0, NULL, GL_STATIC_DRAW);
        tboReady = 0;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vboPoints);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vboSplatIdx);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static int build_tbo(void) {
    if (!gCloud || tboReady) return tboReady;
    long n = gCloud->n;
    if ((long)maxTexBufferTexels < n * 4) { gStats.splatCapable = 0; return 0; }
    float* buf = (float*)malloc((size_t)n * 16 * sizeof(float));
    if (!buf) { gStats.splatCapable = 0; return 0; }
    float iso = gCloud->info.spacing * 0.55f;
    float isoVar = iso * iso;
    for (long i = 0; i < n; i++) {
        float* t = buf + i * 16;
        t[0] = gCloud->pos[i*3]; t[1] = gCloud->pos[i*3+1]; t[2] = gCloud->pos[i*3+2];
        t[3] = gCloud->rgba[i*4+3] / 255.f;
        t[4] = gCloud->rgba[i*4] / 255.f; t[5] = gCloud->rgba[i*4+1] / 255.f; t[6] = gCloud->rgba[i*4+2] / 255.f;
        if (gCloud->cov) {
            const float* cv = &gCloud->cov[i*6];
            t[7] = cv[0]; t[8] = cv[1]; t[9] = cv[2]; t[10] = cv[3]; t[11] = cv[4]; t[12] = cv[5];
        } else {
            t[7] = isoVar; t[8] = 0; t[9] = 0; t[10] = isoVar; t[11] = 0; t[12] = isoVar;
        }
        t[13] = t[14] = t[15] = 0.f;
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tboBuf);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(n * 16 * sizeof(float)), buf, GL_STATIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, tboTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tboBuf);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    free(buf);
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) { gStats.splatCapable = 0; return 0; }
    tboReady = 1;
    return 1;
}

void ply_gl_set_cloud(PlyCloud* c) {
    release_cloud_gpu();
    ply_cloud_free(gCloud);
    gCloud = c;
    if (gCloud) {
        long n = gCloud->n;
        unsigned char* buf = (unsigned char*)malloc((size_t)n * 20);
        for (long i = 0; i < n; i++) {
            memcpy(buf + i*20, &gCloud->pos[i*3], 12);
            memcpy(buf + i*20 + 12, &gCloud->rgba[i*4], 4);
            if (gCloud->nrm) memcpy(buf + i*20 + 16, &gCloud->nrm[i*4], 4);
            else memset(buf + i*20 + 16, 0, 4);
        }
        glBindVertexArray(vaoPoints);
        glBindBuffer(GL_ARRAY_BUFFER, vboPoints);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 20), buf, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 20, (void*)12);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_BYTE, GL_TRUE, 20, (void*)16);
        glBindVertexArray(0);
        free(buf);

        idxFront = (uint32_t*)malloc((size_t)n * sizeof(uint32_t));
        idxBack = (uint32_t*)malloc((size_t)n * sizeof(uint32_t));
        for (long i = 0; i < n; i++) idxFront[i] = (uint32_t)i;
        glBindVertexArray(vaoSplat);
        glBindBuffer(GL_ARRAY_BUFFER, vboSplatIdx);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(uint32_t)), idxFront, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(uint32_t), (void*)0);
        glVertexAttribDivisor(0, 1);
        glBindVertexArray(0);
        gStats.splatCapable = ((long)maxTexBufferTexels >= n * 4);

        gSettings.renderMode = (gCloud->info.isSplat && gStats.splatCapable) ? PLY_MODE_SPLATS : PLY_MODE_POINTS;
        if (!gCloud->info.hasColor && gSettings.colorMode == PLY_COLOR_RGB) gSettings.colorMode = PLY_COLOR_HEIGHT;
        if (gCloud->info.hasColor && gSettings.colorMode == PLY_COLOR_HEIGHT) gSettings.colorMode = PLY_COLOR_RGB;
    }
    update_scene_bounds();
    build_grid();
    ply_cam_reset();
}

void ply_gl_clear(void) { ply_gl_set_cloud(NULL); }

int ply_gl_init(void) {
    char* vsP = format_shader(vsPoints, kColorFuncs);
    char* vsS = format_shader(vsSplat, kColorFuncs);
    gPP.prog = create_program(vsP, fsPoints);
    gSP.prog = create_program(vsS, fsSplat);
    free(vsP); free(vsS);
    gLP.prog = create_program(vsLines, fsLines);
    gCP.prog = create_program(vsComp, fsComp);
    if (!gPP.prog || !gSP.prog || !gLP.prog || !gCP.prog) return 0;

#define U(prog, s, name) s.name = glGetUniformLocation(prog, #name)
    U(gPP.prog, gPP, uView); U(gPP.prog, gPP, uProj); U(gPP.prog, gPP, uModel); U(gPP.prog, gPP, uRadius);
    U(gPP.prog, gPP, uProjScale); U(gPP.prog, gPP, uOrtho); U(gPP.prog, gPP, uMinPx); U(gPP.prog, gPP, uMaxPx);
    U(gPP.prog, gPP, uCutoff); U(gPP.prog, gPP, uColorMode); U(gPP.prog, gPP, uHeightRange); U(gPP.prog, gPP, uUniformColor);
    U(gPP.prog, gPP, uShading); U(gPP.prog, gPP, uHasNormals); U(gPP.prog, gPP, uPick);
    U(gSP.prog, gSP, uSplats); U(gSP.prog, gSP, uView); U(gSP.prog, gSP, uProj); U(gSP.prog, gSP, uModel);
    U(gSP.prog, gSP, uViewport); U(gSP.prog, gSP, uFocal); U(gSP.prog, gSP, uOrtho); U(gSP.prog, gSP, uScale);
    U(gSP.prog, gSP, uCutoff); U(gSP.prog, gSP, uColorMode); U(gSP.prog, gSP, uHeightRange); U(gSP.prog, gSP, uUniformColor);
    U(gLP.prog, gLP, uMVP);
    U(gCP.prog, gCP, uColor); U(gCP.prog, gCP, uDepth); U(gCP.prog, gCP, uSize); U(gCP.prog, gCP, uBg);
    U(gCP.prog, gCP, uEdl); U(gCP.prog, gCP, uEdlStrength); U(gCP.prog, gCP, uEdlRadius); U(gCP.prog, gCP, uNearFar); U(gCP.prog, gCP, uOrtho);
#undef U

    glGenVertexArrays(1, &vaoPoints);
    glGenVertexArrays(1, &vaoSplat);
    glGenVertexArrays(1, &vaoLines);
    glGenVertexArrays(1, &vaoEmpty);
    glGenBuffers(1, &vboPoints);
    glGenBuffers(1, &vboSplatIdx);
    glGenBuffers(1, &vboGrid);
    glGenBuffers(1, &vboOverlay);
    glGenBuffers(1, &tboBuf);
    glGenTextures(1, &tboTex);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexBufferTexels);

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texColor);
    glGenTextures(1, &texDepth);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_DITHER);
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) fprintf(stderr, "GL init error 0x%x\n", e);
    gStats.splatCapable = 1;
    update_scene_bounds();
    build_grid();
    ply_cam_reset();
    return 1;
}

void ply_gl_shutdown(void) {
    release_cloud_gpu();
    ply_cloud_free(gCloud);
    gCloud = NULL;
}

static void ensure_fbo(int w, int h) {
    if (w == fboW && h == fboH) return;
    fboW = w; fboH = h;
    glBindTexture(GL_TEXTURE_2D, texColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, texDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texDepth, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ------------------------------------------------------------------ */
/* camera                                                             */
/* ------------------------------------------------------------------ */

static void cam_dir(float* d) {
    float yaw = camYaw * PI_F / 180.f, pitch = camPitch * PI_F / 180.f;
    d[0] = cosf(pitch) * sinf(yaw);
    d[1] = sinf(pitch);
    d[2] = cosf(pitch) * cosf(yaw);
}

static void clamp_pitch(void) {
    if (camPitch > 89.5f) camPitch = 89.5f;
    if (camPitch < -89.5f) camPitch = -89.5f;
}

static void update_frame(int w, int h, float ps) {
    gF.w = w; gF.h = h; gF.ps = ps;
    gF.viewHPoints = (float)h / ps;
    gF.aspect = (float)w / (float)h;
    gF.ortho = gSettings.ortho;
    memcpy(gF.model, upModel, 64);
    float d[3]; cam_dir(d);
    gF.camPos[0] = camTarget[0] + d[0] * camDist;
    gF.camPos[1] = camTarget[1] + d[1] * camDist;
    gF.camPos[2] = camTarget[2] + d[2] * camDist;
    float upHint[3] = { 0, 1, 0 };
    m4_lookat(gF.look, gF.camPos, camTarget, upHint, gF.fwd, gF.right, gF.up);
    m4_mul(gF.view, gF.look, gF.model);
    gF.tanHalf = tanf(gSettings.fov * PI_F / 360.f);
    gF.hh = camDist * gF.tanHalf;
    gF.hw = gF.hh * gF.aspect;
    float toC[3] = { fullCenter[0] - gF.camPos[0], fullCenter[1] - gF.camPos[1], fullCenter[2] - gF.camPos[2] };
    float dc = v3_dot(toC, gF.fwd);
    float R = fullRadius * 1.1f;
    if (gF.ortho) {
        gF.nearZ = dc - R - camDist * 0.01f;
        gF.farZ = dc + R + camDist * 0.01f;
        if (gF.farZ - gF.nearZ < 1e-6f) gF.farZ = gF.nearZ + 1.f;
        m4_ortho(gF.proj, -gF.hw, gF.hw, -gF.hh, gF.hh, gF.nearZ, gF.farZ);
    } else {
        gF.farZ = dc + R;
        if (gF.farZ < camDist * 1.5f) gF.farZ = camDist * 1.5f;
        gF.nearZ = dc - R;
        if (gF.nearZ > camDist * 0.5f) gF.nearZ = camDist * 0.5f;
        float minNear = gF.farZ * 2e-5f;
        if (gF.nearZ < minNear) gF.nearZ = minNear;
        m4_perspective(gF.proj, gSettings.fov, gF.aspect, gF.nearZ, gF.farZ);
    }
    gF.valid = 1;
}

void ply_cam_orbit(float dx, float dy) {
    camYaw -= dx * 0.35f;
    camPitch -= dy * 0.35f;
    clamp_pitch();
    while (camYaw > 180.f) camYaw -= 360.f;
    while (camYaw < -180.f) camYaw += 360.f;
}

void ply_cam_pan(float dx, float dy) {
    if (!gF.valid) return;
    float k = 2.f * gF.hh / gF.viewHPoints;
    for (int a = 0; a < 3; a++) camTarget[a] -= gF.right[a] * dx * k + gF.up[a] * dy * k;
}

void ply_cam_zoom(float factor, float nx, float ny, int useCursor) {
    if (factor <= 0.f) return;
    float minD = sceneRadius * 0.002f, maxD = sceneRadius * 200.f;
    float newD = camDist * factor;
    if (newD < minD) newD = minD;
    if (newD > maxD) newD = maxD;
    factor = newD / camDist;
    if (useCursor && gF.valid) {
        float ox = (nx * 2.f - 1.f) * gF.hw, oy = (ny * 2.f - 1.f) * gF.hh;
        for (int a = 0; a < 3; a++) {
            float p = camTarget[a] + gF.right[a] * ox + gF.up[a] * oy;
            camTarget[a] = p + (camTarget[a] - p) * factor;
        }
    }
    camDist = newD;
}

void ply_cam_fit(void) {
    memcpy(camTarget, sceneCenter, 12);
    float tanHalf = tanf(gSettings.fov * PI_F / 360.f);
    float sinHalf = sinf(gSettings.fov * PI_F / 360.f);
    float d = gSettings.ortho ? sceneRadius / tanHalf : sceneRadius / sinHalf;
    camDist = d * 1.05f;
}

void ply_cam_view(int preset) {
    switch (preset) {
        case PLY_VIEW_FRONT:  camYaw = 0.f;    camPitch = 0.f; break;
        case PLY_VIEW_BACK:   camYaw = 180.f;  camPitch = 0.f; break;
        case PLY_VIEW_LEFT:   camYaw = -90.f;  camPitch = 0.f; break;
        case PLY_VIEW_RIGHT:  camYaw = 90.f;   camPitch = 0.f; break;
        case PLY_VIEW_TOP:    camYaw = 0.f;    camPitch = 89.5f; break;
        case PLY_VIEW_BOTTOM: camYaw = 0.f;    camPitch = -89.5f; break;
        default:              camYaw = 40.f;   camPitch = 25.f; break;
    }
}

void ply_cam_reset(void) {
    if (gCloud) ply_cam_view(PLY_VIEW_FRONT); else ply_cam_view(PLY_VIEW_ISO);
    ply_cam_fit();
}

void ply_cam_set_up_axis(int upAxis) {
    gSettings.upAxis = upAxis;
    update_scene_bounds();
    build_grid();
    ply_cam_fit();
}

/* ------------------------------------------------------------------ */
/* sorting                                                            */
/* ------------------------------------------------------------------ */

static void* sort_worker(void* arg) {
    (void)arg;
    PlyCloud* c = gCloud;
    long n = c->n;
    const float* p = c->pos;
    float r0 = sortRow[0], r1 = sortRow[1], r2 = sortRow[2];
    uint16_t* keys = (uint16_t*)malloc((size_t)n * sizeof(uint16_t));
    uint32_t* counts = (uint32_t*)calloc(65537, sizeof(uint32_t));
    if (!keys || !counts) {
        free(keys); free(counts);
        for (long i = 0; i < n; i++) idxBack[i] = (uint32_t)i;
        atomic_store(&sortDone, 1);
        return NULL;
    }
    float zmin = 1e30f, zmax = -1e30f;
    for (long i = 0; i < n; i++) {
        float z = r0 * p[i*3] + r1 * p[i*3+1] + r2 * p[i*3+2];
        if (z < zmin) zmin = z;
        if (z > zmax) zmax = z;
    }
    float range = zmax - zmin;
    float scale = range > 1e-20f ? 65535.f / range : 0.f;
    for (long i = 0; i < n; i++) {
        float z = r0 * p[i*3] + r1 * p[i*3+1] + r2 * p[i*3+2];
        int k = (int)((z - zmin) * scale);
        if (k < 0) k = 0; if (k > 65535) k = 65535;
        keys[i] = (uint16_t)k;
        counts[k + 1]++;
    }
    for (int k = 0; k < 65536; k++) counts[k + 1] += counts[k];
    for (long i = 0; i < n; i++) idxBack[counts[keys[i]]++] = (uint32_t)i;
    free(keys); free(counts);
    atomic_store(&sortDone, 1);
    return NULL;
}

static void kick_sort_if_needed(void) {
    if (!gCloud || atomic_load(&sortBusy)) return;
    float row[3] = { gF.view[2], gF.view[6], gF.view[10] };
    if (sortValid) {
        float d = row[0]*sortedRow[0] + row[1]*sortedRow[1] + row[2]*sortedRow[2];
        if (1.f - d < 1e-5f) return;
    }
    memcpy(sortRow, row, 12);
    atomic_store(&sortDone, 0);
    atomic_store(&sortBusy, 1);
    sortStart = now_sec();
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, sort_worker, NULL) != 0) {
        atomic_store(&sortBusy, 0);
    }
    pthread_attr_destroy(&attr);
}

static void consume_sort(void) {
    if (!atomic_load(&sortDone)) return;
    uint32_t* t = idxFront; idxFront = idxBack; idxBack = t;
    memcpy(sortedRow, sortRow, 12);
    sortValid = 1;
    gStats.sortMs = (now_sec() - sortStart) * 1000.0;
    if (gCloud) {
        glBindBuffer(GL_ARRAY_BUFFER, vboSplatIdx);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((long)gCloud->n * sizeof(uint32_t)), NULL, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((long)gCloud->n * sizeof(uint32_t)), idxFront);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    atomic_store(&sortDone, 0);
    atomic_store(&sortBusy, 0);
}

int ply_needs_redraw(void) { return atomic_load(&sortDone) != 0; }

int ply_sort_settled(void) {
    if (!gCloud || !(gSettings.renderMode == PLY_MODE_SPLATS && gStats.splatCapable)) return 1;
    if (atomic_load(&sortBusy) || !sortValid) return 0;
    float row[3] = { gF.view[2], gF.view[6], gF.view[10] };
    float d = row[0]*sortedRow[0] + row[1]*sortedRow[1] + row[2]*sortedRow[2];
    return 1.f - d < 1e-5f;
}

/* ------------------------------------------------------------------ */
/* drawing                                                            */
/* ------------------------------------------------------------------ */

static void draw_points(int pick) {
    glUseProgram(gPP.prog);
    glUniformMatrix4fv(gPP.uView, 1, GL_FALSE, gF.view);
    glUniformMatrix4fv(gPP.uProj, 1, GL_FALSE, gF.proj);
    glUniformMatrix4fv(gPP.uModel, 1, GL_FALSE, gF.model);
    float radius = gCloud->info.spacing * 0.75f * gSettings.pointSize;
    float projScale = gF.ortho ? ((float)gF.h / (2.f * gF.hh)) : ((float)gF.h / (2.f * gF.tanHalf));
    glUniform1f(gPP.uRadius, radius);
    glUniform1f(gPP.uProjScale, projScale);
    glUniform1i(gPP.uOrtho, gF.ortho);
    glUniform1f(gPP.uMinPx, pick ? 3.f * gF.ps : 1.0f * gF.ps);
    glUniform1f(gPP.uMaxPx, 48.f * gF.ps);
    glUniform1f(gPP.uCutoff, gSettings.opacityCutoff);
    glUniform1i(gPP.uColorMode, gSettings.colorMode);
    glUniform2f(gPP.uHeightRange, heightMin, heightMax);
    glUniform3f(gPP.uUniformColor, 0.82f, 0.82f, 0.84f);
    glUniform1i(gPP.uShading, gSettings.shading);
    glUniform1i(gPP.uHasNormals, gCloud->info.hasNormals);
    glUniform1i(gPP.uPick, pick);
    glBindVertexArray(vaoPoints);
    glDrawArrays(GL_POINTS, 0, gCloud->n);
    glBindVertexArray(0);
}

static void draw_splats(void) {
    glUseProgram(gSP.prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, tboTex);
    glUniform1i(gSP.uSplats, 0);
    glUniformMatrix4fv(gSP.uView, 1, GL_FALSE, gF.view);
    glUniformMatrix4fv(gSP.uProj, 1, GL_FALSE, gF.proj);
    glUniformMatrix4fv(gSP.uModel, 1, GL_FALSE, gF.model);
    glUniform2f(gSP.uViewport, (float)gF.w, (float)gF.h);
    glUniform2f(gSP.uFocal, gF.proj[0] * (float)gF.w * 0.5f, gF.proj[5] * (float)gF.h * 0.5f);
    glUniform1i(gSP.uOrtho, gF.ortho);
    glUniform1f(gSP.uScale, gSettings.splatScale);
    glUniform1f(gSP.uCutoff, gSettings.opacityCutoff);
    glUniform1i(gSP.uColorMode, gSettings.colorMode);
    glUniform2f(gSP.uHeightRange, heightMin, heightMax);
    glUniform3f(gSP.uUniformColor, 0.82f, 0.82f, 0.84f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vaoSplat);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, gCloud->n);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

static void draw_grid(void) {
    float mvp[16];
    m4_mul(mvp, gF.proj, gF.look);
    glUseProgram(gLP.prog);
    glUniformMatrix4fv(gLP.uMVP, 1, GL_FALSE, mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vaoLines);
    glBindBuffer(GL_ARRAY_BUFFER, vboGrid);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void*)(3 * sizeof(float)));
    glDrawArrays(GL_LINES, 0, gridVerts);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

static void push_quad(LineVert* v, int* n, float x0, float y0, float x1, float y1, float halfW, float r, float g, float b, float a) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-6f) { dx = 1; dy = 0; len = 1; }
    float nx = -dy / len * halfW, ny = dx / len * halfW;
    LineVert p0 = { x0 + nx, y0 + ny, 0, r, g, b, a }, p1 = { x0 - nx, y0 - ny, 0, r, g, b, a };
    LineVert p2 = { x1 - nx, y1 - ny, 0, r, g, b, a }, p3 = { x1 + nx, y1 + ny, 0, r, g, b, a };
    v[(*n)++] = p0; v[(*n)++] = p1; v[(*n)++] = p2;
    v[(*n)++] = p0; v[(*n)++] = p2; v[(*n)++] = p3;
}

static void push_disc(LineVert* v, int* n, float cx, float cy, float rad, float r, float g, float b, float a) {
    const int seg = 14;
    for (int k = 0; k < seg; k++) {
        float a0 = (float)k / seg * 2.f * PI_F, a1 = (float)(k + 1) / seg * 2.f * PI_F;
        v[(*n)++] = (LineVert){ cx, cy, 0, r, g, b, a };
        v[(*n)++] = (LineVert){ cx + cosf(a0) * rad, cy + sinf(a0) * rad, 0, r, g, b, a };
        v[(*n)++] = (LineVert){ cx + cosf(a1) * rad, cy + sinf(a1) * rad, 0, r, g, b, a };
    }
}

static void draw_gizmo(void) {
    float sizePx = 84.f * gF.ps;
    float margin = 14.f * gF.ps;
    glViewport((GLint)margin, (GLint)margin, (GLsizei)sizePx, (GLsizei)sizePx);
    float cols[3][3] = { {0.93f,0.32f,0.32f}, {0.42f,0.86f,0.36f}, {0.36f,0.56f,0.98f} };
    float proj2[3][3];
    for (int a = 0; a < 3; a++) {
        float px = gF.model[0 + a*4], py = gF.model[1 + a*4], pz = gF.model[2 + a*4];
        proj2[a][0] = gF.look[0] * px + gF.look[4] * py + gF.look[8] * pz;
        proj2[a][1] = gF.look[1] * px + gF.look[5] * py + gF.look[9] * pz;
        proj2[a][2] = gF.look[2] * px + gF.look[6] * py + gF.look[10] * pz;
    }
    int order[3] = { 0, 1, 2 };
    for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) if (proj2[order[j]][2] < proj2[order[i]][2]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    LineVert buf[3 * (6 + 42) + 42];
    int n = 0;
    float L = 0.68f, halfW = 2.2f / sizePx * 2.f, tip = 0.13f;
    push_disc(buf, &n, 0, 0, 0.07f, 0.85f, 0.85f, 0.9f, 0.9f);
    for (int k = 0; k < 3; k++) {
        int a = order[k];
        float ex = proj2[a][0] * L, ey = proj2[a][1] * L;
        float depthFade = 0.55f + 0.45f * (proj2[a][2] * 0.5f + 0.5f);
        push_quad(buf, &n, 0, 0, ex, ey, halfW, cols[a][0], cols[a][1], cols[a][2], depthFade);
        push_disc(buf, &n, ex, ey, tip, cols[a][0], cols[a][1], cols[a][2], depthFade);
    }
    float ident[16]; m4_identity(ident);
    glUseProgram(gLP.prog);
    glUniformMatrix4fv(gLP.uMVP, 1, GL_FALSE, ident);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vaoLines);
    glBindBuffer(GL_ARRAY_BUFFER, vboOverlay);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(LineVert)), buf, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void*)(3 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, n);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glViewport(0, 0, gF.w, gF.h);
}

static void composite(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, gF.w, gF.h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(gCP.prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texDepth);
    glUniform1i(gCP.uColor, 0);
    glUniform1i(gCP.uDepth, 1);
    glUniform2f(gCP.uSize, (float)gF.w, (float)gF.h);
    glUniform1i(gCP.uBg, gSettings.background);
    int edlOn = gSettings.edl && gCloud && !(gSettings.renderMode == PLY_MODE_SPLATS && gStats.splatCapable);
    glUniform1i(gCP.uEdl, edlOn);
    glUniform1f(gCP.uEdlStrength, gSettings.edlStrength);
    glUniform1f(gCP.uEdlRadius, 1.4f * gF.ps);
    glUniform2f(gCP.uNearFar, gF.nearZ, gF.farZ);
    glUniform1i(gCP.uOrtho, gF.ortho);
    glBindVertexArray(vaoEmpty);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ply_draw(int w, int h, float pixelScale) {
    double t0 = now_sec();
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (pixelScale <= 0.f) pixelScale = 1.f;
    ensure_fbo(w, h);
    update_frame(w, h, pixelScale);
    consume_sort();

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    if (gSettings.showGrid) draw_grid();

    if (gCloud) {
        int splat = gSettings.renderMode == PLY_MODE_SPLATS && gStats.splatCapable;
        if (splat && !tboReady) splat = build_tbo();
        if (splat) {
            kick_sort_if_needed();
            draw_splats();
        } else {
            draw_points(0);
        }
    }

    composite();
    if (gSettings.showAxes) draw_gizmo();
    glEnable(GL_DEPTH_TEST);
    gStats.drawMs = (now_sec() - t0) * 1000.0;
}

int ply_read_pixels(unsigned char* rgba, int width, int height) {
    if (!rgba || width != gF.w || height != gF.h) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) fprintf(stderr, "GL error before read 0x%x\n", e);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) fprintf(stderr, "glReadPixels error 0x%x\n", e);
    return e == GL_NO_ERROR;
}

int ply_cam_pick_pivot(float nx, float ny) {
    if (!gCloud || !gF.valid || fboW < 1) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, gF.w, gF.h);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    draw_points(1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    int px = (int)(nx * gF.w), py = (int)(ny * gF.h);
    const int R = 5;
    int x0 = px - R, y0 = py - R, sz = 2 * R + 1;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x0 + sz > gF.w) x0 = gF.w - sz; if (y0 + sz > gF.h) y0 = gF.h - sz;
    if (x0 < 0 || y0 < 0) return 0;
    float depth[121];
    glReadPixels(x0, y0, sz, sz, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    float best = 1.f; int bi = -1;
    for (int i = 0; i < sz * sz; i++) if (depth[i] < best) { best = depth[i]; bi = i; }
    if (bi < 0 || best >= 1.f) return 0;
    float sx = ((float)(x0 + bi % sz) + 0.5f) / (float)gF.w * 2.f - 1.f;
    float sy = ((float)(y0 + bi / sz) + 0.5f) / (float)gF.h * 2.f - 1.f;
    float zEye, ox, oy;
    if (gF.ortho) {
        zEye = gF.nearZ + best * (gF.farZ - gF.nearZ);
        ox = sx * gF.hw; oy = sy * gF.hh;
    } else {
        float zn = best * 2.f - 1.f;
        zEye = 2.f * gF.nearZ * gF.farZ / (gF.farZ + gF.nearZ - zn * (gF.farZ - gF.nearZ));
        ox = sx * zEye * gF.tanHalf * gF.aspect;
        oy = sy * zEye * gF.tanHalf;
    }
    float p[3];
    for (int a = 0; a < 3; a++) p[a] = gF.camPos[a] + gF.fwd[a] * zEye + gF.right[a] * ox + gF.up[a] * oy;
    float d[3] = { gF.camPos[0] - p[0], gF.camPos[1] - p[1], gF.camPos[2] - p[2] };
    float dist = sqrtf(v3_dot(d, d));
    if (dist < sceneRadius * 0.001f) return 0;
    memcpy(camTarget, p, 12);
    camDist = dist;
    return 1;
}
