// SPDX-FileCopyrightText: fabian "ryg" giesen
// SPDX-License-Identifier: MIT

// stb_dxt.h - v1.12 - DXT1/DXT5 compressor

#ifndef STB_INCLUDE_STB_DXT_H
#define STB_INCLUDE_STB_DXT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef STB_DXT_STATIC
#define STBDDEF static
#else
#define STBDDEF extern
#endif

// compression mode (bitflags)
#define STB_DXT_NORMAL 0
#define STB_DXT_DITHER 1 // use dithering. was always dubious, now deprecated. does nothing!
#define STB_DXT_HIGHQUAL                                                                           \
    2 // high quality mode, does two refinement steps instead of 1. ~30-40% slower.

STBDDEF void stb_compress_bc1_block(unsigned char* dest,
                                    const unsigned char* src_rgba_four_bytes_per_pixel, int alpha,
                                    int mode);

STBDDEF void stb_compress_bc3_block(unsigned char* dest, const unsigned char* src, int mode);

#define STB_COMPRESS_DXT_BLOCK

#ifdef __cplusplus
}
#endif
#endif // STB_INCLUDE_STB_DXT_H
