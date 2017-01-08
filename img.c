// implementation-neutral support for im_img

#include "im.h"
#include "private.h"

#include <string.h>    // for memcpy


static void copy_pal_range( ImPalFmt src_fmt, const uint8_t* src, ImPalFmt dest_fmt, uint8_t *dest, int first_colour, int num_colours);


// the current im_img implementation
im_img_impl* im_current_img_impl = &im_default_img_impl;


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
