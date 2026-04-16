#ifndef IMG_IO_H
#define IMG_IO_H

/* C99 single-header utilities for reading/writing whole byte buffers ("images").
 *
 * API:
 *   int load_img_from_path(char* path, unsigned char** img, size_t* img_size);
 *   int load_img_from_stdin(unsigned char** img, size_t* img_size);
 *   int write_img_to_path(char* path, unsigned char* img, size_t img_size);
 *   int write_img_to_stdout(unsigned char* img, size_t img_size);
 *   int destroy_img(unsigned char* img);
 *
 * Conventions:
 *   - Returns 0 on success; non-zero errno-like code on failure.
 *   - On success for load_*: *img is malloc'd and must be freed by destroy_img().
 *   - Binary-safe on Windows (stdin/stdout set to binary mode).
 *
 * Usage:
 *   In ONE .c file:
 *     #define IMG_IO_IMPLEMENTATION
 *     #include "img_io.h"
 *
 *   Elsewhere:
 *     #include "img_io.h"
 */

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

int load_img_from_path(char* path, unsigned char** img, size_t* img_size);
int load_img_from_stdin(unsigned char** img, size_t* img_size);
int write_img_to_path(char* path, unsigned char* img, size_t img_size);
int write_img_to_stdout(unsigned char* img, size_t img_size);
int img_destroy(unsigned char* img);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ===================== Implementation ===================== */
#ifdef IMG_IO_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
  #include <io.h>
  #include <fcntl.h>
  #ifndef STDIN_FILENO
    #define STDIN_FILENO  0
  #endif
  #ifndef STDOUT_FILENO
    #define STDOUT_FILENO 1
  #endif
  static int img_io__set_binmode_stdio(void) {
      /* Set binary mode; non-fatal if it fails (e.g., redirected handles). */
      (void)_setmode(STDIN_FILENO,  _O_BINARY);
      (void)_setmode(STDOUT_FILENO, _O_BINARY);
      return 0;
  }
#else
  static int img_io__set_binmode_stdio(void) { return 0; }
#endif

/* Read entire stream into a dynamically growing buffer */
static int img_io__read_stream_into_buffer(FILE* fp, unsigned char** out_buf, size_t* out_size) {
    const size_t CHUNK = 1u << 16; /* 64 KiB */
    unsigned char* buf = NULL;
    size_t cap = 0, len = 0;

    for (;;) {
        if (len + CHUNK > cap) {
            size_t new_cap = cap ? (cap * 2) : CHUNK;
            if (new_cap < len + CHUNK) new_cap = len + CHUNK;
            unsigned char* tmp = (unsigned char*)realloc(buf, new_cap ? new_cap : 1);
            if (!tmp) {
                int e = errno ? errno : ENOMEM;
                free(buf);
                return e;
            }
            buf = tmp;
            cap = new_cap;
        }
        size_t n = fread(buf + len, 1, CHUNK, fp);
        len += n;
        if (n < CHUNK) {
            if (ferror(fp)) {
                int e = errno ? errno : EIO;
                free(buf);
                return e;
            }
            break; /* EOF */
        }
    }

    /* Optional shrink-to-fit */
    if (len != cap) {
        unsigned char* tmp = (unsigned char*)realloc(buf, len ? len : 1);
        if (tmp) buf = tmp;
    }

    *out_buf = buf;
    *out_size = len;
    return 0;
}

/* Write entire buffer */
static int img_io__write_all(FILE* fp, const unsigned char* data, size_t size) {
    while (size) {
        size_t n = fwrite(data, 1, size, fp);
        if (n == 0) return errno ? errno : EIO;
        data += n;
        size -= n;
    }
    return 0;
}

int load_img_from_path(char* path, unsigned char** img, size_t* img_size) {
    if (!path || !img || !img_size) return EINVAL;
    FILE* fp = fopen(path, "rb");
    if (!fp) return errno ? errno : EIO;

    unsigned char* buf = NULL;
    size_t len = 0;
    int rc = img_io__read_stream_into_buffer(fp, &buf, &len);
    int saved_errno = errno;
    fclose(fp);
    if (rc != 0) {
        (void)saved_errno;
        return rc;
    }
    *img = buf;
    *img_size = len;
    return 0;
}

int load_img_from_stdin(unsigned char** img, size_t* img_size) {
    if (!img || !img_size) return EINVAL;
    (void)img_io__set_binmode_stdio();
    return img_io__read_stream_into_buffer(stdin, img, img_size);
}

int write_img_to_path(char* path, unsigned char* img, size_t img_size) {
    if (!path || (!img && img_size != 0)) return EINVAL;
    FILE* fp = fopen(path, "wb");
    if (!fp) return errno ? errno : EIO;

    int rc = (img_size ? img_io__write_all(fp, img, img_size) : 0);
    int saved_errno = errno;
    if (fclose(fp) != 0 && rc == 0) rc = saved_errno ? saved_errno : EIO;
    return rc;
}

int write_img_to_stdout(unsigned char* img, size_t img_size) {
    if (!img && img_size != 0) return EINVAL;
    (void)img_io__set_binmode_stdio();
    if (img_size == 0) return 0;
    return img_io__write_all(stdout, img, img_size);
}

int img_destroy(unsigned char* img) {
    /* free(NULL) is a no-op by C standard; safe. */
    free(img);
    return 0;
}

#endif /* IMG_IO_IMPLEMENTATION */
#endif /* IMG_IO_H */