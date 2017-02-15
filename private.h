#ifndef PRIVATE_H
#define PRIVATE_H

// private stuff, internal to library
#include "im.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#if 0
typedef struct im_img im_img;
typedef struct im_reader im_reader;
typedef struct im_writer im_writer;
typedef struct im_bundle im_bundle;
#endif

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
#endif


