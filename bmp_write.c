#include "bmp.h"
#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>


im_writer* ibmp_new_writer(im_out* out, ImErr *err);

static void ibmp_prep_img(im_writer* writer);
static void ibmp_emit_header(im_writer* wr);
static void ibmp_emit_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data);
static void ibmp_finish(im_writer* wr);

static struct write_handler bmp_write_handler = {
    IM_FILEFMT_BMP,
    ibmp_prep_img,
    ibmp_emit_header,
    ibmp_emit_rows,
    NULL,   // post_img()
    ibmp_finish
};

// no bmp-specific fields.
typedef struct ibmp_writer {
    im_writer writer;
} ibmp_writer;


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
    ibmp_writer* bmpwriter = imalloc(sizeof(ibmp_writer));
    if (!bmpwriter) {
        *err = ERR_NOMEM;
        return NULL;
    }
    im_writer* writer = (im_writer*)bmpwriter;

    i_writer_init(writer);

    writer->handler = &bmp_write_handler;
    writer->out = out;

    *err = ERR_NONE;
    return writer;
}



static void ibmp_emit_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data)
{
    size_t cnt = writer->internal_bytes_per_row * num_rows;
    assert(writer->state == WRITESTATE_BODY);
    if (im_write(writer->out, data, cnt) != cnt) 
    {
        writer->err = ERR_FILE;
        return;
    }
}

static void ibmp_finish(im_writer* writer)
{
}

static void ibmp_prep_img(im_writer* writer)
{
    if (writer->num_frames > 0 ) {
        writer->err = ERR_ANIM_UNSUPPORTED;
        return;
    }
}

// Write out everything up to the start of the row data itself.
static void ibmp_emit_header(im_writer* wr)
{
    unsigned int w = wr->w;
    unsigned int h = wr->h;
    size_t paletteByteSize = wr->pal_num_colours * 4;   // TODO!!!
    size_t imageOffset;
    size_t imageByteSize = h*wr->bytes_per_row;
    size_t fileSize;
    ImErr err;
    int bitcount;
    int compression;
    uint32_t rmask, gmask, bmask, amask;
    size_t dibheadersize = DIB_BITMAPINFOHEADER_SIZE;

    // Figure out what format we're going to write out, and ask
    // that im_write_rows() gives us that.
    if (im_fmt_is_indexed(wr->fmt)) {
        // paletted
        compression = BI_RGB;
        bitcount = 8;
        i_writer_set_internal_fmt(wr, IM_FMT_INDEX8);
    } else if(im_fmt_has_rgb(wr->fmt)) {
        if (im_fmt_has_alpha(wr->fmt)) {
            // use v4 header for alpha support
            dibheadersize = DIB_BITMAPV4HEADER_SIZE;
            compression = BI_BITFIELDS;
            rmask = 0x00ff0000;
            gmask = 0x0000ff00;
            bmask = 0x000000ff;
            amask = 0xff000000;
            bitcount = 32;
            i_writer_set_internal_fmt(wr, IM_FMT_BGRA);
        } else {
            // no alpha channel
            compression = BI_RGB;
            bitcount = 24;
            i_writer_set_internal_fmt(wr, IM_FMT_BGR);
        }
    } else {
        wr->err = ERR_UNSUPPORTED;
        return;
    }

    if( wr->err != ERR_NONE) {
        return;
    }

    imageOffset = BMP_FILE_HEADER_SIZE + dibheadersize + paletteByteSize;
    fileSize = imageOffset + imageByteSize;
    if(!write_file_header(fileSize, imageOffset, wr->out, &err)) {
        wr->err = err;
        return;
    }

    if (dibheadersize == DIB_BITMAPV4HEADER_SIZE) {
        if(!write_bitmapv4header(w, h, imageByteSize, compression, bitcount, wr->pal_num_colours, rmask, gmask, bmask, amask, wr->out, &err)) {
            wr->err = err;
            return;
        }
    } else {    // DIB_BITMAPINFOHEADER_SIZE
        if(!write_bitmapinfoheader(w, h, imageByteSize, compression, bitcount, wr->pal_num_colours, wr->out, &err)) {
            wr->err = err;
            return;
        }
    }

    // If there's a palette, write it out.
    if (wr->pal_num_colours > 0) {
        // Write palette as BGRA
        im_convert_fn pal_cvt_fn = i_pick_convert_fn(IM_FMT_RGBA, IM_FMT_BGRA);
        if (!pal_cvt_fn) {
            wr->err = ERR_NOCONV;
            return;
        }
        size_t bufsize = im_fmt_bytesperpixel(IM_FMT_BGRA) * wr->pal_num_colours;
        uint8_t* buf = imalloc(bufsize);
        if (!buf) {
            wr->err = ERR_NOMEM;
            return;
        }
        pal_cvt_fn(wr->pal_data, buf, wr->pal_num_colours, 0, NULL);
        if (im_write(wr->out, buf, bufsize) != bufsize) {
            wr->err = ERR_FILE;
        }
        if (buf) {
            ifree(buf);
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


