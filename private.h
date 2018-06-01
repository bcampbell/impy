#ifndef PRIVATE_H
#define PRIVATE_H

// private stuff, internal to library
#include "impy.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct handler {
    bool (*match_cookie)(const uint8_t* buf, int nbytes);
    im_img* (*read_img)( im_reader* rdr, ImErr *err);
    im_bundle* (*read_bundle)( im_reader* rdr, ImErr *err);

    bool (*match_ext)(const char* file_extension);
    bool (*write_img)(im_img* img, im_writer* out, ImErr *err);
    bool (*write_bundle)(im_bundle* bundle, im_writer* out, ImErr *err);

    // TODO: add a suitable() fn to check formats, palettes, anim etc...
};

// from util.c
extern int istricmp(const char* a, const char* b);
extern bool is_path_sep(char c);
extern const char* ext_part( const char* path);


// from im.c
extern void* imalloc( size_t size);
extern void ifree(void* ptr);

// file format handlers

extern struct handler handle_png;
extern struct handler handle_gif;
extern struct handler handle_iff;
extern struct handler handle_bmp;
extern struct handler handle_pcx;
extern struct handler handle_jpeg;


// binary decode helpers
static inline uint32_t decode_u32le(uint8_t** cursor)
{
    uint8_t* p = *cursor;
    *cursor += 4;
    return (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];
}

static inline uint16_t decode_u16le(uint8_t** cursor) {
    uint8_t* p = *cursor;
    *cursor += 2;
    return (p[1]<<8) | p[0];
}

static inline int32_t decode_s32le(uint8_t** cursor)
    { return (int32_t)decode_u32le(cursor); }

static inline int16_t decode_s16le(uint8_t** cursor)
    { return (int16_t)decode_u16le(cursor); } 

#endif


