#include "impy.h"
#include "private.h"
#include <png.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static void custom_write(png_structp png_ptr,
    png_bytep data, png_size_t length);
static void custom_flush(png_structp png_ptr);
static bool suss_color_type( ImFmt fmt, int* color_type );

static void ipng_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt);
static void ipng_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data);
static void ipng_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours);
static ImErr ipng_finish(im_writer* writer);


typedef struct ipng_writer {
    // embedded im_writer
    write_handler* handler;
    ImErr err;
    im_out* out;
    // png-specific
    enum {READY, HEADER, BODY} state;
    png_structp png_ptr;
    png_infop info_ptr;
    ImFmt fmt;
    int w;
    int h;
    int rows_written;
    int num_frames;
} ipng_writer;

static struct write_handler ipng_write_handler = {
    IM_FILEFMT_PNG,
    ipng_begin_img,
    ipng_write_rows,
    ipng_set_palette,
    ipng_finish
};

im_writer* ipng_new_writer(im_out* out, ImErr* err)
{
    ipng_writer* wr = imalloc(sizeof(ipng_writer));
    if (!wr) {
        *err = ERR_NOMEM;
        return NULL;
    }
    wr->handler = &ipng_write_handler;
    wr->err = ERR_NONE;
    wr->out = out;

    wr->state = READY;
    wr->png_ptr = NULL;
    wr->info_ptr = NULL;
    wr->fmt = FMT_RGB;
    wr->w = 0;
    wr->h = 0;
    wr->rows_written = 0;
    wr->num_frames = 0;
    *err = ERR_NONE;
    return (im_writer*)wr;
}

static void ipng_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt)
{
    int color_type;

    ipng_writer* wr = (ipng_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }

    // Ready for image?
    if (wr->state != READY) {
        wr->err = ERR_UNFINISHED_IMG;   // hmm...
        return;
    }

    if (wr->num_frames>0) {
        wr->err = ERR_ANIM_UNSUPPORTED;  // animation not supported.
        return;
    }

    wr->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,NULL,NULL);
    if (!wr->png_ptr) {
        wr->err = ERR_NOMEM;
        return;
    }

    wr->info_ptr = png_create_info_struct(wr->png_ptr);
    if (!wr->info_ptr)
    {
        wr->err = ERR_NOMEM;
        return;
    }

    // TODO - does this cover other functions which call png_* routines?
    if (setjmp(png_jmpbuf(wr->png_ptr)))
    {
       png_destroy_write_struct(&wr->png_ptr, &wr->info_ptr);
       // TODO: check error in im_out?
       wr->err = ERR_EXTLIB;
       return;
    }

    png_set_write_fn(wr->png_ptr,
        (png_voidp)wr->out, custom_write, custom_flush);

    if (!suss_color_type(fmt, &color_type)) {
        wr->err = ERR_UNSUPPORTED;  // unsupported fmt
        return;
    }

    png_set_IHDR(wr->png_ptr, wr->info_ptr,
        (png_uint_32)w,
        (png_uint_32)h,
        8,  //bit_depth
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    wr->w = w;
    wr->h = h;
    wr->fmt = fmt;
    wr->state = HEADER;
}


static void ipng_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours)
{
    png_color rgb[256] = {0};
    png_byte trans[256] = {0};
    int maxtrans = -1;
    int i;

    ipng_writer* wr = (ipng_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }
    if (wr->state == READY) {
        wr->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }
    if (wr->state == BODY) {
        wr->err = ERR_UNFINISHED_IMG;   // should be writing rows.
        return;
    }

    if (num_colours>256) {
        wr->err = ERR_PALETTE_TOO_BIG;
        return;
    }

    // TODO: check to see if we've already set palette?

    if (pal_fmt == PALFMT_RGB) {
        uint8_t const* src = colours;
        for ( i=0; i<num_colours; ++i ) {
            rgb[i].red = *src++;
            rgb[i].green = *src++;
            rgb[i].blue = *src++;
        }
    } else if (pal_fmt == PALFMT_RGBA) {
        uint8_t const* src = colours;
        for ( i=0; i<num_colours; ++i ) {
            rgb[i].red = *src++;
            rgb[i].green = *src++;
            rgb[i].blue = *src++;
            trans[i] = *src++;
            if( trans[i]!=255) {
                maxtrans=i;
            }
        }
    } else {
        wr->err = ERR_UNSUPPORTED;  // unknown palette fmt
        return;
    }

    png_set_PLTE(wr->png_ptr, wr->info_ptr, rgb, num_colours);
    if (maxtrans!=-1) {
        png_set_tRNS(wr->png_ptr, wr->info_ptr, trans, maxtrans+1, NULL);
    }
}


static void ipng_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data)
{
    ipng_writer* wr = (ipng_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }
    if (wr->state == READY) {
        wr->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }

    if (wr->state == HEADER) {
        // write out everything up to the image data
        png_write_info(wr->png_ptr, wr->info_ptr);

        // set up any transforms
        if (wr->fmt == FMT_BGR || wr->fmt == FMT_BGRA) {
            png_set_bgr(wr->png_ptr);
        }

        wr->state = BODY;
        wr->rows_written = 0;
    }

    // Write the rows.
    if (wr->rows_written + num_rows > wr->h) {
        wr->err = ERR_TOO_MANY_ROWS;
        return;
    }

    for (int i = 0; i < num_rows; ++i) {
        png_write_row(wr->png_ptr, (png_const_bytep)data);
        data += im_bytesperpixel(wr->fmt, DT_U8) * wr->w;
        wr->rows_written++;
    }

    // Is that all the rows in the image?

    // Finished the frame?
    if (wr->rows_written >= wr->h) {
        png_write_end(wr->png_ptr, wr->info_ptr);

        wr->state = READY;
        wr->num_frames++;
    }
}


static ImErr ipng_finish(im_writer* writer)
{
    ImErr err;

    ipng_writer* wr = (ipng_writer*)writer;
    if (wr->state != READY) {
        wr->err = ERR_UNFINISHED_IMG;   // expected some rows!
    }

    if( wr->png_ptr) {
       png_destroy_write_struct(&wr->png_ptr, &wr->info_ptr);
    }

    if (wr->out) {
        if (im_out_close(wr->out) < 0 ) {
            if (wr->err == ERR_NONE) {
                wr->err = ERR_FILE;
            }
        }
        wr->out = NULL;
    }

    err = wr->err;
    ifree(wr);
    return err;
}


// calculate png colour type PNG_COLOR_whatever
// return false if unsupported
static bool suss_color_type( ImFmt fmt, int* color_type )
{
    switch (fmt) {
        case FMT_RGB:
        case FMT_BGR:
            *color_type = PNG_COLOR_TYPE_RGB;
            break;
        case FMT_RGBA:
        case FMT_BGRA:
            *color_type = PNG_COLOR_TYPE_RGB_ALPHA;
            break;
        case FMT_COLOUR_INDEX:
            *color_type = PNG_COLOR_TYPE_PALETTE;
            break;
        case FMT_LUMINANCE:
            *color_type = PNG_COLOR_TYPE_GRAY;
            break;
        case FMT_ALPHA:
            return false;
    }
    return true;
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

