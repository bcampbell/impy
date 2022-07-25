#include "bmp.h"
#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>



//
// bmp format reference:
// https://en.wikipedia.org/wiki/BMP_file_format
// https://msdn.microsoft.com/en-us/library/dd183391(v=vs.85).aspx
// http://www.fileformat.info/format/bmp/egff.htm
//
// Good rle8/rel4 explanation:
// http://www.binaryessence.com/dct/en000073.htm
//
// sample files:
//
// http://fileformats.archiveteam.org/wiki/BMP#Sample_files
// http://entropymine.com/jason/bmpsuite/
// (bmpsuite really is rather amazing and thorough)
//

static bool bmp_match_cookie(const uint8_t* buf, int nbytes);
static im_read* bmp_read_create(im_in *in, ImErr *err);
static im_img* iread_bmp_image(im_in* in, ImErr* err);

i_read_handler i_bmp_read_handler = {
    IM_FILETYPE_BMP,
    bmp_match_cookie,
    bmp_read_create,
    i_generic_read_img,
    i_generic_read_rows,
    i_generic_read_finish
};

static bool bmp_match_cookie(const uint8_t* buf, int nbytes)
{
    assert(nbytes >= 2);
    return buf[0] == 'B' && buf[1] == 'M';
}

static im_read* bmp_read_create(im_in *in, ImErr *err)
{
    return i_new_generic_reader(iread_bmp_image, &i_bmp_read_handler, in ,err);
}

typedef struct bmp_state {
    uint8_t fileheader[BMP_FILE_HEADER_SIZE];

    // values from BITMAPFILEHEADER
    size_t filesize;
    size_t image_offset;

    // values parsed (or inferred) from bitmap header
    size_t headersize;
    int w;
    int h;
    bool topdown;
    int bitcount;
    int compression;
    size_t imagesize;   // size of image data (for compressed fmts only)
    int ncolours;
    uint32_t mask[4];   // r,g,b,a

    // buffer to stash src palette data
    uint8_t rawcolours[256*4];

    // a buffer to load/decode a single line of the image
    size_t srclinesize;    // including padding
    uint8_t* linebuf;
} bmp_state;


