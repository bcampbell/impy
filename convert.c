#include "impy.h"

#include <stdint.h>
#include <string.h>

static im_img* convert_indexed( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

static void cvt_u8INDEX_u8RGB( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt );
static void cvt_u8INDEX_u8RGBA( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt );
static void cvt_u8INDEX_u8BGR( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt );
static void cvt_u8INDEX_u8BGRA( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt );
static void cvt_u8INDEX_u8ALPHA( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt );


static im_img* convert_direct( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

static void cvt_u8RGB_u8RGB( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGB_u8RGBA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGB_u8BGR( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGB_u8BGRA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGB_u8ALPHA( const uint8_t* src, uint8_t* dest, int w );

static void cvt_u8RGBA_u8RGB( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGBA_u8RGBA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGBA_u8BGR( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGBA_u8BGRA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8RGBA_u8ALPHA( const uint8_t* src, uint8_t* dest, int w );

static void cvt_u8BGR_u8RGB( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGR_u8RGBA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGR_u8BGR( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGR_u8BGRA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGR_u8ALPHA( const uint8_t* src, uint8_t* dest, int w );

static void cvt_u8BGRA_u8RGB( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGRA_u8RGBA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGRA_u8BGR( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGRA_u8BGRA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8BGRA_u8ALPHA( const uint8_t* src, uint8_t* dest, int w );

static void cvt_u8ALPHA_u8RGB( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8ALPHA_u8RGBA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8ALPHA_u8BGR( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8ALPHA_u8BGRA( const uint8_t* src, uint8_t* dest, int w );
static void cvt_u8ALPHA_u8ALPHA( const uint8_t* src, uint8_t* dest, int w );


im_img* im_img_convert( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype )
{
    if (im_img_datatype(srcImg) != DT_U8 || destDatatype!=DT_U8) {
        // TODO - type conversions!
        // maybe just fmtconvert to same type, then use a second
        // pass to convert the type?
        return NULL;
    }
    if(im_img_format(srcImg)!=FMT_COLOUR_INDEX && destFmt==FMT_COLOUR_INDEX) {
        // no unsolicited quantising, thankyouverymuch
        return NULL;
    }


    switch (im_img_format(srcImg)) {
        case FMT_COLOUR_INDEX:
            return convert_indexed(srcImg, destFmt, destDatatype);
        case FMT_RGB:
        case FMT_RGBA:
        case FMT_BGR:
        case FMT_BGRA:
        case FMT_ALPHA:
            return convert_direct(srcImg, destFmt, destDatatype);
        default:
            return NULL;
    }
}

static im_img* convert_indexed( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype )
{
    im_img* destImg = NULL;

    // pick line-converter
    void (*fn)( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt ) = NULL;
    switch (destFmt) {
        case FMT_RGB: fn=cvt_u8INDEX_u8RGB; break;
        case FMT_RGBA: fn=cvt_u8INDEX_u8RGBA; break;
        case FMT_BGR: fn=cvt_u8INDEX_u8BGR; break;
        case FMT_BGRA: fn=cvt_u8INDEX_u8BGRA; break;
        case FMT_COLOUR_INDEX: break;   // TODO: just pick closest colours?
        case FMT_ALPHA:  fn=cvt_u8INDEX_u8ALPHA; break;
        case FMT_LUMINANCE: break;  //TODO
    }
    if (fn==NULL) {
        return NULL;
    }


    // convert!
    destImg = im_img_new(im_img_w(srcImg), im_img_h(srcImg), im_img_d(srcImg),  destFmt, destDatatype);
    if (destImg) {
        int d,y;
        const uint8_t* srcLine = im_img_row(srcImg,0);
        uint8_t* destLine = im_img_row(destImg,0);
        for (d=0; d<im_img_d(srcImg); ++d) {
            for (y=0; y<im_img_h(srcImg); ++y) {
                fn( srcLine, destLine, im_img_w(srcImg), im_img_pal_data(srcImg), im_img_pal_fmt(srcImg));
                destLine += im_img_pitch(destImg);
                srcLine += im_img_pitch(srcImg);
            }
        }
    }
    return destImg;
}



static void cvt_u8INDEX_u8RGB( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt)
{
    int x;
    const uint8_t* cols = (const uint8_t*)pal_data;
    switch (pal_fmt)
    {
        case PALFMT_RGB:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *3;
                ++src;
                *dest++ = cols[idx+0];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+2];
            } 
            break;
        case PALFMT_RGBA:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *4;
                ++src;
                *dest++ = cols[idx+0];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+2];
            } 
            break;
    }
}


static void cvt_u8INDEX_u8RGBA( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt)
{
    int x;
    const uint8_t* cols = (const uint8_t*)pal_data;
    switch (pal_fmt)
    {
        case PALFMT_RGB:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *3;
                ++src;
                *dest++ = cols[idx+0];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+2];
                *dest++ = 255;
            } 
            break;
        case PALFMT_RGBA:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *4;
                ++src;
                *dest++ = cols[idx+0];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+2];
                *dest++ = cols[idx+3];
            } 
            break;
    }
}


static void cvt_u8INDEX_u8BGR( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt)
{
    int x;
    const uint8_t* cols = (const uint8_t*)pal_data;
    switch (pal_fmt)
    {
        case PALFMT_RGB:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *3;
                ++src;
                *dest++ = cols[idx+2];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+0];
            } 
            break;
        case PALFMT_RGBA:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *4;
                ++src;
                *dest++ = cols[idx+2];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+0];
            } 
            break;
    } 
}

