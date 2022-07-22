#include "impy.h"
#include "private.h"

#include <assert.h>
//#include <stdio.h>
#include <string.h>
//#include <limits.h>

static bool generic_read_img(im_read* rdr);
static void generic_read_rows(im_read *rdr, unsigned int num_rows, void* buf, int stride);
static void generic_read_finish(im_read* rdr);


// Helper to simplify loaders which just slurp in a single im_img.
typedef struct generic_reader {
    im_read base;

    // type-specific fields from here on
    im_img* img;
    im_img* (*load_single)(im_in *in, ImErr *err);
} generic_reader;

//im_img* (load_single)(im_in *in, ImErr *err);


static read_handler generic_read_handler = {
    generic_read_img,
    generic_read_rows,
    generic_read_finish
};

im_read* im_new_generic_reader(im_img* (*load_single)(im_in *, ImErr *), im_in* in, ImErr* err )
{
    generic_reader *gr = imalloc(sizeof(generic_reader));
    if (!gr) {
        *err = ERR_NOMEM;
        return NULL;
    }

    // im_read fields
    i_read_init(&gr->base);
    gr->base.handler = &generic_read_handler;
    gr->base.in = in;

    // type-specific fields
    gr->img = NULL;
    gr->load_single = load_single;

    return (im_read*)gr;
}


static bool generic_read_img(im_read* rdr)
{
    generic_reader *gr = (generic_reader*)rdr;
    im_imginfo* info;
    im_img* img; 

    if (gr->img) {
        // No more frames.
        return false;
    }
    // Perform the load.
    gr->img = gr->load_single(rdr->in, &rdr->err);
    if (!gr->img) {
        return false;
    }

    // Populate curr from the im_img.
    img = gr->img;
    info = &rdr->curr;
    info->w = img->w;
    info->h = img->h;
    info->x_offset = img->x_offset;
    info->y_offset = img->y_offset;
    info->fmt = img->format;
    info->pal_num_colours = img->pal_num_colours;

    if (img->pal_num_colours>0) {
        // Copy out palette, in RGBA format.
        rdr->pal_data = irealloc(rdr->pal_data, img->pal_num_colours * im_fmt_bytesperpixel(IM_FMT_RGBA));
        if (!rdr->pal_data) {
            rdr->err = ERR_NOMEM;
            return false;
        }
        im_convert_fn cvt_fn = i_pick_convert_fn(img->pal_fmt, IM_FMT_RGBA);
        if (!cvt_fn) {
            rdr->err = ERR_NOCONV;
            return false;
        }
        cvt_fn(img->pal_data, rdr->pal_data, img->pal_num_colours, 0, NULL);
    }

    return true;
}

static void generic_read_rows(im_read *rdr, unsigned int num_rows, void* buf, int stride)
{
    generic_reader *gr = (generic_reader*)rdr;
    size_t bytes_per_row;
    im_img* img = gr->img;

    assert(rdr->state == READSTATE_BODY);
    assert(img);

    bytes_per_row = im_fmt_bytesperpixel(img->format) * img->w;
    for (unsigned int row = 0; row < num_rows; ++row) {
        unsigned int y = rdr->rows_read + row;
        memcpy(buf, im_img_row(img, y), bytes_per_row);
        buf += stride;
    }
}

static void generic_read_finish(im_read* rdr)
{
    generic_reader *gr = (generic_reader*)rdr;
    if(gr->img) {
        im_img_free(gr->img);
        gr->img = NULL;
    }
}