static bool read_file_header(bmp_state *bmp, im_in* in, ImErr* err);
static bool read_bitmap_header(bmp_state *bmp, im_in* in, ImErr* err);
static bool read_colour_table(bmp_state* bmp, im_in* in, ImErr* err);
static im_img* read_image(bmp_state* bmp, im_in* in, ImErr* err);
static bool cook_colour_table(bmp_state* bmp, im_img* img, ImErr* err);
static bool read_img_16_BI_BITFIELDS( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_24_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_32_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_32_BI_BITFIELDS( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_packed_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_8_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_BI_RLE8( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);
static bool read_img_BI_RLE4( bmp_state* bmp, im_in* in, im_img* img, ImErr* err);

static im_img* iread_bmp_image(im_in* in, ImErr* err)
{
    bmp_state bmp = {0};
    im_img* img = NULL;

    if (!read_file_header(&bmp, in, err)) {
        goto cleanup;
    }

    if (!read_bitmap_header(&bmp, in, err)) {
        goto cleanup;
    }

    // TODO: V3 has bit masks in colour table for bitcount>=16
    if (!read_colour_table(&bmp, in, err)) {
        goto cleanup;
    }

    // seek to image data
    if (im_in_seek(in, bmp.image_offset, IM_SEEK_SET) != 0) {
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        goto cleanup;
    }

    img = read_image(&bmp, in, err);
    if (img == NULL) {
        goto cleanup;
    }

cleanup:
    if (bmp.linebuf) {
        ifree(bmp.linebuf);
    }
    return img;
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

static bool read_file_header(bmp_state *bmp, im_in* in, ImErr* err) {
    uint8_t buf[BMP_FILE_HEADER_SIZE];
    uint8_t* p;

    // parse the file header
    if (im_in_read(in,buf, BMP_FILE_HEADER_SIZE) != BMP_FILE_HEADER_SIZE) {
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        return false;
    }

    if (!bmp_match_cookie(buf, BMP_FILE_HEADER_SIZE)) {
        *err = IM_ERR_MALFORMED;
        return false;
    }

    p = buf + 2;    // skip bfType ("BM")
    bmp->filesize = (size_t)decode_u32le(&p);    // bfSize
    decode_s16le(&p);       // bfReserved1
    decode_s16le(&p);       // bfReserved2
    bmp->image_offset = decode_u32le(&p);    //bfOffBits

    return true;
}

/*
typedef struct tagBITMAPCOREHEADER {
  DWORD bcSize;
  WORD  bcWidth;
  WORD  bcHeight;
  WORD  bcPlanes;
  WORD  bcBitCount;
} BITMAPCOREHEADER, *PBITMAPCOREHEADER;

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


static bool read_bitmap_header(bmp_state *bmp, im_in* in, ImErr* err)
{
    uint8_t buf[ DIB_MAX_HEADER_SIZE ];

    size_t headersize;
    int w,h,planes,bitcount;
    int compression=0, ncolours=0;
    uint8_t* p;
    uint32_t rmask=0,gmask=0,bmask=0,amask=0;
    size_t imagesize;

    // read the dib header (variable size)
    if(im_in_read(in, buf, 4) != 4 ) {
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        return false;
    }

    p = buf;
    headersize = (size_t)decode_u32le(&p);

    if( headersize < DIB_BITMAPCOREHEADER_SIZE || headersize >DIB_MAX_HEADER_SIZE) {
        *err = IM_ERR_MALFORMED;
        return false;
    }

    // read in rest of header
    if(im_in_read(in, p, headersize-4) != headersize-4 ) {
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        return false;
    }

    //printf("headersize: %d\n", headersize);
    if( headersize < DIB_BITMAPINFOHEADER_SIZE) {
        // treat as BITMAPCOREHEADER
        w = (int)decode_s16le(&p);
        h = (int)decode_s16le(&p);
        planes = (int)decode_s16le(&p);
        bitcount = (int)decode_u16le(&p);
        if (bitcount<=8) {
            ncolours = 1 << bitcount;
        } else {
            ncolours = 0;   // no palette
        }
    } else {
        // it's BITMAPINFOHEADER or higher
        w = (int)decode_s32le(&p);
        h = (int)decode_s32le(&p);
        planes = (int)decode_s16le(&p);
        bitcount = (int)decode_u16le(&p);

        compression = decode_u32le(&p);   // compression method
        imagesize = (size_t)decode_u32le(&p);   // image size
        decode_u32le(&p);   // x pixelspermeter
        decode_u32le(&p);   // y pixelspermeter
        ncolours = decode_u32le(&p);   // num colours in palette
        decode_u32le(&p);   // num of important colours

        //
        if (headersize >= DIB_BITMAPV3INFOHEADER_SIZE ) {
            rmask = decode_u32le(&p);
            gmask = decode_u32le(&p);
            bmask = decode_u32le(&p);
            amask = decode_u32le(&p);
        }
    }

    // sanity checks
    if (planes != 1) {
        *err = IM_ERR_MALFORMED;
        return false;
    }
    if (ncolours > 256) {
        *err = IM_ERR_MALFORMED;
        return false;
    }

    if (compression != BI_RGB &&
        compression != BI_RLE8 &&
        compression != BI_RLE4 &&
        compression != BI_BITFIELDS )
    {
        *err = IM_ERR_UNSUPPORTED;
        return false;
    }

    // TODO:
    // check bitcount in [1,2,4,8,16,24,32]

    bmp->headersize = headersize;

    bmp->w = w;
    if (h>=0) {
        bmp->h = h;
        bmp->topdown = false;
    } else {
        bmp->h = -h;
        bmp->topdown = true;
    }
    bmp->bitcount = bitcount;
    bmp->compression = compression;
    bmp->imagesize = imagesize;
    bmp->ncolours = ncolours;

    bmp->mask[0] = rmask;
    bmp->mask[1] = gmask;
    bmp->mask[2] = bmask;
    bmp->mask[3] = amask;


    if (compression == BI_RGB || compression == BI_BITFIELDS) {
        // alloc buf for reading (enough for a whole line)
        bmp->srclinesize = ((bmp->w*bitcount)+7)/8;
        bmp->srclinesize = ((bmp->srclinesize+3) / 4)*4;    // pad to 32bit
        bmp->linebuf = imalloc(bmp->srclinesize);
        if (!bmp->linebuf) {
            *err = IM_ERR_NOMEM;
            return false;
        }
    } else {
        bmp->linebuf = NULL;
        bmp->srclinesize = 0;
    }

    //printf("%dx%d, %d planes, %d bitcount, %d compression, %d ncolours, %d linesize\n", w,h,planes,bitcount, compression, ncolours, bmp->srclinesize);
    //printf(" mask: 0x%08x 0x%08x 0x%08x 0x%08x\n", rmask, gmask, bmask, amask);
    return true;
}

static bool read_colour_table(bmp_state* bmp, im_in* in, ImErr* err)
{
    int nbytes;

    if( bmp->ncolours==0) {
        return true;
    }

    // BITMAPCOREHEADER files have 3-byte entries
    // later versions have 4-byte entries
    if (bmp->headersize == DIB_BITMAPCOREHEADER_SIZE) {
        nbytes = 3*bmp->ncolours;
    } else {
        nbytes = 4*bmp->ncolours;
    }

    if (im_in_read(in,bmp->rawcolours,nbytes) != nbytes) {
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        return false;
    }

    return true;
}


static im_img* read_image(bmp_state* bmp, im_in* in, ImErr* err)
{
    im_img* img=NULL;
    ImFmt fmt;

    if (bmp->bitcount<16) {
        fmt = IM_FMT_INDEX8;
    } else {
        if ( bmp->mask[3] ) {
            fmt = IM_FMT_RGBA;
        } else {
            fmt = IM_FMT_RGB;
        }
    }

    img = im_img_new(bmp->w, bmp->h, 1, fmt);
    if (!img) {
        *err = IM_ERR_NOMEM;
        goto bailout;
    }

    // parse the palette, if any
    if (bmp->ncolours > 0) {
        if (!cook_colour_table(bmp,img,err)) {
            goto bailout;
        }
    }


    if (bmp->bitcount==4 && bmp->compression==BI_RLE4 ) {
        if (!read_img_BI_RLE4(bmp,in,img,err)) {
            goto bailout;
        }
    } else if (bmp->bitcount<8 && bmp->compression==BI_RGB) {
        if (!read_img_packed_BI_RGB(bmp,in,img,err)) {
            goto bailout;
        }
    } else if (bmp->bitcount==8 && bmp->compression==BI_RGB) {
        if (!read_img_8_BI_RGB(bmp,in,img,err)) {
            goto bailout;
        }
    } else if (bmp->bitcount==8 && bmp->compression==BI_RLE8 ) {
        if (!read_img_BI_RLE8(bmp,in,img,err)) {
            goto bailout;
        }
    } else if (bmp->bitcount==16) {
        if (!read_img_16_BI_BITFIELDS(bmp,in,img,err)) {
            goto bailout;
        }
    } else if (bmp->bitcount==24) {
        if (!read_img_24_BI_RGB(bmp,in,img,err)) {
            goto bailout;
        }
    } else if (bmp->bitcount==32) {
        if (bmp->compression==BI_RGB) {
            if (!read_img_32_BI_RGB(bmp,in,img,err)) {
                goto bailout;
            }
        } else if (bmp->compression==BI_BITFIELDS) {
            if (!read_img_32_BI_BITFIELDS(bmp,in,img,err)) {
                goto bailout;
            }
        } else {
            *err = IM_ERR_UNSUPPORTED;
            goto bailout;
        }
    } else {
        *err = IM_ERR_UNSUPPORTED;
        goto bailout;
    }
    return img;
bailout:
    if (img) {
        im_img_free(img);
    }
    return NULL;
}

// load the colours from the bmp into an im_img palette
static bool cook_colour_table(bmp_state* bmp, im_img* img, ImErr* err)
{
    uint8_t buf[256*4];
    const uint8_t* src;
    uint8_t* dest;
    size_t colsize;
    int i;

    if (bmp->headersize == DIB_BITMAPCOREHEADER_SIZE) {
        colsize = 3;    // bgrbgrbgr...
    } else {
        colsize = 4;    // bgrxbgrxbgrx...
    }

    src = bmp->rawcolours;
    dest = buf;
    for (i=0; i<bmp->ncolours; ++i) {
        *dest++ = src[2];
        *dest++ = src[1];
        *dest++ = src[0];
        src += colsize;
    }
    if (!im_img_pal_set(img, IM_FMT_RGB, bmp->ncolours, buf)) {
        *err = IM_ERR_NOMEM;
        return false;
    }

    return true;
}


// could be handled by read_img_packed_BI_RGB(), but a  special case seems reasonable
// (this'll be quicker because it doesn't have to faff about with bitmasking)
static bool read_img_8_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* src;
    uint8_t* dest;
    int x,y;

    assert(bmp->linebuf);
    for (y=0; y<bmp->h; ++y) {
        if (im_in_read(in,bmp->linebuf, bmp->srclinesize) != bmp->srclinesize) {
            *err = im_in_eof(in) ? IM_ERR_MALFORMED:IM_ERR_FILE;
            return false;
        }
        dest = im_img_row(img, bmp->topdown ? y : (bmp->h-1)-y);
        src = bmp->linebuf;
        for( x=0; x<bmp->w; ++x) {
            *dest++ = *src++;
        }
    }
    return true;
}


// figure out how far right a mask needs to be shifted
static int calc_shift(uint32_t mask)
{
    int i;
    for (i=0; i<32; ++i) {
        if (mask & (1<<i)) {
            return i;
        }
    }
    return 0;
}



// handle 16bit, BI_BITFIELDS
static bool read_img_16_BI_BITFIELDS( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* src;
    uint8_t* dest;
    int x,y;
    int i;
    uint32_t shift[4];
    uint32_t div[4];

    assert(bmp->linebuf);
    // calc shift and divisor
    for (i=0; i<4; ++i) {
        shift[i] = calc_shift(bmp->mask[i]);
        div[i] = bmp->mask[i] >> shift[i];
    }

    for (y=0; y<bmp->h; ++y) {
        if (im_in_read(in,bmp->linebuf,bmp->srclinesize) != bmp->srclinesize) {
            *err = im_in_eof(in) ? IM_ERR_MALFORMED:IM_ERR_FILE;
            return false;
        }
        dest = im_img_row(img, bmp->topdown ? y : (bmp->h-1)-y);
        src=bmp->linebuf;

        if (bmp->mask[3]) {
            // RGBA
            for( x=0; x<bmp->w; ++x) {
                uint32_t packed = (uint32_t)decode_u16le(&src);
                for( i=0; i<4; ++i) {
                    uint32_t v = (packed & bmp->mask[i]) >> shift[i];
                    v = (255*v) / div[i];   // scale to 0..255
                    *dest++ = (uint8_t)v;
                }
            }
        } else {
            // RGB - no alpha
            for( x=0; x<bmp->w; ++x) {
                uint32_t packed = (uint32_t)decode_u16le(&src);
                for( i=0; i<3; ++i) {
                    uint32_t v = (packed & bmp->mask[i]) >> shift[i];
                    v = (255*v) / div[i];   // scale to 0..255
                    *dest++ = (uint8_t)v;
                }
            }
        }
    }

    return true;
}




// BI_RGB only - no fancy bitfield shenanigans needed
static bool read_img_24_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* src;
    uint8_t* dest;
    int x,y;

    for (y=0; y<bmp->h; ++y) {
        if (im_in_read(in,bmp->linebuf,bmp->srclinesize) != bmp->srclinesize) {
            *err = im_in_eof(in) ? IM_ERR_MALFORMED:IM_ERR_FILE;
            return false;
        }
        dest = im_img_row(img, bmp->topdown ? y : (bmp->h-1)-y);
        src=bmp->linebuf;

        // bgrbgrbgr...
        for( x=0; x<bmp->w; ++x) {
            *dest++ = src[2];
            *dest++ = src[1];
            *dest++ = src[0];
            src += 3;
        }
    }
    return true;
}

static bool read_img_32_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    // TODO
    *err = IM_ERR_UNSUPPORTED;
    return false;
}

static bool read_img_32_BI_BITFIELDS( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* src;
    uint8_t* dest;
    int x,y;
    int i;
    uint32_t shift[4];
    uint32_t div[4];

    // calc shift and divisor
    for (i=0; i<4; ++i) {
        shift[i] = calc_shift(bmp->mask[i]);
        div[i] = bmp->mask[i] >> shift[i];
    }

    //
    for (y=0; y<bmp->h; ++y) {
        if (im_in_read(in,bmp->linebuf,bmp->srclinesize) != bmp->srclinesize) {
            *err = im_in_eof(in) ? IM_ERR_MALFORMED:IM_ERR_FILE;
            return false;
        }
        dest = im_img_row(img, bmp->topdown ? y : (bmp->h-1)-y);
        src = bmp->linebuf;

        if (bmp->mask[3]) {
            // RGBA
            for( x=0; x<bmp->w; ++x) {
                uint32_t packed = decode_u32le(&src);
                for( i=0; i<4; ++i) {
                    uint32_t v = (packed & bmp->mask[i]) >> shift[i];
                    v = (255*v) / div[i];   // scale to 0..255
                    *dest++ = (uint8_t)v;
                }
            }
        } else {
            // RGB - no alpha
            for( x=0; x<bmp->w; ++x) {
                uint32_t packed = decode_u32le(&src);
                for( i=0; i<3; ++i) {
                    uint32_t v = (packed & bmp->mask[i]) >> shift[i];
                    v = (255*v) / div[i];   // scale to 0..255
                    *dest++ = (uint8_t)v;
                }
            }
        }
    }
    return true;
}

// handle BI_RGB 1,2,4 bit-packed images
static bool read_img_packed_BI_RGB( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* src;
    uint8_t* dest;
    int x,y;
    uint8_t mask;
    int shift;
    int pixelsperbyte = 8/bmp->bitcount;

    switch( bmp->bitcount) {
        case 1: mask = 0x01; shift = 1; break;
        case 2: mask = 0x03; shift=2; break;
        case 4: mask=0x0f; shift=4; break;
        default:
            *err = IM_ERR_MALFORMED;
            return false;
    }
    assert(bmp->linebuf);
    for (y=0; y<bmp->h; ++y) {
        if (im_in_read(in,bmp->linebuf,bmp->srclinesize) != bmp->srclinesize) {
            *err = im_in_eof(in) ? IM_ERR_MALFORMED:IM_ERR_FILE;
            return false;
        }
        dest = im_img_row(img, bmp->topdown ? y : (bmp->h-1)-y);
        src = bmp->linebuf;
        x=0;
        while( x<bmp->w ) {
            uint8_t packed = *src++;
            int i;
            for (i=pixelsperbyte-1; i>=0 && x<bmp->w; --i) {
                *dest++ = packed>>(i*shift) & mask;
                ++x;
            }
        }
    }
    return true;
}




static bool read_img_BI_RLE8( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* buf;
    uint8_t* src;
    uint8_t* dest;
    uint8_t* end;
    int x,y;
    assert(bmp->imagesize);
    *err = IM_ERR_NONE;

    buf = imalloc(bmp->imagesize);
    if (!buf) {
        *err = IM_ERR_NOMEM;
        return false;
    }

    if (im_in_read(in,buf,bmp->imagesize) != bmp->imagesize) {
        ifree(buf);
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        return false;
    }

    // cheesy hackery - clear the image first. Any skipped deltas or
    // premature EOLs will thus be left as colour 0.
    for (y=0; y<bmp->h; ++y) {
        memset(im_img_row(img,y), 0, bmp->w);
    }


    // now start decoding...
    src = buf;
    end = src + bmp->imagesize;
    x=0;
    y=0;
    dest = im_img_pos(img, x, bmp->topdown ? y : (bmp->h-1)-y);
    while(src < end) {
        uint8_t n;
        if (src+2 > end) {
            goto borked;
        }

        n = *src++;
        if (n==0) {
            n = *src++;
            if (n>2) {
                int pad = n&1;
                // copy next n bytes
                if (src+n+pad > end) {
                    goto borked;
                }
                if(x+n>bmp->w) {
                    goto borked;
                }
                x+=n;
                while( n>0) {
                    *dest++ = *src++;
                    --n;
                }
                // odd counts have a pad byte
                src += pad;
            } else {
                switch (n) {
                    case 0: // end of line
                        x=0;
                        ++y;
                        break;
                    case 1: // end of bitmap
                        // TODO: ensure all src data is consumed?
                        goto success;
                    case 2: // delta
                        x+=(int)(*src++);
                        y+=(int)(*src++);
                        break;
                }
                // update dest ptr
                if(x>=bmp->w || y>=bmp->h) {
                    goto borked;
                }
                dest = im_img_pos(img, x, bmp->topdown ? y : (bmp->h-1)-y);
            }
        } else {
            uint8_t v = *src++;
            // emit v n times
            if (x+n>bmp->w) {
                goto borked;
            }
            x+=n;
            while (n>0) {
                *dest++ = v;
                --n;
            }
        }
    }

success:
    ifree(buf);
    return true;

borked:
    ifree(buf);
    *err = IM_ERR_MALFORMED;
    return false;
}


static bool read_img_BI_RLE4( bmp_state* bmp, im_in* in, im_img* img, ImErr* err)
{
    uint8_t* buf;
    uint8_t* src;
    uint8_t* dest;
    uint8_t* end;
    int x,y;
    assert(bmp->imagesize);
    *err = IM_ERR_NONE;

    buf = imalloc(bmp->imagesize);
    if (!buf) {
        *err = IM_ERR_NOMEM;
        return false;
    }

    if (im_in_read(in,buf,bmp->imagesize) != bmp->imagesize) {
        ifree(buf);
        *err = im_in_eof(in) ? IM_ERR_MALFORMED : IM_ERR_FILE;
        return false;
    }


    // cheesy hackery - clear the image first. Any skipped deltas or
    // premature EOLs will thus be left as colour 0.
    for (y=0; y<bmp->h; ++y) {
        memset(im_img_row(img,y), 0, bmp->w);
    }


    // now start decoding...
    src = buf;
    end = src + bmp->imagesize;
    x=0;
    y=0;
    dest = im_img_pos(img, x, bmp->topdown ? y : (bmp->h-1)-y);
    while(src < end) {
        uint8_t n;
        if (src+2 > end) {
            goto borked;
        }

        n = *src++;
        if (n==0) {
            n = *src++;
            if (n>2) {
                int pad = ((n+1)/2)&1;
                // copy next n pixels
                if (src+(n+1)/2+pad > end) {
                    goto borked;
                }
                if(x+n>bmp->w) {
                    goto borked;
                }
                x+=n;
                while( n>=2) {
                    uint8_t c = *src++;
                    *dest++ = c>>4;
                    *dest++ = c & 0x0f;
                    n-=2;
                }
                while( n>0) {
                    uint8_t c = *src++;
                    *dest++ = c>>4;
                    --n;
                }
                // odd runs have a pad byte
                src += pad;
            } else {
                switch (n) {
                    case 0: // end of line
                        x=0;
                        ++y;
                        break;
                    case 1: // end of bitmap
                        // TODO: ensure all src data is consumed?
                        goto success;
                    case 2: // delta
                        x+=(int)(*src++);
                        y+=(int)(*src++);
                        break;
                }
                // update dest ptr
                if(x>=bmp->w || y>=bmp->h) {
                    goto borked;
                }
                dest = im_img_pos(img, x, bmp->topdown ? y : (bmp->h-1)-y);
            }
        } else {
            uint8_t v = *src++;
            // emit v n times
            if (x+n>bmp->w) {
                goto borked;
            }
            x+=n;
            while (n>=2) {
                *dest++ = v >> 4;
                *dest++ = v & 0x0f;
                n-=2;
            }
            while (n>0) {
                *dest++ = v>>4;
                --n;
            }
        }
    }

success:
    ifree(buf);
    return true;

borked:
    ifree(buf);
    *err = IM_ERR_MALFORMED;
    return false;
}


