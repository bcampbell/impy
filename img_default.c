// default im_img implementation

#include "im.h"
#include "private.h"

#include <string.h>




struct image {
    int width;
    int height;
    int depth;

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
};




im_img* image_create(int w, int h, int d, ImFmt fmt, ImDatatype datatype)
{
    struct image* foo;
    int bytesPerPixel;
    int datsize;

    if( w<1 || h<1 || d<1 ) {
        return NULL;
    }

    bytesPerPixel = im_bytesperpixel(fmt,datatype);
    if(bytesPerPixel==0) {
        return NULL;
    }

    foo = imalloc(sizeof(struct image));
    if (foo==NULL) {
        return 0;
    }

    foo->width = w;
    foo->height = h;
    foo->depth = d;


    foo->format = fmt;
    foo->datatype = datatype;
    foo->pitch = bytesPerPixel * foo->width;
    foo->pixel_data = imalloc(foo->height * foo->pitch * foo->depth);
    if (!foo->pixel_data) {
        ifree(foo);
        return NULL;
    }

    foo->pal_num_colours = 0;
    foo->pal_fmt = PALFMT_RGB;
    foo->pal_data = 0;

    foo->x_offset = 0;
    foo->y_offset = 0;

    return (im_img*)foo;
}

void image_free(im_img *img)
{
    struct image* foo = (struct image*)img;
    if (foo->pal_data) {
        ifree(foo->pal_data);
    }
    ifree(foo);
}


static int image_w(const im_img *img)
    { return ((const struct image*)img)->width; }

static int image_h(const im_img *img)
    { return ((const struct image*)img)->height; }

static int image_d(const im_img *img)
    { return ((const struct image*)img)->depth; }

static ImFmt image_fmt(const im_img *img)
    { return ((const struct image*)img)->format; }

static ImDatatype image_datatype(const im_img *img)
    { return ((const struct image*)img)->datatype; }

static void* image_pos(const im_img *img, int x, int y)
{
    const struct image* foo = (const struct image*)img;
    return ((uint8_t*)(foo->pixel_data)) + (y*foo->pitch) + (x*im_bytesperpixel(foo->format, foo->datatype));
}

static int image_pitch(const im_img *img)
    { return ((const struct image*)img)->pitch; }

// image palette fns
static bool image_pal_set( im_img* img, ImPalFmt fmt, int ncolours, const void* data)
{
    struct image* foo = (struct image*)img;
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


static void* image_pal_data(const im_img* img)
    { return ((const struct image*)img)->pal_data; }

static int image_pal_num_colours(const im_img* img)
    { return ((const struct image*)img)->pal_num_colours; }

static ImPalFmt image_pal_fmt(const im_img* img)
    { return ((const struct image*)img)->pal_fmt; }

// metadata access... too verbose?

static int image_x_offset(const im_img *img)
    { return ((const struct image*)img)->x_offset; }
static int image_y_offset(const im_img *img)
    { return ((const struct image*)img)->y_offset; }

static void image_set_offset(im_img *img, int x, int y)
{
    struct image* foo = (struct image*)img;
    foo->x_offset = x;
    foo->y_offset = y;
}

im_img_impl im_default_img_impl = {
    image_create,
    image_free,
    image_w,
    image_h,
    image_d,
    image_fmt,
    image_datatype,
    image_pos,
    image_pitch,

    image_pal_set,
    image_pal_data,
    image_pal_num_colours,
    image_pal_fmt,

    image_x_offset,
    image_y_offset,
    image_set_offset,
};







