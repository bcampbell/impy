#include "impy.h"
#include "private.h"

//#include <stdlib.h>
#include <string.h>
//#include <stdio.h>
#include <assert.h>

void i_read_init(im_read* rdr)
{
    memset(rdr, 0, sizeof(im_read));
    rdr->err = IM_ERR_NONE;
    rdr->state = READSTATE_READY;
    rdr->external_fmt = IM_FMT_NONE;
}


static const i_read_handler* read_handlers[] = {
    &i_gif_read_handler,
    &i_png_read_handler,
    &i_bmp_read_handler,
    &i_jpeg_read_handler,
    &i_pcx_read_handler,
    &i_targa_read_handler,
    NULL
};



// Reading



ImFiletype im_sniff_filetype(im_in *in)
{
    const i_read_handler** h;
 
    // is_iff requires 12 bytes...
    uint8_t cookie[16];
    if (im_in_read(in, cookie, sizeof(cookie)) != sizeof(cookie)) {
        return IM_FILETYPE_UNKNOWN;
    }

    // reset reader
    if (im_in_seek(in, 0, IM_SEEK_SET) != 0 ) {
        return IM_FILETYPE_UNKNOWN;
    }

    for (h = read_handlers; *h; ++h) {
        if ((*h)->match_cookie(cookie, sizeof(cookie))) {
            return (*h)->file_format;
        }
    }
    return IM_FILETYPE_UNKNOWN;
}

im_read* im_read_new(ImFiletype file_fmt, im_in* in, ImErr* err)
{
    const i_read_handler** h;

    if (file_fmt == IM_FILETYPE_UNKNOWN) {
        file_fmt = im_sniff_filetype(in);
    }

    for (h = read_handlers; *h; ++h) {
        if ((*h)->file_format == file_fmt) {
            return (*h)->create(in, err);
        }
    }
    *err = IM_ERR_UNSUPPORTED;
    return NULL;
}

im_read* im_read_open_file(const char* filename, ImErr* err)
{
    im_in* in;
    ImFiletype file_fmt;
    im_read* rdr;
    

    in = im_in_open_file(filename, err);
    if (!in) {
        return NULL;
    }

    file_fmt = im_sniff_filetype(in);
    if (file_fmt == IM_FILETYPE_UNKNOWN) {
        // Fall back to using filename...
        // eg Targa has no magic cookie matching...
        file_fmt = im_filetype_from_filename(filename);
    }
    rdr = im_read_new(file_fmt, in, err);
    if (!rdr) {
        im_in_close(in);    // also frees in
        return NULL;
    }

    // Indicate we want `in` closed and freed when reader is finished.
    rdr->in_owned = true;
    return rdr;
}

bool im_read_img(im_read* rdr, im_imginfo* info)
{
    bool got;

    if (rdr->err != IM_ERR_NONE) {
        return false;
    }
    if (rdr->state != READSTATE_READY) {
        rdr->err = IM_ERR_BAD_STATE;
        return false;
    }

    got = rdr->handler->get_img(rdr);
    memcpy(info, &rdr->curr, sizeof(im_imginfo));

    rdr->state = READSTATE_HEADER;
    return got;
}

void im_read_set_fmt(im_read* rdr, ImFmt fmt)
{
    if (rdr->err != IM_ERR_NONE) {
        return;
    }

    // make sure im_get_img() was called first.
    if (rdr->state != READSTATE_HEADER) {
        rdr->err = IM_ERR_BAD_STATE;
        return;
    }
    rdr->external_fmt = fmt;
}

ImErr im_read_finish(im_read* rdr)
{
    ImErr err;
    // Perform any required format-specific cleanup.
    rdr->handler->finish(rdr);

    if (rdr->rowbuf) {
        ifree(rdr->rowbuf);
        rdr->rowbuf = NULL;
    }

    if (rdr->in && rdr->in_owned) {
        // Close and free `in`.
        if (im_in_close(rdr->in) < 0) {
            if (rdr->err != IM_ERR_NONE) {
                rdr->err = IM_ERR_FILE;
            }
        }
        rdr->in = NULL;
    }

    err = rdr->err;
    ifree(rdr);
    return err;
}


