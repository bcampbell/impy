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

typedef struct bmp_state {
    uint8_t fileheader[BMP_FILE_HEADER_SIZE];
    size_t dibheader_size;
    uint8_t dibheader[DIB_MAX_HEADER_SIZE];
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

static bool read_header(bmp_state *bmp, im_reader* rdr, ImErr* err);

static im_img* read_bmp_image( im_reader* rdr, ImErr* err )
{
    bmp_state bmp;
    uint8_t* p;

    // parse the file header
    if (im_read(rdr,bmp.fileheader, BMP_FILE_HEADER_SIZE) != BMP_FILE_HEADER_SIZE) {
        if (im_eof(rdr)) {
            *err = ERR_MALFORMED;
        } else {
            *err = ERR_FILE;
        }
        return NULL;
    }
    if (!is_bmp(bmp.fileheader, BMP_FILE_HEADER_SIZE)) {
        *err = ERR_MALFORMED;
    }

    if (!read_header(&bmp, rdr, err)) {
        return NULL;
    }
}


static bool read_header(bmp_state *bmp, im_reader* rdr, ImErr* err) {
    size_t headersize;
    int w,h,planes,bitsperpixel;
    int compression=-1, ncolours=0;
    uint8_t* p;

    // read the dib header (variable size)
    if(im_read(rdr, bmp->dibheader, 4) != 4 ) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return NULL;
    }

    p = bmp->dibheader;
    headersize = (size_t)decode_u32le(&p);

    if( headersize < DIB_BITMAPCOREHEADER_SIZE || headersize >DIB_MAX_HEADER_SIZE) {
        *err = ERR_MALFORMED;
        return false;
    }

    printf("dibheadersize: %d\n", headersize);

    // read in rest of header
    if(im_read(rdr, bmp->dibheader+4, headersize-4) != headersize-4 ) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }

    if( headersize < DIB_BITMAPINFOHEADER_SIZE) {
        // treat as BITMAPCOREHEADER
        w = (int)decode_u16le(&p);
        h = (int)decode_u16le(&p);
        planes = (int)decode_u16le(&p);
        bitsperpixel = (int)decode_u16le(&p);
    } else {
        // it's BITMAPINFOHEADER or higher
        w = (int)decode_u32le(&p);
        h = (int)decode_u32le(&p);
        planes = (int)decode_u16le(&p);
        bitsperpixel = (int)decode_u16le(&p);

        compression = decode_u32le(&p);   // compression method
        decode_u32le(&p);   // image size
        decode_u32le(&p);   // x pixelspermeter
        decode_u32le(&p);   // y pixelspermeter
        ncolours = decode_u32le(&p);   // num colours in palette
        decode_u32le(&p);   // num of important colours
    }

    printf("%dx%d, %d planes, %d bitsperpixel %d compression, %d ncolours\n", w,h,planes,bitsperpixel, compression, ncolours);

    *err = ERR_MALFORMED;
    return false;
}






