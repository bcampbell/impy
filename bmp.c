#include "im.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static bool is_bmp(const uint8_t* buf, int nbytes);
static bool match_bmp_ext(const char* file_ext);
static im_img* read_bmp_image( im_reader* rdr, ImErr* err );
//static bool write_bmp_image(im_img* img, im_writer* out, ImErr* err);

struct handler handle_bmp = {is_bmp, read_bmp_image, NULL, match_bmp_ext, NULL, NULL};



#define BMP_FILE_HEADER_SIZE 14

#define DIB_BITMAPCOREHEADER_SIZE 12
#define DIB_BITMAPINFOHEADER_SIZE 40
#define DIB_BITMAPV2INFOHEADER_SIZE 52
#define DIB_BITMAPV3INFOHEADER_SIZE 56
#define DIB_BITMAPV4HEADER_SIZE 104
#define DIB_BITMAPV5HEADER 124
#define DIB_MAX_HEADER_SIZE DIB_BITMAPV5HEADER

// compression types (we'll only support the first few)
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3
#define BI_JPEG 4
#define BI PNG 5
#define BI_ALPHABITFIELDS 6
#define BI_CMYK 11
#define BI_CMYKRLE8 12
#define BI_CMYKRLE4 13



typedef struct bmp_state {
    uint8_t fileheader[BMP_FILE_HEADER_SIZE];

    // values from BITMAPFILEHEADER
    size_t filesize;
    size_t image_offset;

    // values parsed (or inferred) from bitmap header
    size_t headersize;
    int w;
    int h;
    int bitcount;
    int compression;
    int ncolours;
    uint32_t mask[4];   // r,g,b,a

    //
    uint8_t rawcolours[256*4];
} bmp_state;

static bool is_bmp(const uint8_t* buf, int nbytes)
{
    return buf[0]=='B' && buf[1] == 'M';
}

static bool match_bmp_ext(const char* file_ext)
{
    return (istricmp(file_ext,".bmp")==0);
}


static inline uint32_t decode_u32le(uint8_t** cursor)
{
    const uint8_t* p = *cursor;
    *cursor += 4;
    return (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];
}

static inline uint16_t decode_u16le(uint8_t** cursor) {
    const uint8_t* p = *cursor;
    *cursor += 2;
    return (p[1]<<8) | p[0];
}

static inline int32_t decode_s32le(uint8_t** cursor)
    { return (int32_t)decode_u32le(cursor); }

static inline int16_t decode_s16le(uint8_t** cursor)
    { return (int16_t)decode_u16le(cursor); } 

static bool read_file_header(bmp_state *bmp, im_reader* rdr, ImErr* err);
static bool read_bitmap_header(bmp_state *bmp, im_reader* rdr, ImErr* err);
static bool read_palette(bmp_state* bmp, im_reader* rdr, ImErr* err);
static im_img* read_image(bmp_state* bmp, im_reader* rdr, ImErr* err);

static im_img* read_bmp_image( im_reader* rdr, ImErr* err )
{
    bmp_state bmp;
    uint8_t* p;
    im_img* img;

    if (!read_file_header(&bmp, rdr, err)) {
        return NULL;
    }

    if (!read_bitmap_header(&bmp, rdr, err)) {
        return NULL;
    }

    if (!read_palette(&bmp, rdr, err)) {
        return NULL;
    }

    // seek to image data
    if (im_seek(rdr, bmp.image_offset, IM_SEEK_SET) != 0) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return NULL;
    }

    img = read_image(&bmp, rdr, err);
    if (img == NULL) {
        return NULL;
    }



    *err = ERR_UNSUPPORTED;
    return NULL;
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

