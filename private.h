#ifndef PRIVATE_H
#define PRIVATE_H

// private stuff, internal to library

#include <stdint.h>
#include <stdbool.h>

typedef struct im_Img im_Img;
typedef struct im_reader im_reader;
typedef struct im_writer im_writer;
typedef struct im_bundle im_bundle;

struct handler {
    bool (*match_cookie)(const uint8_t* buf, int nbytes);
    im_Img* (*read_img)( im_reader* rdr);
    im_bundle* (*read_bundle)( im_reader* rdr);

    bool (*match_ext)(const char* file_extension);
    bool (*write_img)(im_Img* img, im_writer* out);
    bool (*write_bundle)(im_bundle* bundle, im_writer* out);

    // TODO: add a suitable() fn to check formats, palettes, anim etc...
};

// from util.c
extern int istricmp(const char* a, const char* b);
extern bool is_path_sep(char c);
extern const char* ext_part( const char* path);



// file format handlers

extern struct handler handle_png;
extern struct handler handle_gif;
#endif


