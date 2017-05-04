#include "bmp.h"
#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>







static bool write_file_header(size_t fileSize, size_t imageOffset, im_writer* out, ImErr* err);
static bool write_bitmapinfoheader(int w, int h,
    size_t imageByteSize,
    int compression, int bitcount, int ncolours,
    im_writer* out, ImErr* err);

static bool write_bitmapv4header( int w, int h,
    size_t imageByteSize,
    int compression, int bitcount, int ncolours,
    uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask,
    im_writer* out, ImErr* err);

static bool write_uncompressed(im_img* img, im_writer* out, ImErr* err);


/*
typedef struct tagBITMAPFILEHEADER {
  WORD  bfType;
  DWORD bfSize;
  WORD  bfReserved1;
  WORD  bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
*/

static bool write_file_header(size_t fileSize, size_t imageOffset, im_writer* out, ImErr* err)
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
    im_writer* out, ImErr* err)
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
    im_writer* out, ImErr* err)
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


static bool write_colour_table(im_img* img, im_writer* out, ImErr* err)
{
    uint8_t buf[256*4];
    int ncolours = im_img_pal_num_colours(img);
    ImPalFmt palfmt = im_img_pal_fmt(img);

    const uint8_t *src = im_img_pal_data(img);
    uint8_t *dest = buf;
    int i;
    if (palfmt==PALFMT_RGBA) {
        for (i=0; i<ncolours; ++i) {
            *dest++ = src[2]; // b
            *dest++ = src[1]; // g
            *dest++ = src[0]; // r
            *dest++ = src[3]; // a
            src += 4;
        }
    } else if (palfmt==PALFMT_RGB) {
        for (i=0; i<ncolours; ++i) {
            *dest++ = src[2]; // b
            *dest++ = src[1]; // g
            *dest++ = src[0]; // r
            *dest++ = 0;    // x
            src += 3;
        }
    } else {
        *err = ERR_UNSUPPORTED;
        return false;
    }

    if (im_write(out,buf,ncolours*4) != ncolours*4) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}

static void copy_indexed_data( const uint8_t* src, uint8_t* dest, int w)
    { memcpy(dest, src, w); }


// write uncompressed image data
static bool write_uncompressed(im_img* img, im_writer* out, ImErr* err)
{
    int x,y;
    int w = im_img_w(img);
    int h = im_img_h(img);
    uint8_t* linebuf = NULL;
    size_t linesize;
    ImFmt srcFmt = im_img_format(img);
    im_convert_fn cvt;

    switch(srcFmt) {
        case FMT_RGBA:
        case FMT_BGRA:
            cvt = pick_convert_fn(srcFmt, im_img_datatype(img), FMT_BGRA, DT_U8);
            break;
        case FMT_RGB:
        case FMT_BGR:
            cvt = pick_convert_fn(srcFmt, im_img_datatype(img), FMT_BGR, DT_U8);
            break;
        case FMT_COLOUR_INDEX:
            cvt = copy_indexed_data;
            break;
        default:
            cvt = NULL;
            break;
    }
   
    if (!cvt) { 
        *err = ERR_UNSUPPORTED;
        return false;
    }

    linesize = im_img_bytesperpixel(img)*w;
    linebuf = imalloc(linesize);
    if (!linebuf) {
        *err = ERR_NOMEM;
        return false;
    }

    for (y=h-1; y>=0; --y) {
        const uint8_t* src = im_img_row(img,y);
        cvt(src,linebuf,w);
        if (im_write( out, linebuf, linesize) != linesize) {
            *err = ERR_FILE;
            ifree(linebuf);
            return false;
        }
    }

    // success!
    ifree(linebuf);
    return true;
}


// write a img out to a .bmp file
bool im_img_write_bmp(im_img* img, im_writer* out, ImErr* err)
{

    int w = im_img_w(img);
    int h = im_img_h(img);
    int paletteColours = 0;
    size_t paletteByteSize = 0;
    size_t imageOffset;
    size_t imageByteSize = w*h*im_img_bytesperpixel(img);
    size_t fileSize;

    int bitcount;
    int compression;
    uint32_t rmask, gmask, bmask, amask;
    ImFmt img_fmt = im_img_format(img);
    size_t dibheadersize = DIB_BITMAPINFOHEADER_SIZE;

    if (im_img_datatype(img) != DT_U8) {
        *err = ERR_UNSUPPORTED;
        return false;
    } 

    switch(img_fmt) {
        case FMT_RGB:
        case FMT_BGR:
            compression=BI_RGB;
            bitcount=24;
            break;
        case FMT_RGBA:
        case FMT_BGRA:
            // use v4 header for alpha support
            dibheadersize = DIB_BITMAPV4HEADER_SIZE;
            compression=BI_BITFIELDS;
            rmask = 0x00ff0000;
            gmask = 0x0000ff00;
            bmask = 0x000000ff;
            amask = 0xff000000;
            bitcount=32;
            break;
        case FMT_COLOUR_INDEX:
            compression=BI_RGB;
            bitcount=8;
            paletteColours = im_img_pal_num_colours(img);
            if (paletteColours>256) {
                paletteColours = 256;
            }
            break;
        case FMT_ALPHA:
        case FMT_LUMINANCE:
            *err = ERR_UNSUPPORTED;
            return false;
    }

    paletteByteSize = paletteColours * 4;
    imageOffset = BMP_FILE_HEADER_SIZE + dibheadersize + paletteByteSize;
    fileSize = imageOffset + imageByteSize;
    if(!write_file_header(fileSize, imageOffset, out, err)) {
        return false;
    }

    if (dibheadersize == DIB_BITMAPV4HEADER_SIZE) {
        if(!write_bitmapv4header(w, h, imageByteSize, compression, bitcount, paletteColours, rmask, gmask, bmask, amask, out, err)) {
            return false;
        }
    } else {    // DIB_BITMAPINFOHEADER_SIZE
        if(!write_bitmapinfoheader(w, h, imageByteSize, compression, bitcount, paletteColours, out, err)) {
            return false;
        }
    }

    // TODO: write palette here
    if (paletteColours>0) {
        if (!write_colour_table(img, out, err)) {
            return false;
        }
    }
    if(!write_uncompressed(img, out, err)) {
        return false;
    }

    return true;
}

