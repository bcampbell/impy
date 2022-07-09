#include "bmp.h"
#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>


im_writer* ibmp_new_writer(im_out* out, ImErr *err);

static void ibmp_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt);
static void ibmp_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data);
static void ibmp_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours);
static ImErr ibmp_finish(im_writer* writer);

static struct write_handler bmp_write_handler = {
    IM_FILEFMT_BMP,
    ibmp_begin_img,
    ibmp_write_rows,
    ibmp_set_palette,
    ibmp_finish
};

typedef struct ibmp_writer {
    // embedded im_writer:
    write_handler* handler;
    ImErr err;
    im_out* out;
    // bmp-specifc
    enum {READY, HEADER, BODY} state;
    unsigned int num_frames;
    unsigned int w;
    unsigned int h;
    ImFmt fmt;
    size_t bytes_per_row;
    uint8_t* rowbuf;
    im_convert_fn row_cvt_fn;

    unsigned int num_colours;
    uint8_t* colours;

    unsigned int rows_written;

} ibmp_writer;

static void flush_pre_imgdata(ibmp_writer* wr);
static bool write_file_header(size_t fileSize, size_t imageOffset, im_out* out, ImErr* err);
static bool write_bitmapinfoheader(int w, int h,
    size_t imageByteSize,
    int compression, int bitcount, int ncolours,
    im_out* out, ImErr* err);

static bool write_bitmapv4header( int w, int h,
    size_t imageByteSize,
    int compression, int bitcount, int ncolours,
    uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask,
    im_out* out, ImErr* err);



im_writer* ibmp_new_writer(im_out* out, ImErr* err)
{
    ibmp_writer* writer = imalloc(sizeof(ibmp_writer));
    if (!writer) {
        *err = ERR_NOMEM;
        return NULL;
    }
    writer->handler = &bmp_write_handler;
    writer->out = out;
    writer->err = ERR_NONE;
    writer->state = READY;

    writer->fmt = FMT_RGB;
    writer->num_colours = 0;
    writer->colours = NULL;
    writer->w = 0;
    writer->h = 0;
    writer->rows_written = 0;
    writer->num_frames = 0;

    writer->bytes_per_row = 0;
    writer->rowbuf = NULL;
    writer->row_cvt_fn = NULL;
    *err = ERR_NONE;
    return (im_writer*)writer;
}

static void ibmp_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt)
{
    ibmp_writer* wr = (ibmp_writer*)writer;
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

    wr->w = w;
    wr->h = h;
    wr->fmt = fmt;
    wr->bytes_per_row = im_bytesperpixel(fmt, DT_U8) * w;

    switch(fmt) {
        case FMT_RGBA:
        case FMT_BGRA:
            wr->row_cvt_fn = pick_convert_fn(fmt, DT_U8, FMT_BGRA, DT_U8);
            break;
        case FMT_RGB:
        case FMT_BGR:
            wr->row_cvt_fn = pick_convert_fn(fmt, DT_U8, FMT_BGR, DT_U8);
            break;
        case FMT_COLOUR_INDEX:
            wr->row_cvt_fn = NULL;  // no conversion required.
            break;
        default:
            wr->err = ERR_UNSUPPORTED;
            return;
    }
 
    if (wr->row_cvt_fn) {
        // allocate a buffer for format-conversion
        assert(wr->rowbuf == NULL); 
        wr->rowbuf = imalloc(wr->bytes_per_row);
        if (!wr->rowbuf) {
            wr->err = ERR_NOMEM;
            return;
        }
    }

    wr->state = HEADER;
}

static void ibmp_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours)
{
    ibmp_writer* wr = (ibmp_writer*)writer;
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

    if (wr->colours != NULL) {
        ifree(wr->colours);
    }
    wr->colours = imalloc(num_colours*4);
    wr->num_colours = num_colours;

    // bmp expects BGRA palette entries.
    const uint8_t *src = colours;
    uint8_t *dest = wr->colours;
    unsigned int i;
    if (pal_fmt==PALFMT_RGBA) {
        for (i=0; i<num_colours; ++i) {
            dest[2] = *src++;
            dest[1] = *src++;
            dest[0] = *src++;
            dest[3] = *src++;
            dest += 4;
        }
    } else if (pal_fmt==PALFMT_RGB) {
        for (i=0; i<num_colours; ++i) {
            dest[2] = *src++;
            dest[1] = *src++;
            dest[0] = *src++;
            dest[3] = 0;
            dest += 4;
        }
    } else {
        wr->err = ERR_UNSUPPORTED;
        return;
    }
}