static void cvt_u8INDEX_u8BGRA( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt)
{
    int x;
    const uint8_t* cols = (const uint8_t*)pal_data;
    switch (pal_fmt)
    {
        case PALFMT_RGB:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *3;
                ++src;
                *dest++ = cols[idx+2];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+0];
                *dest++ = 255;
            } 
            break;
        case PALFMT_RGBA:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *4;
                ++src;
                *dest++ = cols[idx+2];
                *dest++ = cols[idx+1];
                *dest++ = cols[idx+0];
                *dest++ = cols[idx+3];
            } 
            break;
    }
}

static void cvt_u8INDEX_u8ALPHA( const uint8_t* src, uint8_t* dest, int w, const void* pal_data, ImPalFmt pal_fmt)
{
    int x;
    const uint8_t* cols = (const uint8_t*)pal_data;
    switch (pal_fmt)
    {
        case PALFMT_RGB:
            // BONKERS.
            for (x=0; x<w; ++x) {
                *dest++ = 255;
            } 
            break;
        case PALFMT_RGBA:
            for (x=0; x<w; ++x) {
                int idx = ((int)*src ) *4;
                ++src;
                *dest++ = cols[idx+3];
            } 
            break;
    }
}




/*  */
static im_img* convert_direct( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype )
{
    im_img* destImg = NULL;

    // pick line-converter
    void (*fn)( const uint8_t* src, uint8_t* dest, int w) = NULL;
    switch (im_img_format(srcImg)) {
        case FMT_RGB:
            switch (destFmt) {
                case FMT_RGB: fn=cvt_u8RGB_u8RGB; break;
                case FMT_RGBA: fn=cvt_u8RGB_u8RGBA; break;
                case FMT_BGR: fn=cvt_u8RGB_u8BGR; break;
                case FMT_BGRA: fn=cvt_u8RGB_u8BGRA; break;
                case FMT_COLOUR_INDEX: break;
                case FMT_ALPHA:  fn=cvt_u8RGB_u8ALPHA; break;
                case FMT_LUMINANCE: break;  //TODO
            }
            break;
        case FMT_RGBA:
            switch (destFmt) {
                case FMT_RGB: fn=cvt_u8RGBA_u8RGB; break;
                case FMT_RGBA: fn=cvt_u8RGBA_u8RGBA; break;
                case FMT_BGR: fn=cvt_u8RGBA_u8BGR; break;
                case FMT_BGRA: fn=cvt_u8RGBA_u8BGRA; break;
                case FMT_COLOUR_INDEX: break;
                case FMT_ALPHA:  fn=cvt_u8RGBA_u8ALPHA; break;
                case FMT_LUMINANCE: break;  //TODO
            }
            break;
        case FMT_BGR:
            switch (destFmt) {
                case FMT_RGB: fn=cvt_u8BGR_u8RGB; break;
                case FMT_RGBA: fn=cvt_u8BGR_u8RGBA; break;
                case FMT_BGR: fn=cvt_u8BGR_u8BGR; break;
                case FMT_BGRA: fn=cvt_u8BGR_u8BGRA; break;
                case FMT_COLOUR_INDEX: break;
                case FMT_ALPHA:  fn=cvt_u8BGR_u8ALPHA; break;
                case FMT_LUMINANCE: break;  //TODO
            }
            break;
        case FMT_BGRA:
            switch (destFmt) {
                case FMT_RGB: fn=cvt_u8BGRA_u8RGB; break;
                case FMT_RGBA: fn=cvt_u8BGRA_u8RGBA; break;
                case FMT_BGR: fn=cvt_u8BGRA_u8BGR; break;
                case FMT_BGRA: fn=cvt_u8BGRA_u8BGRA; break;
                case FMT_COLOUR_INDEX: break;
                case FMT_ALPHA:  fn=cvt_u8BGRA_u8ALPHA; break;
                case FMT_LUMINANCE: break;  //TODO
            }
            break;
        case FMT_ALPHA:
            switch (destFmt) {
                case FMT_RGB: fn=cvt_u8ALPHA_u8RGB; break;
                case FMT_RGBA: fn=cvt_u8ALPHA_u8RGBA; break;
                case FMT_BGR: fn=cvt_u8ALPHA_u8BGR; break;
                case FMT_BGRA: fn=cvt_u8ALPHA_u8BGRA; break;
                case FMT_COLOUR_INDEX: break;
                case FMT_ALPHA:  fn=cvt_u8ALPHA_u8ALPHA; break;
                case FMT_LUMINANCE: break;  //TODO
            }
            break;
        default:
            return NULL;
    }
    if (fn==NULL) {
        return NULL;
    }


    // convert!
    destImg = im_img_new(im_img_w(srcImg), im_img_h(srcImg), im_img_d(srcImg),  destFmt, destDatatype);
    if (destImg) {
        int d,y;
        const uint8_t* srcLine = im_img_row(srcImg,0);
        uint8_t* destLine = im_img_row(destImg,0);
        for (d=0; d<im_img_d(srcImg); ++d) {
            for (y=0; y<im_img_h(srcImg); ++y) {
                fn( srcLine, destLine, im_img_w(srcImg));
                destLine += im_img_pitch(destImg);
                srcLine += im_img_pitch(srcImg);
            }
        }
    }
    return destImg;
}


