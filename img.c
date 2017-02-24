// implementation-neutral support for im_img

#include "impy.h"
#include "private.h"

#include <string.h>    // for memcpy


static void copy_pal_range( ImPalFmt src_fmt, const uint8_t* src, ImPalFmt dest_fmt, uint8_t *dest, int first_colour, int num_colours);


// the current im_img implementation
im_img_impl* im_current_img_impl = &im_default_img_impl;

// calc bytes per pixel
size_t im_bytesperpixel(ImFmt fmt, ImDatatype datatype) {
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

    if (im_img_pal_num_colours(a) != im_img_pal_num_colours(b)) {
        return false;
    }
    if (im_img_pal_fmt(a) != im_img_pal_fmt(b)) {
        return false;
    }
    switch (im_img_pal_fmt(a)) {
        case PALFMT_RGB: nbytes=3; break;
        case PALFMT_RGBA: nbytes=4; break;
        default: return false;
    }
    if(memcmp( im_img_pal_data(a), im_img_pal_data(b), nbytes) != 0 ) {
        return false;
    }
    return true;
}



// load colours into palette
bool im_img_pal_write( im_img* img, int first_colour, int num_colours, ImPalFmt data_fmt, const void* data)
{
    if (first_colour+num_colours > im_img_pal_num_colours(img)) {
        return false;
    }

    copy_pal_range(
        data_fmt, data,
        im_img_pal_fmt(img), im_img_pal_data(img),
        first_colour, num_colours);
    return true;
}


// read colours from palette
bool im_img_pal_read( im_img* img, int first_colour, int num_colours, ImPalFmt dest_fmt, void* dest)
{
    if (first_colour+num_colours > im_img_pal_num_colours(img)) {
        return false;
    }

    copy_pal_range(
        im_img_pal_fmt(img), im_img_pal_data(img),
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
    int w = im_img_w(src_img);
    int h = im_img_h(src_img);
    int d = im_img_d(src_img);
    ImFmt fmt = im_img_format(src_img);
    ImDatatype dt = im_img_datatype(src_img);

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
    int ncolours = im_img_pal_num_colours(src_img);
    if (ncolours>0) {
        const void* raw = im_img_pal_data(src_img);
        if( !im_img_pal_set( dest_img, im_img_pal_fmt(src_img), ncolours, raw) ) {
            im_img_free(dest_img);
            return NULL;
        }
    }
    return dest_img;
}

