#include "impy.h"
#include "private.h"

#include <string.h>

// Ported from SDL_image
/*
  SDL_image:  An example image loading library for use with SDL
  Copyright (C) 1997-2017 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#if 0
static bool is_targa(const uint8_t* buf, int nbytes);
static bool match_targa_ext(const char* file_ext);

// no magic cookie for targa...
static bool is_targa(const uint8_t* buf, int nbytes)
{
    return true;
}

static bool match_targa_ext(const char* file_ext)
{
    return (istricmp(file_ext,".tga")==0);
}
#endif


/*
 * A TGA loader for the SDL library
 * Supports: Reading 8, 15, 16, 24 and 32bpp images, with alpha or colourkey,
 *           uncompressed or RLE encoded.
 *
 * 2000-06-10 Mattias Engdegård <f91-men@nada.kth.se>: initial version
 * 2000-06-26 Mattias Engdegård <f91-men@nada.kth.se>: read greyscale TGAs
 * 2000-08-09 Mattias Engdegård <f91-men@nada.kth.se>: alpha inversion removed
 */

struct TGAheader {
    uint8_t infolen;      /* length of info field */
    uint8_t has_cmap;     /* 1 if image has colormap, 0 otherwise */
    uint8_t type;

    uint8_t cmap_start[2];    /* index of first colormap entry */
    uint8_t cmap_len[2];      /* number of entries in colormap */
    uint8_t cmap_bits;        /* bits per colormap entry */

    uint8_t yorigin[2];       /* image origin (ignored here) */
    uint8_t xorigin[2];
    uint8_t width[2];     /* image size */
    uint8_t height[2];
    uint8_t pixel_bits;       /* bits/pixel */
    uint8_t flags;
};

enum tga_type {
    TGA_TYPE_INDEXED = 1,
    TGA_TYPE_RGB = 2,
    TGA_TYPE_BW = 3,
    TGA_TYPE_RLE_INDEXED = 9,
    TGA_TYPE_RLE_RGB = 10,
    TGA_TYPE_RLE_BW = 11
};

#define TGA_INTERLEAVE_MASK 0xc0
#define TGA_INTERLEAVE_NONE 0x00
#define TGA_INTERLEAVE_2WAY 0x40
#define TGA_INTERLEAVE_4WAY 0x80

#define TGA_ORIGIN_MASK     0x30
#define TGA_ORIGIN_LEFT     0x00
#define TGA_ORIGIN_RIGHT    0x10
#define TGA_ORIGIN_LOWER    0x00
#define TGA_ORIGIN_UPPER    0x20

/* read/write unaligned little-endian 16-bit ints */
#define LE16(p) ((p)[0] + ((p)[1] << 8))
#define SETLE16(p, v) ((p)[0] = (v), (p)[1] = (v) >> 8)

