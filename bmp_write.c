#include "bmp.h"
#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>







static bool write_file_header(size_t fileSize, size_t imageOffset, im_writer* out, ImErr* err);
static bool write_bitmap_header(im_img* img, size_t imageByteSize, im_writer* out, ImErr* err);
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

/*
typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG  biWidth;
  LONG  biHeight;
  WORD  biPlanes;
  WORD  biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG  biXPelsPerMeter;
  LONG  biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;
*/

static bool write_bitmap_header(im_img* img, size_t imageByteSize, im_writer* out, ImErr* err)
{
    uint8_t buf[DIB_BITMAPINFOHEADER_SIZE];
    int bitcount;
    ImFmt img_fmt = im_img_format(img);

    switch(img_fmt) {
        case FMT_RGB:
        case FMT_BGR:
            bitcount=24;
            break;
        case FMT_RGBA:
        case FMT_BGRA:
            bitcount=32;
            break;
        case FMT_COLOUR_INDEX:
            bitcount=8;
            break;
        case FMT_ALPHA:
        case FMT_LUMINANCE:
            *err = ERR_UNSUPPORTED;
            return false;
    }

    uint8_t* p = buf;
    encode_u32le(&p, DIB_BITMAPINFOHEADER_SIZE);
    encode_s32le(&p, im_img_w(img));
    encode_s32le(&p, im_img_h(img));    // +ve => we'll save as bottom-up
    encode_u16le(&p, 1);                // planes
    encode_u16le(&p, bitcount);         // 8, 24, 32
    encode_u32le(&p, BI_RGB);           //biCompression
    encode_u32le(&p, imageByteSize);    //biSizeImage (0 ok for uncompressed)
    encode_s32le(&p, 256);
    encode_s32le(&p, 256);
    encode_u32le(&p, 0);                // biClrUsed
    encode_u32le(&p, 0);                // biClrImportant

    assert(p-buf == DIB_BITMAPINFOHEADER_SIZE );

    if (im_write(out, buf, DIB_BITMAPINFOHEADER_SIZE) != DIB_BITMAPINFOHEADER_SIZE) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}


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



bool im_img_write_bmp(im_img* img, im_writer* out, ImErr* err)
{
    if (im_img_datatype(img) != DT_U8) {
        *err = ERR_UNSUPPORTED;
        return false;
    } 

    size_t paletteByteSize = 0;
    size_t imageOffset = BMP_FILE_HEADER_SIZE + DIB_BITMAPINFOHEADER_SIZE + paletteByteSize;
    size_t imageByteSize = im_img_w(img)*im_img_h(img)*im_img_bytesperpixel(img);
    size_t fileSize = imageOffset + imageByteSize;

    if(!write_file_header(fileSize, imageOffset, out, err)) {
        return false;
    }

    if(!write_bitmap_header(img, imageByteSize, out, err)) {
        return false;
    }
    // TODO: write palette here
    if(!write_uncompressed(img, out, err)) {
        return false;
    }

    return true;
}

