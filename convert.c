#include "im.h"

#include <stdint.h>
#include <string.h>

static im_Img* convert_indexed( const im_Img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

static void cvt_u8INDEX_u8RGB( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal);
static void cvt_u8INDEX_u8RGBA( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal);
static void cvt_u8INDEX_u8BGR( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal);
static void cvt_u8INDEX_u8BGRA( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal);
static void cvt_u8INDEX_u8ALPHA( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal);


static im_Img* convert_direct( const im_Img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

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


im_Img* im_img_convert( const im_Img* srcImg, ImFmt destFmt, ImDatatype destDatatype )
{
    if (srcImg->Datatype != DT_U8 || destDatatype!=DT_U8) {
        // TODO - type conversions!
        // maybe just fmtconvert to same type, then use a second
        // pass to convert the type?
        im_err(ERR_NOCONV);
        return NULL;
    }
    if(srcImg->Format!=FMT_COLOUR_INDEX && destFmt==FMT_COLOUR_INDEX) {
        // no unsolicited quantising, thankyouverymuch
        im_err(ERR_NOCONV);
        return NULL;
    }


    switch (srcImg->Format) {
        case FMT_COLOUR_INDEX:
            return convert_indexed(srcImg, destFmt, destDatatype);
        case FMT_RGB:
        case FMT_RGBA:
        case FMT_BGR:
        case FMT_BGRA:
        case FMT_ALPHA:
            return convert_direct(srcImg, destFmt, destDatatype);
        default:
            im_err(ERR_NOCONV);
            return NULL;
    }
}

static im_Img* convert_indexed( const im_Img* srcImg, ImFmt destFmt, ImDatatype destDatatype )
{
    im_Img* destImg = NULL;

    // pick line-converter
    void (*fn)( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal) = NULL;
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
        im_err(ERR_NOCONV);
        return NULL;
    }


    // convert!
    destImg = im_img_new(srcImg->Width, srcImg->Height, srcImg->Depth,  destFmt, destDatatype);
    if (destImg) {
        int d,y;
        const uint8_t* srcLine = srcImg->Data;
        uint8_t* destLine = destImg->Data;
        for (d=0; d<srcImg->Depth; ++d) {
            for (y=0; y<srcImg->Height; ++y) {
                fn( srcLine, destLine, srcImg->Width, srcImg->Palette);
                destLine += destImg->Pitch;
                srcLine += srcImg->Pitch;
            }
        }
    }
    return destImg;
}



static void cvt_u8INDEX_u8RGB( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal)
{
    int x;
    const uint8_t* cols = (const uint8_t*)srcPal->Data;
    switch (srcPal->Format)
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


static void cvt_u8INDEX_u8RGBA( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal)
{
    int x;
    const uint8_t* cols = (const uint8_t*)srcPal->Data;
    switch (srcPal->Format)
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


static void cvt_u8INDEX_u8BGR( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal)
{
    int x;
    const uint8_t* cols = (const uint8_t*)srcPal->Data;
    for (x=0; x<w; ++x) {
        int idx = ((int)*src ) *4;
        ++src;
        *dest++ = cols[idx+2];
        *dest++ = cols[idx+1];
        *dest++ = cols[idx+0];
    } 
}

static void cvt_u8INDEX_u8BGRA( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal)
{
    int x;
    const uint8_t* cols = (const uint8_t*)srcPal->Data;
    switch (srcPal->Format)
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

static void cvt_u8INDEX_u8ALPHA( const uint8_t* src, uint8_t* dest, int w, const im_Pal* srcPal)
{
    int x;
    const uint8_t* cols = (const uint8_t*)srcPal->Data;
    switch (srcPal->Format)
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
static im_Img* convert_direct( const im_Img* srcImg, ImFmt destFmt, ImDatatype destDatatype )
{
    im_Img* destImg = NULL;

    // pick line-converter
    void (*fn)( const uint8_t* src, uint8_t* dest, int w) = NULL;
    switch (srcImg->Format) {
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

    }
    if (fn==NULL) {
        im_err(ERR_NOCONV);
        return NULL;
    }


    // convert!
    destImg = im_img_new(srcImg->Width, srcImg->Height, srcImg->Depth,  destFmt, destDatatype);
    if (destImg) {
        int d,y;
        const uint8_t* srcLine = srcImg->Data;
        uint8_t* destLine = destImg->Data;
        for (d=0; d<srcImg->Depth; ++d) {
            for (y=0; y<srcImg->Height; ++y) {
                fn( srcLine, destLine, srcImg->Width);
                destLine += destImg->Pitch;
                srcLine += srcImg->Pitch;
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


