// implementation-neutral support for im_img

#include "impy.h"
#include "private.h"

#include <string.h>    // for memcpy


static void copy_pal_range( ImPalFmt src_fmt, const uint8_t* src, ImPalFmt dest_fmt, uint8_t *dest, int first_colour, int num_colours);


// calc bytes per pixel
size_t im_bytesperpixel(ImFmt fmt, ImDatatype datatype)
{
    int n=0;

    switch (datatype) {
        case DT_U8:
        case DT_S8:
            n=1;
            break;
        case DT_U16:
        case DT_S16:
        case DT_FLOAT16:
            n=2;
            break;
        case DT_U32:
        case DT_S32:
        case DT_FLOAT32:
            n=4;
            break;
        case DT_FLOAT64:
            n=8;
            break;
    }

    switch (fmt) {
        case FMT_RGB: return 3*n;
        case FMT_RGBA: return 4*n;
        case FMT_BGR: return 3*n;
        case FMT_BGRA: return 4*n;
        case FMT_COLOUR_INDEX: return 1*n;
        case FMT_ALPHA: return 1*n;
        case FMT_LUMINANCE: return 1*n;
    }
    return 0;
}




bool im_img_pal_equal(const im_img* a, const im_img* b)
{
    size_t nbytes;

    if (a->pal_num_colours != b->pal_num_colours) {
        return false;
    }
    if (a->pal_fmt != b->pal_fmt) {
        return false;
    }
    switch (a->pal_fmt) {
        case PALFMT_RGB: nbytes=3; break;
        case PALFMT_RGBA: nbytes=4; break;
        default: return false;
    }
    if(memcmp( a->pal_data, b->pal_data, a->pal_num_colours*nbytes) != 0 ) {
        return false;
    }
    return true;
}



// load colours into palette
bool im_img_pal_write( im_img* img, int first_colour, int num_colours, ImPalFmt data_fmt, const void* data)
{
    if (first_colour+num_colours > img->pal_num_colours) {
        return false;
    }

    copy_pal_range(
        data_fmt, data,
        img->pal_fmt, img->pal_data,
        first_colour, num_colours);
    return true;
}


// read colours from palette
bool im_img_pal_read( im_img* img, int first_colour, int num_colours, ImPalFmt dest_fmt, void* dest)
{
    if (first_colour+num_colours > img->pal_num_colours) {
        return false;
    }

    copy_pal_range(
        img->pal_fmt, img->pal_data,
        dest_fmt, dest,
        first_colour, num_colours);
    return true;
}


// helper func - copy/convert palette colours
static void copy_pal_range( ImPalFmt src_fmt, const uint8_t* src, ImPalFmt dest_fmt, uint8_t *dest, int first_colour, int num_colours) {

    if (src_fmt==PALFMT_RGB && dest_fmt==PALFMT_RGB) {
        memcpy(dest + (first_colour*3), src, num_colours*3);
    } else if (src_fmt==PALFMT_RGBA && dest_fmt==PALFMT_RGBA) {
        memcpy(dest + (first_colour*4), src, num_colours*4);
    } else if (src_fmt == PALFMT_RGB && dest_fmt==PALFMT_RGBA) {
        int i;
        dest += 4*first_colour;
        for (i=0; i<num_colours; ++i) {
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = 255;
        }
    } else if (src_fmt == PALFMT_RGBA && dest_fmt==PALFMT_RGB) {
        int i;
        dest += 3*first_colour;
        for (i=0; i<num_colours; ++i) {
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = *src++;
            ++src;
        }
    }
}



im_img* im_img_clone(const im_img* src_img)
{
    int w = src_img->w;
    int h = src_img->h;
    int d = src_img->d;
    ImFmt fmt = src_img->format;
    ImDatatype dt = src_img->datatype;

    size_t bytesperline = w * im_bytesperpixel(fmt,dt);
    int y,z;
    im_img* dest_img = im_img_new(w,h,d,fmt,dt);
    if (!dest_img) {
        return NULL;
    }
    // copy the data:
    for (z=0; z<d; ++z) {
        for (y=0; y<h; ++y) {
            const uint8_t* src = im_img_row(src_img, (z*h) + y);
            uint8_t* dest = im_img_row(dest_img, (z*h) + y);
            memcpy(dest, src, bytesperline);
        }
    }

    // copy palette, if there is one
    int ncolours = src_img->pal_num_colours;
    if (ncolours>0) {
        const void* raw = src_img->pal_data;
        if( !im_img_pal_set( dest_img, src_img->pal_fmt, ncolours, raw) ) {
            im_img_free(dest_img);
            return NULL;
        }
    }
    return dest_img;
}

im_img* im_img_new(int w, int h, int d, ImFmt fmt, ImDatatype datatype)
{
    im_img* foo;
    int bytesPerPixel;
    int datsize;

    if( w<1 || h<1 || d<1 ) {
        return NULL;
    }

    bytesPerPixel = im_bytesperpixel(fmt,datatype);
    if(bytesPerPixel==0) {
        return NULL;
    }

    foo = imalloc(sizeof(struct im_img));
    if (foo==NULL) {
        return 0;
    }

    foo->w = w;
    foo->h = h;
    foo->d = d;

    foo->format = fmt;
    foo->datatype = datatype;
    foo->pitch = bytesPerPixel * foo->w;
    foo->pixel_data = imalloc(foo->h * foo->pitch * foo->d);
    if (!foo->pixel_data) {
        ifree(foo);
        return NULL;
    }

    // memset(foo->pixel_data, 0xdd, foo->height * foo->pitch * foo->depth);

    foo->pal_num_colours = 0;
    foo->pal_fmt = PALFMT_RGB;
    foo->pal_data = 0;

    foo->x_offset = 0;
    foo->y_offset = 0;

    return foo;
}

void im_img_free(im_img *img)
{
    if (img->pixel_data) {
        ifree(img->pixel_data);
    }
    if (img->pal_data) {
        ifree(img->pal_data);
    }
    ifree(img);
}


// image palette fns
bool im_img_pal_set( im_img* img, ImPalFmt fmt, int ncolours, const void* data)
{
    im_img* foo = img;
    int colsize;
    switch (fmt) {
        case PALFMT_RGB: colsize=3; break;
        case PALFMT_RGBA: colsize=4; break;
        default: return false;
    }
    if( foo->pal_fmt != fmt || foo->pal_num_colours != ncolours ) {
        // reallocate
        uint8_t* newdata = NULL;
        if(ncolours > 0) {
            newdata = imalloc(ncolours*colsize);
            if (!newdata) {
                return false;
            }
        }
        if (foo->pal_data) {
            ifree(foo->pal_data);
        }
        foo->pal_data = newdata;
        foo->pal_fmt = fmt;
        foo->pal_num_colours = ncolours;
    }

    if( data ) {
        im_img_pal_write( img, 0, ncolours, fmt, data );
    } else {
        memset( foo->pal_data, 0, ncolours*colsize);
    }
    return true;
}