/* RGB -> whatever */

static void cvt_u8RGB_u8RGB( const uint8_t* src, uint8_t* dest, int w)
{
    memcpy(dest, src, w*3);
}

static void cvt_u8RGB_u8RGBA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        *dest++ = 255;
        src += 3;
    } 
}

static void cvt_u8RGB_u8BGR( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 3;
    } 
}

static void cvt_u8RGB_u8BGRA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = 255;
        src += 3;
    } 
}

static void cvt_u8RGB_u8ALPHA( const uint8_t* src, uint8_t* dest, int w)
{
    // this is just stupid :-)
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
    } 
}

/* RGBA -> whatever */

static void cvt_u8RGBA_u8RGB( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[0];
        *dest++ = src[1];
        *dest++ = src[2];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8RGBA( const uint8_t* src, uint8_t* dest, int w)
{
    memcpy(dest, src, w*4);
}

static void cvt_u8RGBA_u8BGR( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8BGRA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8RGBA_u8ALPHA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        src += 4;
    } 
}


/* BGR -> whatever */

static void cvt_u8BGR_u8RGB( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 3;
    } 
}

static void cvt_u8BGR_u8RGBA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = 255;
        src += 3;
    } 
}

static void cvt_u8BGR_u8BGR( const uint8_t* src, uint8_t* dest, int w)
{
    memcpy(dest,src,w*3);
}

static void cvt_u8BGR_u8BGRA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = 255;
    } 
}

static void cvt_u8BGR_u8ALPHA( const uint8_t* src, uint8_t* dest, int w)
{
    // this is just stupid :-)
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = 255;
    } 
}


/* BGRA -> whatever */

static void cvt_u8BGRA_u8RGB( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += 4;
    } 
}

static void cvt_u8BGRA_u8RGBA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        *dest++ = src[3];
        src += 4;
    } 
}

static void cvt_u8BGRA_u8BGR( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = *src++;
        ++src;
    } 
}

static void cvt_u8BGRA_u8BGRA( const uint8_t* src, uint8_t* dest, int w)
{
    memcpy(dest,src,w*4);
}

static void cvt_u8BGRA_u8ALPHA( const uint8_t* src, uint8_t* dest, int w)
{
    // this is just stupid :-)
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = src[3];
        src += 4;
    } 
}




/* ALPHA -> whatever */

static void cvt_u8ALPHA_u8RGB( const uint8_t* src, uint8_t* dest, int w)
{
    // silly.
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
   } 
}

static void cvt_u8ALPHA_u8RGBA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = *src++;
    } 
}

static void cvt_u8ALPHA_u8BGR( const uint8_t* src, uint8_t* dest, int w)
{
    // silly.
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
    } 
}

static void cvt_u8ALPHA_u8BGRA( const uint8_t* src, uint8_t* dest, int w)
{
    int x;
    for (x=0; x<w; ++x) {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = *src++;
    } 
}

static void cvt_u8ALPHA_u8ALPHA( const uint8_t* src, uint8_t* dest, int w)
{
    memcpy(dest,src,w*1);
}


