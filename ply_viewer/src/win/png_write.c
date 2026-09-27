#include "png_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long crc_table[256];
static int crc_ready = 0;

static void make_crc_table(void) {
    for (unsigned long n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320UL ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static unsigned long crc32_buf(const unsigned char* buf, size_t len, unsigned long crc) {
    if (!crc_ready) make_crc_table();
    crc ^= 0xffffffffUL;
    for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffUL;
}

static unsigned long adler32_buf(const unsigned char* buf, size_t len) {
    unsigned long a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void put_be32(unsigned char* p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static int write_chunk(FILE* f, const char* tag, const unsigned char* data, size_t len) {
    unsigned char hdr[8];
    put_be32(hdr, (unsigned long)len);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return 0;
    if (len && fwrite(data, 1, len, f) != len) return 0;
    unsigned long crc = crc32_buf((const unsigned char*)tag, 4, 0);
    if (len) crc = crc32_buf(data, len, crc);
    unsigned char tail[4];
    put_be32(tail, crc);
    return fwrite(tail, 1, 4, f) == 4;
}

int png_write_rgba(const char* path, const unsigned char* rgba, int w, int h, int flipY) {
    if (w <= 0 || h <= 0) return 0;

    size_t rawLen = (size_t)h * ((size_t)w * 4 + 1);
    unsigned char* raw = (unsigned char*)malloc(rawLen);
    if (!raw) return 0;
    for (int y = 0; y < h; y++) {
        int src = flipY ? (h - 1 - y) : y;
        unsigned char* dst = raw + (size_t)y * ((size_t)w * 4 + 1);
        dst[0] = 0;
        memcpy(dst + 1, rgba + (size_t)src * w * 4, (size_t)w * 4);
        for (int x = 0; x < w; x++) dst[1 + (size_t)x * 4 + 3] = 255;
    }

    size_t blocks = rawLen / 65535 + 1;
    size_t zLen = 2 + blocks * 5 + rawLen + 4;
    unsigned char* z = (unsigned char*)malloc(zLen);
    if (!z) { free(raw); return 0; }

    size_t zi = 0;
    z[zi++] = 0x78; z[zi++] = 0x01;
    size_t off = 0;
    while (off < rawLen) {
        size_t n = rawLen - off;
        if (n > 65535) n = 65535;
        int last = (off + n >= rawLen);
        z[zi++] = (unsigned char)(last ? 1 : 0);
        z[zi++] = (unsigned char)(n & 0xff);
        z[zi++] = (unsigned char)(n >> 8);
        z[zi++] = (unsigned char)(~n & 0xff);
        z[zi++] = (unsigned char)((~n >> 8) & 0xff);
        memcpy(z + zi, raw + off, n);
        zi += n;
        off += n;
    }
    put_be32(z + zi, adler32_buf(raw, rawLen));
    zi += 4;
    free(raw);

    FILE* f = fopen(path, "wb");
    if (!f) { free(z); return 0; }

    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    int ok = fwrite(sig, 1, 8, f) == 8;

    unsigned char ihdr[13];
    put_be32(ihdr, (unsigned long)w);
    put_be32(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    ok = ok && write_chunk(f, "IHDR", ihdr, sizeof ihdr);
    ok = ok && write_chunk(f, "IDAT", z, zi);
    ok = ok && write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(z);
    if (!ok) remove(path);
    return ok;
}
