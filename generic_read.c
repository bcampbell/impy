#include "impy.h"
#include "private.h"

#include <assert.h>
//#include <stdio.h>
#include <string.h>
//#include <limits.h>

static bool generic_get_img(im_reader* reader, im_imginfo* info);
static void generic_read_rows(im_reader* reader, unsigned int num_rows, uint8_t* buf);
static void generic_read_palette(im_reader* reader, uint8_t *buf);
static ImErr generic_reader_finish(im_reader* reader);


// Helper to simplify loaders which just slurp in a single im_img.
typedef struct generic_reader {
    // im_reader members
    read_handler* handler;
    ImErr err;
    im_in* in;

    // type-specific fields from here on
    enum {READY, HEADER, BODY} state;
    im_img* img;
    unsigned int rows_read;
    im_img* (*load_single)(im_in *in, ImErr *err);
} generic_reader;

//im_img* (load_single)(im_in *in, ImErr *err);


static read_handler generic_read_handler = {
    generic_get_img,
    generic_read_rows,
    generic_read_palette,
    generic_reader_finish
};

im_reader* im_new_generic_reader(im_img* (*load_single)(im_in *, ImErr *), im_in* in, ImErr* err )
{
    generic_reader *rdr = imalloc(sizeof(generic_reader));
    if (!rdr) {
        *err = ERR_NOMEM;
        return NULL;
    }

    // im_reader fields
    rdr->handler = &generic_read_handler;
    rdr->err = ERR_NONE;
    rdr->in = in;

    // type-specific fields
    rdr->state = READY;
    rdr->img = NULL;
    rdr->rows_read = 0;
    rdr->load_single = load_single;

    return (im_reader*)rdr;
}


static bool generic_get_img(im_reader* reader, im_imginfo* info)
{
    generic_reader *rdr = (generic_reader*)reader;
    if (rdr->err != ERR_NONE) {
        return false;
    }

    if (rdr->state != READY) {
        rdr->err = ERR_BAD_STATE;
        return false;
    }

    if (rdr->img) {
        // No more frames.
        return false;
    }

    // Perform the load.
    rdr->img = rdr->load_single(rdr->in, &rdr->err);
    if (!rdr->img) {
        return false;
    }

    info->w = rdr->img->w;
    info->h = rdr->img->h;
    info->x_offset = rdr->img->x_offset;
    info->y_offset = rdr->img->y_offset;
    info->fmt = rdr->img->format;
    info->pal_num_colours = rdr->img->pal_num_colours;
    info->pal_fmt = rdr->img->pal_fmt;

    rdr->state = HEADER;
    return true;
}

static void generic_read_rows(im_reader* reader, unsigned int num_rows, uint8_t* buf)
{
    generic_reader *rdr = (generic_reader*)reader;
    if (rdr->err != ERR_NONE) {
        return;
    }
    if (rdr->state == READY) {
        rdr->err = ERR_BAD_STATE;
        return;
    }

    if (rdr->state == HEADER) {
        // start reading.
        rdr->state = BODY;
        rdr->rows_read = 0;
    }

    assert(rdr->state == BODY);
    {
        size_t bytes_per_row;
        im_img* img = rdr->img;
        assert(img);

        bytes_per_row = im_img_bytesperpixel(img) * img->w;
        if (rdr->rows_read + num_rows > img->h) {
            rdr->err = ERR_TOO_MANY_ROWS;
            return;
        }
        for (unsigned int row = 0; row < num_rows; ++row) {
            unsigned int y = rdr->rows_read + row;
            memcpy(buf, im_img_row(img, y), bytes_per_row);
            buf += bytes_per_row;
        }
        rdr->rows_read += num_rows;

        // Read them all?
        if (rdr->rows_read == img->h) {
            rdr->state = READY;
        }
    }
}

static void generic_read_palette(im_reader* reader, uint8_t *buf)
{
    generic_reader *rdr = (generic_reader*)reader;

    if (rdr->err != ERR_NONE) {
        return;
    }
    if (rdr->state != HEADER) {
        rdr->err = ERR_BAD_STATE;
        return;
    }

    {
        im_img *img = rdr->img;
        assert(img);

        if (img->pal_num_colours > 0) {
            size_t s = (img->pal_fmt == PALFMT_RGBA) ? 4 : 3;
            memcpy(buf, img->pal_data, img->pal_num_colours * s);
        }
    }
}

static ImErr generic_reader_finish(im_reader* reader)
{
    ImErr err;
    generic_reader *rdr = (generic_reader*)reader;

    if(rdr->img) {
        im_img_free(rdr->img);
        rdr->img = NULL;
    }

    if (rdr->in) {
        if( im_in_close(rdr->in) < 0) {
            if (rdr->err != ERR_NONE) {
                rdr->err = ERR_FILE;
            }
        }
    }

    err = rdr->err;
    ifree(rdr);
    return err;
}


