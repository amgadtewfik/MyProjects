#ifndef PLY_PNG_WRITE_H
#define PLY_PNG_WRITE_H

#ifdef __cplusplus
extern "C" {
#endif

int png_write_rgba(const char* path, const unsigned char* rgba, int w, int h, int flipY);

#ifdef __cplusplus
}
#endif

#endif
