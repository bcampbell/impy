#ifndef IMG_H
#define IMG_H

#include "impy.h"

/**********************
 * Image handling stuff - internal!
 **********************/


typedef struct im_img {
    int w;
    int h;
    int d;

    ImFmt format;     // IM_FMT_*

    int pitch;  // bytes per line
    void* pixel_data;

    // palette
    int pal_num_colours;    // 0=no palette
    ImFmt pal_fmt;
    void* pal_data;     // can be NULL, iff num_colours==0

    // metadata
    int x_offset;
    int y_offset;
    // TODO: disposal, frame duration, etc...
} im_img;


// creates a new image (no palette, even if indexed)
extern im_img* im_img_new(int w, int h, int d, ImFmt fmt);

// Free an image (and its content and palette)
extern void im_img_free(im_img *img);

// Create a new copy of an existing image
extern im_img* im_img_clone(const im_img* src_img);

// Fetch a pointer to a specific pixel
static inline void* im_img_pos(const im_img *img, int x, int y)
    { return ((uint8_t*)(img->pixel_data)) + (y*img->pitch) + (x*im_fmt_bytesperpixel(img->format)); }

// Fetch a pointer to the start of a specific row
static inline void* im_img_row(const im_img *img, int row)
    { return im_img_pos(img,0,row); }


// image palette fns

// set a palette. If data is non-NULL, the palette will be initialised using
// the colours it points to.
// If data is NULL, the palette data will be zeroed.
// ncolours==0 means no palette.
extern bool im_img_pal_set( im_img* img, ImFmt fmt, int ncolours, const void* data);

// load colours into existing palette, converting format if necessary
extern bool im_img_pal_write( im_img* img, int first_colour, int num_colours, ImFmt data_fmt, const void* data);

// read colours out of palette, converting format if necessary
extern bool im_img_pal_read( im_img* img, int first_colour, int num_colours, ImFmt dest_fmt, void* dest);

// returns true if images have identical palettes (same format,
// number of colours and colour data)
extern bool im_img_pal_equal(const im_img* a, const im_img* b);

#endif  // IMG_H
