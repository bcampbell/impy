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

    ImFmt format;     // FMT_*
    ImDatatype datatype;     // DT_*

    int pitch;  // bytes per line
    void* pixel_data;

    // palette
    int pal_num_colours;    // 0=no palette
    ImPalFmt pal_fmt;
    void* pal_data;     // can be NULL, iff num_colours==0

    // metadata
    int x_offset;
    int y_offset;
    // TODO: disposal, frame duration, etc...
} im_img;


// creates a new image (no palette, even if indexed)
extern im_img* im_img_new(int w, int h, int d, ImFmt fmt, ImDatatype datatype);

// Free an image (and its content and palette)
extern void im_img_free(im_img *img);

// Create a new copy of an existing image
extern im_img* im_img_clone(const im_img* src_img);

// Convert an image to another format/datatype (a copy is returned, 
extern im_img* im_img_convert( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

// Helper to calculate the bytes per pixel required for a given format/datatype combination
extern size_t im_bytesperpixel(ImFmt fmt, ImDatatype datatype);

// Fetch a pointer to a specific pixel
static inline void* im_img_pos(const im_img *img, int x, int y)
    { return ((uint8_t*)(img->pixel_data)) + (y*img->pitch) + (x*im_bytesperpixel(img->format, img->datatype)); }

// Fetch a pointer to the start of a specific row
static inline void* im_img_row(const im_img *img, int row)
    { return im_img_pos(img,0,row); }

static inline size_t im_img_bytesperpixel(const im_img *img)
    { return im_bytesperpixel(img->format, img->datatype); }


// image palette fns

// set a palette. If data is non-NULL, the palette will be initialised using
// the colours it points to.
// If data is NULL, the palette data will be zeroed.
// ncolours==0 means no palette.
extern bool im_img_pal_set( im_img* img, ImPalFmt fmt, int ncolours, const void* data);

// load colours into existing palette, converting format if necessary
extern bool im_img_pal_write( im_img* img, int first_colour, int num_colours, ImPalFmt data_fmt, const void* data);

// read colours out of palette, converting format if necessary
extern bool im_img_pal_read( im_img* img, int first_colour, int num_colours, ImPalFmt dest_fmt, void* dest);

// returns true if images have identical palettes (same format,
// number of colours and colour data)
extern bool im_img_pal_equal(const im_img* a, const im_img* b);

#endif  // IMG_H