/* Load a TGA type image from an im_in */
im_img* iread_targa_image( im_in* in, ImErr* err )
{
    struct TGAheader hdr;
    int rle = 0;
    int alpha = 0;
    int indexed = 0;
    int grey = 0;
    int ncols, w, h;
    im_img* img = 0;
    //uint32_t rmask, gmask, bmask, amask;
    uint8_t *dst;
    uint8_t *rowStart;
    int i;
    int bpp;
    uint32_t pixel;
    int count, rep;
    ImFmt fmt;

    *err = ERR_NONE;

    if (im_in_read(in, &hdr, sizeof(hdr)) != sizeof(hdr) ) {
        *err = im_in_eof(in) ? ERR_MALFORMED: ERR_FILE;
        goto cleanup;
    }

    ncols = LE16(hdr.cmap_len);
    switch(hdr.type) {
    case TGA_TYPE_RLE_INDEXED:
        rle = 1;
        /* fallthrough */
    case TGA_TYPE_INDEXED:
        if (!hdr.has_cmap || hdr.pixel_bits != 8 || ncols > 256) {
            *err = ERR_UNSUPPORTED;
            goto cleanup;
        }
        indexed = 1;
        break;

    case TGA_TYPE_RLE_RGB:
        rle = 1;
        /* fallthrough */
    case TGA_TYPE_RGB:
        indexed = 0;
        break;

    case TGA_TYPE_RLE_BW:
        rle = 1;
        /* fallthrough */
    case TGA_TYPE_BW:
        if (hdr.pixel_bits != 8) {
            *err = ERR_UNSUPPORTED;
            goto cleanup;
        }
        /* Treat greyscale as 8bpp indexed images */
        indexed = grey = 1;
        break;

    default:
        *err = ERR_UNSUPPORTED;
        goto cleanup;
    }

    bpp = (hdr.pixel_bits + 7) >> 3;
    //rmask = gmask = bmask = amask = 0;
    switch(hdr.pixel_bits) {
    case 8:
        if (!indexed) {
            *err = ERR_UNSUPPORTED;
            goto cleanup;
        }
        break;

    case 15:
    case 16:
        *err = ERR_UNSUPPORTED;
        goto cleanup;
        // TODO: Support 15/16 bit formats
#if 0
        /* 15 and 16bpp both seem to use 5 bits/plane. The extra alpha bit
           is ignored for now. */
        rmask = 0x7c00;
        gmask = 0x03e0;
        bmask = 0x001f;
        break;
#endif

    case 32:
        alpha = 1;
        /* fallthrough */
    case 24:
#if 0
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        {
        int s = alpha ? 0 : 8;
        amask = 0x000000ff >> s;
        rmask = 0x0000ff00 >> s;
        gmask = 0x00ff0000 >> s;
        bmask = 0xff000000 >> s;
        }
#else
        amask = alpha ? 0xff000000 : 0;
        rmask = 0x00ff0000;
        gmask = 0x0000ff00;
        bmask = 0x000000ff;
#endif
#endif
        break;

    default:
        *err = ERR_UNSUPPORTED;
        goto cleanup;
    }

    if ((hdr.flags & TGA_INTERLEAVE_MASK) != TGA_INTERLEAVE_NONE
       || hdr.flags & TGA_ORIGIN_RIGHT) {
        *err = ERR_UNSUPPORTED;
        goto cleanup;
    }


    //SDL_RWseek(src, hdr.infolen, RW_SEEK_CUR); /* skip info field */
    im_in_seek(in, hdr.infolen, IM_SEEK_CUR);

    w = LE16(hdr.width);
    h = LE16(hdr.height);
    
    if (indexed) {
        fmt = IM_FMT_INDEX8;
    } else if(alpha) {
        fmt = IM_FMT_RGBA;
    } else {
        fmt = IM_FMT_RGB;
    }
    img = im_img_new(w, h, 1, fmt);
    if (!img) {
        *err = ERR_NOMEM;
        goto cleanup;
    }

    if (hdr.has_cmap) {
        int palsiz = ncols * ((hdr.cmap_bits + 7) >> 3);
        if (indexed && !grey) {
            // TODO: read palette!
            *err = ERR_UNSUPPORTED;
            goto cleanup;
#if 0
            uint8_t *pal = (uint8_t *)imalloc(palsiz), *p = pal;
            SDL_Color *colors = img->format->palette->colors;
            img->format->palette->ncolors = ncols;
            SDL_RWread(src, pal, palsiz, 1);
            for(i = 0; i < ncols; i++) {
                switch(hdr.cmap_bits) {
                case 15:
                case 16:
                    {
                    uint16_t c = p[0] + (p[1] << 8);
                    p += 2;
                    colors[i].r = (c >> 7) & 0xf8;
                    colors[i].g = (c >> 2) & 0xf8;
                    colors[i].b = c << 3;
                    }
                    break;
                case 24:
                case 32:
                    colors[i].b = *p++;
                    colors[i].g = *p++;
                    colors[i].r = *p++;
                    if (hdr.cmap_bits == 32 && *p++ < 128)
                    ckey = i;
                    break;
                }
            }
            ifree(pal);
//            if (ckey >= 0)
//                SDL_SetColorKey(img, SDL_TRUE, ckey);
#endif                
        } else {
            /* skip unneeded colormap */
            im_in_seek(in, palsiz, IM_SEEK_CUR);
        }
    }

    if (grey) {
        // TODO
        *err = ERR_UNSUPPORTED;
        goto cleanup;
#if 0
        SDL_Color *colors = img->format->palette->colors;
        for(i = 0; i < 256; i++)
            colors[i].r = colors[i].g = colors[i].b = i;
        img->format->palette->ncolors = 256;
#endif
    }

    /* The RLE decoding code is slightly convoluted since we can't rely on
       spans not to wrap across scan lines */
    count = rep = 0;
    for(i = 0; i < h; i++) {
        if (hdr.flags & TGA_ORIGIN_UPPER) {
            rowStart = (uint8_t *)im_img_row(img, i);
        } else {
            rowStart = (uint8_t *)im_img_row(img, (h-1)-i);
        }
        dst = rowStart;

        if (rle) {
            int x = 0;
            for(;;) {
                uint8_t c;

                if (count) {
                    int n = count;
                    if (n > w - x)
                        n = w - x;
                    if (im_in_read(in, dst + x * bpp, n * bpp) != n*bpp) {
                        *err = im_in_eof(in) ? ERR_MALFORMED:ERR_FILE;
                        goto cleanup;
                    }
                    count -= n;
                    x += n;
                    if (x == w)
                        break;
                } else if (rep) {
                    int n = rep;
                    if (n > w - x)
                        n = w - x;
                    rep -= n;
                    while (n--) {
                        memcpy(dst + x * bpp, &pixel, bpp);
                        x++;
                    }
                    if (x == w)
                        break;
                }

                if (im_in_read(in, &c, 1) != 1) {
                    *err = im_in_eof(in) ? ERR_MALFORMED:ERR_FILE;
                    goto cleanup;
                }
                if (c & 0x80) {
                    if (im_in_read(in, &pixel, bpp) != bpp) {
                        *err = im_in_eof(in) ? ERR_MALFORMED:ERR_FILE;
                        goto cleanup;
                    }
                    rep = (c & 0x7f) + 1;
                } else {
                    count = c + 1;
                }
            }
        } else {
            if (im_in_read(in, dst, w*bpp) != w*bpp) {
                *err = im_in_eof(in) ? ERR_MALFORMED:ERR_FILE;
                goto cleanup;
            }
        }

        if (bpp == 3) {
            // convert BGR -> RGB
            uint8_t* p = rowStart;
            int x;
            for (x = 0; x < w; ++x) {
                uint8_t b = p[0];
                p[0] = p[2];
                p[2] = b;
                p += 3;
            }
        }
        if (bpp == 4) {
            // convert BGRA -> RGBA
            uint8_t* p = rowStart;
            int x;
            for (x = 0; x < w; ++x) {
                uint8_t b = p[0];
                p[0] = p[2];
                p[2] = b;
                p += 4;
            }
        }

#if 0
        // TODO: support expanding out 15/16 bit formats?
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
        if (bpp == 2) {
            /* swap byte order */
            int x;
            uint16_t *p = (uint16_t *)dst;
            for(x = 0; x < w; x++)
            p[x] = SDL_Swap16(p[x]);
        }
#endif
#endif
    }

cleanup:
    if (*err != ERR_NONE) {
        if (img) {
            im_img_free(img);
            img = NULL;
        }
    }

    return img;
}


