#include "impy.h"
#include "private.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>

// Writing

extern im_writer* ibmp_new_writer(im_out* out, ImErr *err); // bmp_write.c
extern im_writer* igif_new_writer(im_out* out, ImErr *err); // gif_write.c
extern im_writer* ipng_new_writer(im_out* out, ImErr* err); // png_write.c

void i_writer_init(im_writer* writer)
{
    memset(writer, 0, sizeof(im_writer));
    writer->err = ERR_NONE;
    writer->state = WRITESTATE_READY;
}


im_writer* im_writer_new(ImFileFmt file_fmt, im_out* out, ImErr* err)
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


ImErr im_writer_finish(im_writer* writer)
{
    ImErr err;
    writer->handler->finish(writer);
    if (writer->out) {
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

ImErr im_writer_err(im_writer* writer)
    { return writer->err; }


im_writer* im_writer_open_file(const char *filename, ImErr* err)
{
    im_out* out;
    ImFileFmt file_fmt;
    
    file_fmt = im_guess_file_format(filename);

    out = im_out_open_file(filename, err);
    if (!out) {
        return NULL;
    }

    return im_writer_new(file_fmt, out, err);
}


void im_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt)
{
    if (writer->err != ERR_NONE) {
        return;
    }

    // Ready for image?
    if (writer->state != WRITESTATE_READY) {
        writer->err = ERR_UNFINISHED_IMG;   // hmm...
        return;
    }
#if 0
    if (writer->num_frames>0) {
        writer->err = ERR_ANIM_UNSUPPORTED;  // animation not supported.
        return;
    }
#endif

    writer->x_offset = 0;
    writer->y_offset = 0;
    writer->w = w;
    writer->h = h;
    writer->fmt = fmt;
    writer->bytes_per_row = im_fmt_bytesperpixel(fmt) * w;

    // assume internal format is same
    writer->internal_fmt = fmt;
    writer->internal_bytes_per_row = writer->bytes_per_row;
    writer->row_cvt_fn = NULL;

    // if backend has a pre_img hook, call it now
    if (writer->handler->pre_img) {
        writer->handler->pre_img(writer);
    }

    // ready to start image
    writer->state = WRITESTATE_HEADER;
}


// set internal_fmt and set up pixelformat conversion if required.
void i_writer_set_internal_fmt(im_writer* writer, ImFmt internal_fmt)
{
    if (internal_fmt == writer->fmt) {
        // Bypass conversion.
        writer->internal_fmt = internal_fmt;
        writer->internal_bytes_per_row = writer->bytes_per_row;
        writer->row_cvt_fn = NULL;
        return;
    }

    if (internal_fmt == FMT_COLOUR_INDEX) {
        writer->err = ERR_NOCONV;   // quantisation would be required
        return;
    }

    writer->row_cvt_fn = pick_convert_fn(internal_fmt, DT_U8, writer->fmt, DT_U8);
    if (writer->row_cvt_fn == NULL) {
        writer->err = ERR_NOCONV;
        return;
    }

    writer->internal_fmt = internal_fmt;
    // (re)allocate the row buffer
    writer->internal_bytes_per_row = im_fmt_bytesperpixel(internal_fmt) * writer->w;
    writer->rowbuf = irealloc(writer->rowbuf, writer->internal_bytes_per_row);
    if (!writer->rowbuf) {
        writer->err = ERR_NOMEM;
        return;
    }
}


void im_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data)
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

    if (writer->row_cvt_fn == NULL) {
        // no conversion required.
        writer->handler->emit_rows(writer, num_rows, data);
        writer->rows_written += num_rows;
    } else {
        // convert and write one row at a time.
        for (unsigned int i = 0; i < num_rows; ++i) {
            writer->row_cvt_fn(data, writer->rowbuf, writer->w);
            writer->handler->emit_rows(writer, 1, writer->rowbuf);
            data += writer->bytes_per_row;
            writer->rows_written++;
        }
    }
    // Is that all the rows in the image?

    // Finished the frame?
    if (writer->rows_written >= writer->h) {
        writer->state = WRITESTATE_READY;
        writer->num_frames++;
        // if the handler has a post_img hook, call it now
        if (writer->handler->post_img) {
            writer->handler->post_img(writer);
        }
    }
}

void im_set_palette(im_writer* wr, ImFmt pal_fmt, unsigned int num_colours, const uint8_t *colours)
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

    // If backend hasn't expressed a preference, take whatever is given.
    if (wr->pal_fmt == IM_FMT_NONE) {
        wr->pal_fmt = pal_fmt;
    }
   
    im_convert_fn cvt = pick_convert_fn(pal_fmt, DT_U8, wr->pal_fmt, DT_U8);
    if (!cvt) {
        wr->err = ERR_NOCONV;   // No suitable palette conversion.
        return;
    }

    // Allocate and copy/convert
    size_t byte_cnt = num_colours * im_fmt_bytesperpixel(wr->pal_fmt);
    wr->pal_data = irealloc(wr->pal_data, byte_cnt);
    cvt(colours, wr->pal_data, num_colours);
    wr->pal_num_colours = num_colours;
}

