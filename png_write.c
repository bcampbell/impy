#include "impy.h"
#include "private.h"
#include <png.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>


static void pre_img(im_writer* wr);
static void emit_header(im_writer* wr);
static void emit_rows(im_writer* wr, unsigned int num_rows, const uint8_t *data);
static void post_img(im_writer* wr);
static void finish(im_writer* wr);

static void emit_palette(im_writer* wr);
static void custom_write(png_structp png_ptr,
    png_bytep data, png_size_t length);
static void custom_flush(png_structp png_ptr);


typedef struct ipng_writer {
    // embedded im_writer
    im_writer base;
    // png-specific
    png_structp png_ptr;
    png_infop info_ptr;
} ipng_writer;

static struct write_handler ipng_write_handler = {
    IM_FILEFMT_PNG,
    pre_img,
    emit_header,
    emit_rows,
    post_img,
    finish
};


im_writer* ipng_new_writer(im_out* out, ImErr* err)
{
    ipng_writer* pw = imalloc(sizeof(ipng_writer));
    if (!pw) {
        *err = ERR_NOMEM;
        return NULL;
    }

    i_writer_init(&pw->base);

    pw->base.handler = &ipng_write_handler;
    pw->base.out = out;

    // PNG-specific
    pw->png_ptr = NULL;
    pw->info_ptr = NULL;

    *err = ERR_NONE;
    return (im_writer*)pw;
}


static void pre_img(im_writer* wr)
{
    if (wr->num_frames>0) {
        wr->err = ERR_ANIM_UNSUPPORTED;  // animation not supported.
        return;
    }

    // We want im_set_palette() to serve us RGBA format.
    wr->pal_fmt = IM_FMT_RGBA;
}

static void emit_header(im_writer* wr)
{
    ipng_writer* pw = (ipng_writer*)wr;

    assert(pw->png_ptr == NULL);
    assert(pw->info_ptr == NULL);

    pw->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,NULL,NULL);
    if (!pw->png_ptr) {
        wr->err = ERR_NOMEM;
        return;
    }

    pw->info_ptr = png_create_info_struct(pw->png_ptr);
    if (!pw->info_ptr)
    {
        wr->err = ERR_NOMEM;
        return;
    }

    // TODO - cover other functions which call png_* routines!
    if (setjmp(png_jmpbuf(pw->png_ptr)))
    {
       png_destroy_write_struct(&pw->png_ptr, &pw->info_ptr);
       // TODO: check error in im_out?
       wr->err = ERR_EXTLIB;
       return;
    }

    png_set_write_fn(pw->png_ptr,
        (png_voidp)wr->out, custom_write, custom_flush);

    // work out which format we'll be writing (and also which format
    // we'd like to receive from im_write_rows()).
    int color_type;
    if (im_fmt_is_indexed(wr->fmt)) {
        color_type = PNG_COLOR_TYPE_PALETTE;
        i_writer_set_internal_fmt(wr, IM_FMT_INDEX8);
    } else if (im_fmt_has_rgb(wr->fmt)) {
        if (im_fmt_has_alpha(wr->fmt)) {
            color_type = PNG_COLOR_TYPE_RGB_ALPHA;
            i_writer_set_internal_fmt(wr, IM_FMT_RGBA);
        } else {
            color_type = PNG_COLOR_TYPE_RGB;
            i_writer_set_internal_fmt(wr, IM_FMT_RGB);
        }
    } else {
        wr->err = ERR_UNSUPPORTED;  // unsupported fmt
        return;
    }

    png_set_IHDR(pw->png_ptr, pw->info_ptr,
        (png_uint_32)wr->w,
        (png_uint_32)wr->h,
        8,  //bit_depth
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    if (wr->pal_num_colours > 0) {
        emit_palette(wr);
    }

    // write the header chunks
    png_write_info(pw->png_ptr, pw->info_ptr);
}


static void emit_rows(im_writer* wr, unsigned int num_rows, const uint8_t *data)
{
    ipng_writer* pw = (ipng_writer*)wr;
    int i;
    for (i = 0; i < num_rows; ++i) {
        png_write_row(pw->png_ptr, (png_const_bytep)data);
        data += im_fmt_bytesperpixel(wr->fmt) * wr->w;
    }
}


static void emit_palette(im_writer* wr)
{
    png_color rgb[256] = {0};
    png_byte trans[256] = {0};
    uint8_t const* src;
    int maxtrans = -1;
    int i;
    ipng_writer* pw = (ipng_writer*)wr;

    assert(wr->pal_fmt == IM_FMT_RGBA); // As per prep_img().
    src = wr->pal_data;
    for (i = 0; i < wr->pal_num_colours; ++i) {
        rgb[i].red = *src++;
        rgb[i].green = *src++;
        rgb[i].blue = *src++;
        trans[i] = *src++;
        if( trans[i] != 255) {
            maxtrans=i;
        }
    }

    png_set_PLTE(pw->png_ptr, pw->info_ptr, rgb, wr->pal_num_colours);
    if (maxtrans!=-1) {
        png_set_tRNS(pw->png_ptr, pw->info_ptr, trans, maxtrans+1, NULL);
    }
}


// called after last row written
static void post_img(im_writer* wr)
{
    ipng_writer* pw = (ipng_writer*)wr;
    png_write_end(pw->png_ptr, pw->info_ptr);
}


static void finish(im_writer* wr)
{
    ipng_writer* pw = (ipng_writer*)wr;

    if (pw->png_ptr) {
       png_destroy_write_struct(&pw->png_ptr, &pw->info_ptr);
    }
}


static void custom_write(png_structp png_ptr,
    png_bytep data, png_size_t length)
{
    im_out* w = (im_out*)png_get_io_ptr(png_ptr);
    size_t n = im_write(w, data, length);
    if (n!=length) {
        png_error(png_ptr, "write error");
    }
}

static void custom_flush(png_structp png_ptr)
{
    // TODO
}

