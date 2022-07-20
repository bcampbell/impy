#include "impy.h"
#include "private.h"

#include <stdint.h>
#include <string.h>
#include <assert.h>


/*******************
 * INDEX8 -> whatever
 */

static void cvt_u8INDEX_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w,  unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = ((int)*src) * 4;
        ++src;
        *dest++ = rgba[idx+0];
        *dest++ = rgba[idx+1];
        *dest++ = rgba[idx+2];
    } 
}


static void cvt_u8INDEX_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = 4 * (int)(*src++);
        *dest++ = rgba[idx+0];
        *dest++ = rgba[idx+1];
        *dest++ = rgba[idx+2];
        *dest++ = rgba[idx+3];
    }
}

static void cvt_u8INDEX_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = ((int)*src) * 4;
        ++src;
        *dest++ = rgba[idx+1];
        *dest++ = rgba[idx+2];
        *dest++ = rgba[idx+3];
        *dest++ = rgba[idx+0];
    }
}


static void cvt_u8INDEX_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = ((int)*src) * 4;
        ++src;
        *dest++ = rgba[idx+2];
        *dest++ = rgba[idx+1];
        *dest++ = rgba[idx+0];
    } 
}

static void cvt_u8INDEX_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = ((int)*src) * 4;
        ++src;
        *dest++ = rgba[idx+2];
        *dest++ = rgba[idx+1];
        *dest++ = rgba[idx+0];
        *dest++ = rgba[idx+3];
    } 
}

static void cvt_u8INDEX_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = ((int)*src) * 4;
        ++src;
        *dest++ = rgba[idx+3];
        *dest++ = rgba[idx+2];
        *dest++ = rgba[idx+1];
        *dest++ = rgba[idx+0];
    } 
}


static void cvt_u8INDEX_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x = 0; x < w; ++x) {
        unsigned int idx = ((int)*src) * 4;
        ++src;
        *dest++ = rgba[idx+3];
    }
}


/*******************
 * RGB -> whatever
 */
static void cvt_u8RGB_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    memcpy(dest, src, w*3);
}

static void cvt_u8RGB_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = 255;
        src += 3;
    } 
}

static void cvt_u8RGB_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        src += 3;
    } 
}


static void cvt_u8RGB_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 3;
    } 
}

static void cvt_u8RGB_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = 255;
        src += 3;
    } 
}

static void cvt_u8RGB_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 3;
    } 
}


static void cvt_u8RGB_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    // this is just stupid :-)
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
    } 
}

/* RGBA -> whatever */

static void cvt_u8RGBA_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    memcpy(dest, src, w*4);
}

static void cvt_u8RGBA_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        src += 4;
    } 
}

/****************
 * ARGB -> whatever
 */

static void cvt_u8ARGB_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8ARGB_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = src[3];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8ARGB_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8ARGB_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        src += 4;
    } 
}

static void cvt_u8ARGB_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8ARGB_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        src += 4;
    } 
}

static void cvt_u8ARGB_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        src += 4;
    } 
}

/*****************
 * BGR -> whatever
 */

static void cvt_u8BGR_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 3;
    } 
}

static void cvt_u8BGR_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = 255;
        src += 3;
    } 
}

static void cvt_u8BGR_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 3;
    } 
}


static void cvt_u8BGR_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    memcpy(dest,src,w*3);
}

static void cvt_u8BGR_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = 255;
    } 
}

static void cvt_u8BGR_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = *src++;
    } 
}


static void cvt_u8BGR_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    // this is just stupid :-)
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
    } 
}


/******************
 * BGRA -> whatever
 */

static void cvt_u8BGRA_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8BGRA_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8BGRA_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}


static void cvt_u8BGRA_u8BGR( const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = *src++;
        ++src;
    } 
}

static void cvt_u8BGRA_u8BGRA( const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    memcpy(dest,src,w*4);
}

static void cvt_u8BGRA_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        src += 4;
    } 
}


static void cvt_u8BGRA_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    // this is just stupid :-)
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        src += 4;
    } 
}

/****************
 * ABGR -> whatever
 */

static void cvt_u8ABGR_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        src += 4;
    } 
}

static void cvt_u8ABGR_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8ABGR_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[3];
        *dest++ = src[2];
        *dest++ = src[1];
        src += 4;
    } 
}