static void ibmp_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data)
{
    ibmp_writer* wr = (ibmp_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }
    if (wr->state == READY) {
        wr->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }

    if (wr->state == HEADER) {
        // write out everything up to the image data
        flush_pre_imgdata(wr);
        if (wr->err != ERR_NONE) {
            return;
        }
        wr->state = BODY;
        wr->rows_written = 0;
    }

    // Write the rows.
    if (wr->rows_written + num_rows > wr->h) {
        wr->err = ERR_TOO_MANY_ROWS;
        return;
    }

    for (unsigned int i = 0; i < num_rows; ++i) {
        uint8_t const* src = data;
        if (wr->row_cvt_fn) {
            // pixel conversion required
            wr->row_cvt_fn(data, wr->rowbuf, wr->w);
            src = wr->rowbuf;
        }

        if (im_write(wr->out, src, wr->bytes_per_row) != wr->bytes_per_row) {
           wr->err = ERR_FILE;
           return ;
        }
        data += wr->bytes_per_row;
        wr->rows_written++;
    }

    // Is that all the rows in the image?

    // Finished the frame?
    if (wr->rows_written >= wr->h) {
        wr->state = READY;
        wr->num_frames++;
    }
}

static ImErr ibmp_finish(im_writer* writer)
{
    ImErr err;

    ibmp_writer* wr = (ibmp_writer*)writer;
    if (wr->state != READY) {
        wr->err = ERR_UNFINISHED_IMG;   // expected some rows!
    }

    if (wr->colours) {
        ifree(wr->colours);
    }
    if (wr->rowbuf) {
        ifree(wr->rowbuf);
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

// Write out everything up to the start of the row data itself.
static void flush_pre_imgdata(ibmp_writer* wr)
{
    unsigned int w = wr->w;
    unsigned int h = wr->h;
    size_t paletteByteSize = wr->num_colours * 4;
    size_t imageOffset;
    size_t imageByteSize = h*wr->bytes_per_row;
    size_t fileSize;
    ImErr err;
    int bitcount;
    int compression;
    uint32_t rmask, gmask, bmask, amask;
    size_t dibheadersize = DIB_BITMAPINFOHEADER_SIZE;

    switch(wr->fmt) {
        case FMT_RGB:
        case FMT_BGR:
            compression = BI_RGB;
            bitcount = 24;
            break;
        case FMT_RGBA:
        case FMT_BGRA:
            // use v4 header for alpha support
            dibheadersize = DIB_BITMAPV4HEADER_SIZE;
            compression = BI_BITFIELDS;
            rmask = 0x00ff0000;
            gmask = 0x0000ff00;
            bmask = 0x000000ff;
            amask = 0xff000000;
            bitcount = 32;
            break;
        case FMT_COLOUR_INDEX:
            compression = BI_RGB;
            bitcount = 8;
            break;
        case FMT_ALPHA:
        case FMT_LUMINANCE:
        default:
            wr->err = ERR_UNSUPPORTED;
            return;
    }

    imageOffset = BMP_FILE_HEADER_SIZE + dibheadersize + paletteByteSize;
    fileSize = imageOffset + imageByteSize;
    if(!write_file_header(fileSize, imageOffset, wr->out, &err)) {
        wr->err = err;
        return;
    }

    if (dibheadersize == DIB_BITMAPV4HEADER_SIZE) {
        if(!write_bitmapv4header(w, h, imageByteSize, compression, bitcount, wr->num_colours, rmask, gmask, bmask, amask, wr->out, &err)) {
            wr->err = err;
            return;
        }
    } else {    // DIB_BITMAPINFOHEADER_SIZE
        if(!write_bitmapinfoheader(w, h, imageByteSize, compression, bitcount, wr->num_colours, wr->out, &err)) {
            wr->err = err;
            return;
        }
    }

    // If there's a palette, write it out.
    if (paletteByteSize > 0) {
        if (im_write(wr->out, wr->colours, paletteByteSize) != paletteByteSize) {
            wr->err = ERR_FILE;
            return;
        }
    }
}



/*
typedef struct tagBITMAPFILEHEADER {
  WORD  bfType;
  DWORD bfSize;
  WORD  bfReserved1;
  WORD  bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
*/

static bool write_file_header(size_t fileSize, size_t imageOffset, im_out* out, ImErr* err)
{
    uint8_t buf[BMP_FILE_HEADER_SIZE];

    uint8_t* p = buf;
    *p++ = 'B';
    *p++ = 'M';
    encode_u32le(&p, fileSize);    // total filesize (often 0 for uncompressed?)
    encode_u16le(&p, 0);    // reserved1
    encode_u16le(&p, 0);    // reserved2
    encode_u32le(&p, imageOffset);    // bfOffBits - offset to image data (from start of file)

    assert( p-buf == BMP_FILE_HEADER_SIZE );

    if (im_write(out, buf, BMP_FILE_HEADER_SIZE) != BMP_FILE_HEADER_SIZE) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}


static bool write_bitmapinfoheader(int w, int h,
    size_t imageByteSize,
    int compression, int bitcount, int ncolours,
    im_out* out, ImErr* err)
{
    uint8_t buf[DIB_BITMAPINFOHEADER_SIZE];
    uint8_t* p = buf;
    encode_u32le(&p, DIB_BITMAPINFOHEADER_SIZE);
    encode_s32le(&p, w);
    encode_s32le(&p, h);    // +ve => we'll save as bottom-up
    encode_u16le(&p, 1);                // planes
    encode_u16le(&p, bitcount);         // 8, 24, 32
    encode_u32le(&p, compression);           //biCompression
    encode_u32le(&p, imageByteSize);    //biSizeImage (0 ok for uncompressed)
    encode_s32le(&p, 256);
    encode_s32le(&p, 256);
    encode_u32le(&p, ncolours);                // biClrUsed
    encode_u32le(&p, 0);                // biClrImportant

    assert(p-buf == DIB_BITMAPINFOHEADER_SIZE );

    if (im_write(out, buf, DIB_BITMAPINFOHEADER_SIZE) != DIB_BITMAPINFOHEADER_SIZE) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}


// BITMAPV4HEADER seems to be the first one with decent alpha support
static bool write_bitmapv4header( int w, int h,
    size_t imageByteSize,
    int compression, int bitcount, int ncolours,
    uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask,
    im_out* out, ImErr* err)
{
    uint8_t buf[DIB_BITMAPV4HEADER_SIZE];

    uint8_t* p = buf;
    encode_u32le(&p, DIB_BITMAPV4HEADER_SIZE);
    encode_s32le(&p, w);
    encode_s32le(&p, h);    // +ve => we'll save as bottom-up
    encode_u16le(&p, 1);                // planes
    encode_u16le(&p, bitcount);         // 8, 24, 32
    encode_u32le(&p, compression);           //biCompression
    encode_u32le(&p, imageByteSize);    //biSizeImage (0 ok for uncompressed)
    encode_s32le(&p, 256);
    encode_s32le(&p, 256);
    encode_u32le(&p, ncolours);                // biClrUsed
    encode_u32le(&p, 0);                // biClrImportant

    encode_u32le(&p, rmask);   // DWORD bV4RedMask;
    encode_u32le(&p, gmask);   // DWORD bV4GreenMask;
    encode_u32le(&p, bmask);   // DWORD bV4BlueMask;
    encode_u32le(&p, amask);   // DWORD bV4AlphaMask;
    // DWORD bV4CSType;
    // LCS_WINDOWS_COLOR_SPACE
    *p++ = ' ';
    *p++ = 'n';
    *p++ = 'i';
    *p++ = 'w';

    memset(p, 0, 3*3*4);     //  CIEXYZTRIPLE bV5Endpoints;
    p += 3*3*4;
    encode_u32le(&p, 0);   // DWORD bV4GammaRed;
    encode_u32le(&p, 0);   // DWORD bV4GammaGreen;
    encode_u32le(&p, 0);   // DWORD bV4GammaBlue;
    assert(p-buf == DIB_BITMAPV4HEADER_SIZE );

    if (im_write(out, buf, DIB_BITMAPV4HEADER_SIZE) != DIB_BITMAPV4HEADER_SIZE) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}