ImErr im_read_err(im_read* rdr)
    { return rdr->err; }



static void enter_READSTATE_BODY(im_read* rdr)
{
    rdr->state = READSTATE_BODY;
    rdr->rows_read = 0;

    // If no pixelformat was requested, serve up whatever the backend provides.
    if (rdr->external_fmt == IM_FMT_NONE) {
        rdr->external_fmt = rdr->curr.fmt;
    }

    // Nice and simple if no conversion required.
    if (rdr->external_fmt == rdr->curr.fmt) {
        rdr->row_cvt_fn = NULL;
        return;
    }

    rdr->row_cvt_fn = i_pick_convert_fn(rdr->curr.fmt, rdr->external_fmt); 
    if (rdr->row_cvt_fn == NULL) {
        rdr->err = IM_ERR_NOCONV;
        return;
    }

    // Need to perform pixelconversion. So need a buffer big enough to read
    // in a row, in our internal pixel format.
    size_t src_bytes_per_row = im_fmt_bytesperpixel(rdr->curr.fmt) * rdr->curr.w;
    rdr->rowbuf = irealloc(rdr->rowbuf, src_bytes_per_row);
    if (!rdr->rowbuf) {
        rdr->err = IM_ERR_NOMEM;
        return;
    }
}

void im_read_rows(im_read *rdr, unsigned int num_rows, void *buf, int stride)
{
    int i;

    if (rdr->err != IM_ERR_NONE) {
        return;
    }
    if (rdr->state == READSTATE_READY) {
        rdr->err = IM_ERR_BAD_STATE;
        return;
    } else if (rdr->state == READSTATE_HEADER) {
        // start reading.
        enter_READSTATE_BODY(rdr);
    }

    // Are there enough rows left?
    if (rdr->rows_read + num_rows > rdr->curr.h) {
        rdr->err = IM_ERR_TOO_MANY_ROWS;
        return;
    }


    if(rdr->curr.fmt == rdr->external_fmt) {
        // No conversion required.
        rdr->handler->read_rows(rdr, num_rows, buf, stride);
        rdr->rows_read += num_rows;
    } else {
        // Pixelconverting. Read one row at a time into rowbuf and convert.
        size_t src_bytes_per_row = im_fmt_bytesperpixel(rdr->curr.fmt) * rdr->curr.w;
        assert(rdr->row_cvt_fn != NULL);
        for (i=0; i<num_rows; ++i) {
            rdr->handler->read_rows(rdr, 1, rdr->rowbuf, src_bytes_per_row);
            rdr->row_cvt_fn(rdr->rowbuf, buf, rdr->curr.w, rdr->curr.pal_num_colours, rdr->pal_data);
            buf += stride;
            rdr->rows_read++;
        }
    }

    // Read them all?
    if (rdr->rows_read == rdr->curr.h) {
        rdr->state = READSTATE_READY;
        rdr->frame_num++;
    }
}

void im_read_palette(im_read* rdr, ImFmt pal_fmt, uint8_t* buf)
{
    if (rdr->err != IM_ERR_NONE) {
        return;
    }
    if (rdr->state != READSTATE_HEADER) {
        rdr->err = IM_ERR_BAD_STATE;
        return;
    }

    if (rdr->curr.pal_num_colours == 0) {
        rdr->err = IM_ERR_NO_PALETTE;
        return;
    }
    if (!im_fmt_has_rgb(pal_fmt)) {
        rdr->err = IM_ERR_NOCONV;  // Not a suitable format for a palette.
        return;
    }

    im_convert_fn cvt_fn = i_pick_convert_fn(IM_FMT_RGBA, pal_fmt);
    if (!cvt_fn) {
        rdr->err = IM_ERR_NOCONV;
        return;
    }
    cvt_fn(rdr->pal_data, buf, rdr->curr.pal_num_colours, 0, NULL);
}


