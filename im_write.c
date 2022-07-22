#include "impy.h"
#include "private.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>
#include <assert.h>

// Writing

extern im_write* ibmp_new_writer(im_out* out, ImErr *err); // bmp_write.c
extern im_write* igif_new_writer(im_out* out, ImErr *err); // gif_write.c
extern im_write* ipng_new_writer(im_out* out, ImErr* err); // png_write.c

void i_write_init(im_write* writer)
{
    memset(writer, 0, sizeof(im_write));
    writer->err = ERR_NONE;
    writer->state = WRITESTATE_READY;
}


im_write* im_write_new(ImFileFmt file_fmt, im_out* out, ImErr* err)
{
    switch (file_fmt) {
        case IM_FILEFMT_PNG:
            return ipng_new_writer(out, err);
        case IM_FILEFMT_GIF:
            return igif_new_writer(out, err);
        case IM_FILEFMT_BMP:
            return ibmp_new_writer(out, err);
        default:
           *err = ERR_UNSUPPORTED;
          return NULL; 
    }
}

ImErr im_write_finish(im_write* writer)
{
    ImErr err;

    // TODO - check for unfinished images and set error!

    writer->handler->finish(writer);
    if (writer->out && writer->out_owned) {
        // Close and free `out`.
        if (im_out_close(writer->out) < 0 ) {
            if (writer->err == ERR_NONE) {
                writer->err = ERR_FILE;
            }
        }
        writer->out = NULL;
    }

    if( writer->rowbuf) {
        ifree(writer->rowbuf);
        writer->rowbuf = NULL;
    }
    if( writer->pal_data) {
        ifree(writer->pal_data);
        writer->pal_data = NULL;
    }

    err = writer->err;
    ifree(writer);
    return err;
}

ImErr im_write_err(im_write* writer)
    { return writer->err; }


im_write* im_write_open_file(const char *filename, ImErr* err)
{
    im_out* out;
    ImFileFmt file_fmt;
    im_write* writer;
    
    file_fmt = im_guess_file_format(filename);

    out = im_out_open_file(filename, err);
    if (!out) {
        return NULL;
    }

    writer = im_write_new(file_fmt, out, err);
    if (!writer) {
        // Close and free.
        im_out_close(out);
        return NULL;
    }
    writer->out_owned = true;
    return writer;
}


void im_write_img(im_write* writer, unsigned int w, unsigned int h, ImFmt fmt)
{
    if (writer->err != ERR_NONE) {
        return;
    }

    // Ready for image?
    if (writer->state != WRITESTATE_READY) {
        writer->err = ERR_UNFINISHED_IMG;   // hmm...
        return;
    }

    writer->x_offset = 0;
    writer->y_offset = 0;
    writer->w = w;
    writer->h = h;
    writer->fmt = fmt;

    // Assume internal format is same (but backend can call
    // i_write_set_internal_fmt() to chnage this).
    writer->internal_fmt = fmt;
    writer->row_cvt_fn = NULL;

    // if backend has a pre_img hook, call it now
    if (writer->handler->pre_img) {
        writer->handler->pre_img(writer);
    }

    // ready to start image
    writer->state = WRITESTATE_HEADER;
}


// set internal_fmt and set up pixelformat conversion if required.
void i_write_set_internal_fmt(im_write* writer, ImFmt internal_fmt)
{
    if (internal_fmt == writer->fmt) {
        // Same format. Bypass conversion.
        writer->internal_fmt = internal_fmt;
        writer->row_cvt_fn = NULL;
        return;
    }

    writer->row_cvt_fn = i_pick_convert_fn(internal_fmt, writer->fmt);
    if (writer->row_cvt_fn == NULL) {
        writer->err = ERR_NOCONV;
        return;
    }

    writer->internal_fmt = internal_fmt;
    // (re)allocate the row buffer - enough for the caller to write in a row in
    // the external pixelformat.
    writer->rowbuf = irealloc(writer->rowbuf, im_fmt_bytesperpixel(writer->fmt) * writer->w);
    if (!writer->rowbuf) {
        writer->err = ERR_NOMEM;
        return;
    }
}


void im_write_rows(im_write *writer, unsigned int num_rows, const void *data, int stride)
{
    if (writer->err != ERR_NONE) {
        return;
    }
    if (writer->state == WRITESTATE_READY) {
        writer->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }

    if (writer->state == WRITESTATE_HEADER) {
        // write out everything up to the image data
        writer->handler->emit_header(writer);
        if (writer->err != ERR_NONE) {
            return;
        }
        writer->state = WRITESTATE_BODY;
        writer->rows_written = 0;
    }

    // Write the rows.
    if (writer->rows_written + num_rows > writer->h) {
        writer->err = ERR_TOO_MANY_ROWS;
        return;
    }

    if (writer->fmt == writer->internal_fmt) {
        // No conversion required.
        writer->handler->emit_rows(writer, num_rows, data, stride);
        writer->rows_written += num_rows;
    } else {
        // Convert from the incoming format to our internal format, one row at
        // a time.
        int dest_bytes_per_row = im_fmt_bytesperpixel(writer->internal_fmt) * writer->w;
        assert(writer->row_cvt_fn != NULL);
        for (unsigned int i = 0; i < num_rows; ++i) {
            writer->row_cvt_fn(data, writer->rowbuf, writer->w, writer->pal_num_colours, writer->pal_data);
            data += stride;
            writer->handler->emit_rows(writer, 1, writer->rowbuf, dest_bytes_per_row);
            writer->rows_written++;
        }
    }

    // Finished the image?
    if (writer->rows_written >= writer->h) {
        writer->state = WRITESTATE_READY;
        writer->num_frames++;
        // If the handler has a post_img hook, call it now.
        if (writer->handler->post_img) {
            writer->handler->post_img(writer);
        }
    }
}

void im_write_palette(im_write* wr, ImFmt pal_fmt, unsigned int num_colours, const uint8_t *colours)
{
    if (wr->err != ERR_NONE) {
        return;
    }
    if (wr->state == WRITESTATE_READY) {
        wr->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }
    if (wr->state == WRITESTATE_BODY) {
        wr->err = ERR_UNFINISHED_IMG;   // should be writing rows.
        return;
    }

    if (num_colours>256) {
        wr->err = ERR_PALETTE_TOO_BIG;
        return;
    }

    // Internally we always store as RGBA 
    im_convert_fn cvt = i_pick_convert_fn(pal_fmt, IM_FMT_RGBA);
    if (!cvt) {
        wr->err = ERR_NOCONV;   // No suitable palette conversion.
        return;
    }

    // Allocate and copy/convert
    size_t byte_cnt = num_colours * im_fmt_bytesperpixel(IM_FMT_RGBA);
    wr->pal_data = irealloc(wr->pal_data, byte_cnt);
    cvt(colours, wr->pal_data, num_colours, 0, NULL);
    wr->pal_num_colours = num_colours;
}