static void cvt_u8ABGR_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8ABGR_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = src[3];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8ABGR_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8ABGR_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        src += 4;
    } 
}


/* ALPHA -> whatever */

static void cvt_u8ALPHA_u8RGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    // silly.
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
   } 
}

static void cvt_u8ALPHA_u8RGBA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = *src++;
    } 
}

static void cvt_u8ALPHA_u8ARGB(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = *src++;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
    } 
}

static void cvt_u8ALPHA_u8BGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    // silly.
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
    } 
}

static void cvt_u8ALPHA_u8BGRA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = *src++;
    } 
}

static void cvt_u8ALPHA_u8ABGR(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    unsigned int x;
    for (x=0; x<w; ++x) {
        *dest++ = *src++;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
    } 
}

static void cvt_u8ALPHA_u8ALPHA(const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba)
{
    memcpy(dest,src,w*1);
}



/*
 * Note: we treat any pad byte (X) the same as the alpha byte (A) to save
 * a few conversion functions.
 */

im_convert_fn i_pick_convert_fn(ImFmt srcFmt, ImFmt destFmt)
{
    im_convert_fn fn = NULL;

    switch (srcFmt) {
        case IM_FMT_INDEX8:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8INDEX_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8INDEX_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8INDEX_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8INDEX_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8INDEX_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8INDEX_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8INDEX_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8INDEX_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8INDEX_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8INDEX_u8ABGR; break;
                case IM_FMT_INDEX8: break;   // TODO: just pick closest colours?
                case IM_FMT_ALPHA: fn = cvt_u8INDEX_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_RGB:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8RGB_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8RGB_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8RGB_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8RGB_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8RGB_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8RGB_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8RGB_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8RGB_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8RGB_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8RGB_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8RGB_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_RGBA:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8RGBA_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8RGBA_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8RGBA_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8RGBA_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8RGBA_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8RGBA_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8RGBA_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8RGBA_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8RGBA_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8RGBA_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8RGBA_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_ARGB:
        case IM_FMT_XRGB:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8ARGB_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8ARGB_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8ARGB_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8ARGB_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8ARGB_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8ARGB_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8ARGB_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8ARGB_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8ARGB_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8ARGB_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8ARGB_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_BGR:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8BGR_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8BGR_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8BGR_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8BGR_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8BGR_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8BGR_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8BGR_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8BGR_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8BGR_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8BGR_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8BGR_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_BGRA:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8BGRA_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8BGRA_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8BGRA_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8BGRA_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8BGRA_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8BGRA_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8BGRA_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8BGRA_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8BGRA_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8BGRA_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8BGRA_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_ABGR:
        case IM_FMT_XBGR:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8ABGR_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8ABGR_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8ABGR_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8ABGR_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8ABGR_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8ABGR_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8ABGR_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8ABGR_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8ABGR_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8ABGR_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8ABGR_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        case IM_FMT_ALPHA:
            switch (destFmt) {
                case IM_FMT_RGB: fn = cvt_u8ALPHA_u8RGB; break;
                case IM_FMT_RGBA: fn = cvt_u8ALPHA_u8RGBA; break;
                case IM_FMT_RGBX: fn = cvt_u8ALPHA_u8RGBA; break;
                case IM_FMT_ARGB: fn = cvt_u8ALPHA_u8ARGB; break;
                case IM_FMT_XRGB: fn = cvt_u8ALPHA_u8ARGB; break;
                case IM_FMT_BGR: fn = cvt_u8ALPHA_u8BGR; break;
                case IM_FMT_BGRA: fn = cvt_u8ALPHA_u8BGRA; break;
                case IM_FMT_BGRX: fn = cvt_u8ALPHA_u8BGRA; break;
                case IM_FMT_ABGR: fn = cvt_u8ALPHA_u8ABGR; break;
                case IM_FMT_XBGR: fn = cvt_u8ALPHA_u8ABGR; break;
                case IM_FMT_INDEX8: break;
                case IM_FMT_ALPHA: fn = cvt_u8ALPHA_u8ALPHA; break;
                case IM_FMT_LUMINANCE: break;  //TODO
                default: break;
            }
            break;
        default:
            break;
    }
    return fn;
}

