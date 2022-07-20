#include "impy.h"
#include "private.h"

//#include <stdlib.h>
#include <string.h>
//#include <stdio.h>
#include <assert.h>

void i_reader_init(im_reader* rdr)
{
    memset(rdr, 0, sizeof(im_reader));
    rdr->err = ERR_NONE;
    rdr->state = READSTATE_READY;
    rdr->external_fmt = IM_FMT_NONE;
}

// Reading

im_reader* im_reader_new(ImFileFmt file_fmt, im_in* in, ImErr* err)
{
    switch (file_fmt) {
        case IM_FILEFMT_GIF:
            return im_new_gif_reader(in, err);
        case IM_FILEFMT_PNG:
            return im_new_generic_reader(iread_png_image, in ,err);
        case IM_FILEFMT_BMP:
            return im_new_generic_reader(iread_bmp_image, in ,err);
        case IM_FILEFMT_JPEG:
            return im_new_generic_reader(iread_jpeg_image, in ,err);
        case IM_FILEFMT_PCX:
            return im_new_generic_reader(iread_pcx_image, in ,err);
        case IM_FILEFMT_TARGA:
            return im_new_generic_reader(iread_targa_image, in ,err);
        default:
           *err = ERR_UNSUPPORTED;
          return NULL; 
    }
}

im_reader* im_reader_open_file(const char* filename, ImErr* err)
{
    im_in* in;
    ImFileFmt file_fmt;
    im_reader* rdr;
    
    // TODO: magic cookie sniffing instead of extension guessing here!
    file_fmt = im_guess_file_format(filename);

    in = im_in_open_file(filename, err);
    if (!in) {
        return NULL;
    }

    rdr = im_reader_new(file_fmt, in, err);
    if (!rdr) {
        im_in_close(in);    // also frees in
        return NULL;
    }

    rdr->in_owned = true;
    return rdr;
}

void im_reader_set_fmt(im_reader* rdr, ImFmt fmt)
{
    assert(rdr->state == READSTATE_HEADER);
    rdr->external_fmt = fmt;

}

bool im_get_img(im_reader* rdr, im_imginfo* info)
{
    bool got;

    if (rdr->err != ERR_NONE) {
        return false;
    }
    if (rdr->state != READSTATE_READY) {
        rdr->err = ERR_BAD_STATE;
        return false;
    }

    got = rdr->handler->get_img(rdr);
    memcpy(info, &rdr->curr, sizeof(im_imginfo));

    rdr->state = READSTATE_HEADER;
    return got;
}

ImErr im_reader_finish(im_reader* rdr)
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
            if (rdr->err != ERR_NONE) {
                rdr->err = ERR_FILE;
            }
        }
        rdr->in = NULL;
    }

    err = rdr->err;
    ifree(rdr);
    return err;
}


ImErr im_reader_err(im_reader* rdr)
    { return rdr->err; }



static void enter_READSTATE_BODY(im_reader* rdr)
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
        rdr->err = ERR_NOCONV;
        return;
    }

    // Need a buffer to perform pixelconversion.
    size_t curr_bytes_per_row = im_fmt_bytesperpixel(rdr->curr.fmt) * rdr->curr.w;
    rdr->rowbuf = irealloc(rdr->rowbuf, curr_bytes_per_row);
    if (!rdr->rowbuf) {
        rdr->err = ERR_NOMEM;
        return;
    }
}

void im_read_rows(im_reader* rdr, unsigned int num_rows, uint8_t* buf)
{
    int i;

    if (rdr->err != ERR_NONE) {
        return;
    }
    if (rdr->state == READSTATE_READY) {
        rdr->err = ERR_BAD_STATE;
        return;
    } else if (rdr->state == READSTATE_HEADER) {
        // start reading.
        enter_READSTATE_BODY(rdr);
    }

    // Are there enough rows left?
    if (rdr->rows_read + num_rows > rdr->curr.h) {
        rdr->err = ERR_TOO_MANY_ROWS;
        return;
    }


    if(rdr->row_cvt_fn) {
        // Pixelconverting. Read one row at a time into rowbuf and convert.
        for (i=0; i<num_rows; ++i) {
            rdr->handler->read_rows(rdr, 1, rdr->rowbuf);
            rdr->row_cvt_fn(rdr->rowbuf, buf, rdr->curr.w, rdr->curr.pal_num_colours, rdr->pal_data);
            buf += im_fmt_bytesperpixel(rdr->external_fmt) * rdr->curr.w;
            rdr->rows_read++;
        }
    } else {
        // No conversion required.
        rdr->handler->read_rows(rdr, num_rows, buf);
        rdr->rows_read += num_rows;
    }

    // Read them all?
    if (rdr->rows_read == rdr->curr.h) {
        rdr->state = READSTATE_READY;
        rdr->frame_num++;
    }
}

void im_read_palette(im_reader* rdr, ImFmt pal_fmt, uint8_t* buf)
{
    if (rdr->err != ERR_NONE) {
        return;
    }
    if (rdr->state != READSTATE_HEADER) {
        rdr->err = ERR_BAD_STATE;
        return;
    }

    if (rdr->curr.pal_num_colours == 0) {
        rdr->err = ERR_NO_PALETTE;
        return;
    }
    if (!im_fmt_has_rgb(pal_fmt)) {
        rdr->err = ERR_NOCONV;  // Not a suitable format for a palette.
        return;
    }

    im_convert_fn cvt_fn = i_pick_convert_fn(IM_FMT_RGBA, pal_fmt);
    if (!cvt_fn) {
        rdr->err = ERR_NOCONV;
        return;
    }
    cvt_fn(rdr->pal_data, buf, rdr->curr.pal_num_colours, 0, NULL);
}