static bool read_file_header(bmp_state *bmp, im_reader* rdr, ImErr* err) {
    uint8_t buf[BMP_FILE_HEADER_SIZE];
    uint8_t* p;

    // parse the file header
    if (im_read(rdr,buf, BMP_FILE_HEADER_SIZE) != BMP_FILE_HEADER_SIZE) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }

    if (!is_bmp(buf, BMP_FILE_HEADER_SIZE)) {
        *err = ERR_MALFORMED;
        return false;
    }

    p=buf+2;    // skip bfType ("BM")
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


static bool read_bitmap_header(bmp_state *bmp, im_reader* rdr, ImErr* err)
{
    uint8_t buf[ DIB_MAX_HEADER_SIZE ];

    size_t headersize;
    int w,h,planes,bitcount;
    int compression=0, ncolours=0;
    uint8_t* p;
    uint32_t rmask=0,gmask=0,bmask=0,amask=0;

    // read the dib header (variable size)
    if(im_read(rdr, buf, 4) != 4 ) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }

    p = buf;
    headersize = (size_t)decode_u32le(&p);

    if( headersize < DIB_BITMAPCOREHEADER_SIZE || headersize >DIB_MAX_HEADER_SIZE) {
        *err = ERR_MALFORMED;
        return false;
    }

    // read in rest of header
    if(im_read(rdr, p, headersize-4) != headersize-4 ) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }

    printf("headersize: %d\n", headersize);
    if( headersize < DIB_BITMAPINFOHEADER_SIZE) {
        // treat as BITMAPCOREHEADER
        w = (int)decode_s16le(&p);
        h = (int)decode_s16le(&p);
        planes = (int)decode_s16le(&p);
        bitcount = (int)decode_u16le(&p);

        ncolours = 1 << bitcount;
    } else {
        // it's BITMAPINFOHEADER or higher
        w = (int)decode_s32le(&p);
        h = (int)decode_s32le(&p);
        planes = (int)decode_s16le(&p);
        bitcount = (int)decode_u16le(&p);

        compression = decode_u32le(&p);   // compression method
        decode_u32le(&p);   // image size
        decode_u32le(&p);   // x pixelspermeter
        decode_u32le(&p);   // y pixelspermeter
        ncolours = decode_u32le(&p);   // num colours in palette
        decode_u32le(&p);   // num of important colours

        printf("%dx%d, %d planes, %d bitcount %d compression, %d ncolours\n", w,h,planes,bitcount, compression, ncolours);
        //
        if (headersize >= DIB_BITMAPV3INFOHEADER_SIZE ) {
            rmask = decode_u32le(&p);
            gmask = decode_u32le(&p);
            bmask = decode_u32le(&p);
            amask = decode_u32le(&p);
            printf(" mask: 0x%08x 0x%08x 0x%08x 0x%08x\n", rmask, gmask, bmask, amask);
        }
    }

    // sanity checks
    if (planes != 1) {
        *err = ERR_MALFORMED;
        return false;
    }
    if (ncolours > 256) {
        *err = ERR_MALFORMED;
        return false;
    }

    if (compression != BI_RGB &&
        compression != BI_RLE8 &&
        compression != BI_RLE4 &&
        compression != BI_BITFIELDS )
    {
        *err = ERR_UNSUPPORTED;
        return false;
    }

    // TODO:
    // check bitcount in [1,2,4,8,16,24,32]

    bmp->headersize = headersize;

    bmp->w = w;
    bmp->h = h;
    bmp->bitcount = bitcount;
    bmp->compression = compression;
    bmp->ncolours = ncolours;

    bmp->mask[0] = rmask;
    bmp->mask[1] = gmask;
    bmp->mask[2] = bmask;
    bmp->mask[3] = amask;

    return true;
}

static bool read_palette(bmp_state* bmp, im_reader* rdr, ImErr* err)
{
    uint8_t buf[256*4];
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

    if (im_read(rdr,bmp->rawcolours,nbytes) != nbytes) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }

    return true;
}


static im_img* read_image(bmp_state* bmp, im_reader* rdr, ImErr* err)
{
    *err = ERR_UNSUPPORTED;
    return NULL;
}

