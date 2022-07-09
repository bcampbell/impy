#ifndef PRIVATE_H
#define PRIVATE_H

// private stuff, internal to library
#include "impy.h"
#include "img.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


typedef struct write_handler {
    ImFileFmt file_fmt;
    void (*begin_img)(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt);
    void (*write_rows)(im_writer* writer, unsigned int num_rows, const uint8_t *data);
    void (*set_palette)(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours);
    ImErr (*finish)(im_writer* writer);
} write_handler;

// The common fields shared by all writers.
typedef struct im_writer {
    write_handler* handler;
    ImErr err;
    im_out* out;
} im_writer;


typedef struct read_handler {
    bool (*get_img)(im_reader* reader, im_imginfo* info);
    void (*read_rows)(im_reader* reader, unsigned int num_rows, uint8_t* buf);
    void (*read_palette)(im_reader* reader, uint8_t* buf);
    ImErr (*finish)(im_reader* reader);
} read_handler;


// The common fields shared by all readers.
typedef struct im_reader {
    read_handler* handler;
    ImErr err;
    im_in* in;
} im_reader;


// from util.c
extern int istricmp(const char* a, const char* b);
extern bool is_path_sep(char c);
extern const char* ext_part( const char* path);

// from generic_read.c
extern im_reader* im_new_generic_reader(im_img* (*load_single)(im_in *, ImErr *), im_in* in, ImErr* err );

// from im.c
extern void* imalloc( size_t size);
extern void ifree(void* ptr);

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


static inline void encode_u32le(uint8_t** cursor, uint32_t val)
{
    uint8_t* p = *cursor;
    *cursor += 4;
    p[0] = val & 0xff;
    p[1] = (val >> 8) & 0xff;
    p[2] = (val >> 16) & 0xff;
    p[3] = (val >> 24) & 0xff;
}

static inline void encode_u16le(uint8_t** cursor, uint16_t val)
{
    uint8_t* p = *cursor;
    *cursor += 2;
    p[0] = val & 0xff;
    p[1] = (val >> 8) & 0xff;
}

static inline void encode_s32le(uint8_t** cursor, int32_t val)
    { encode_u32le(cursor, (int32_t)val); }

static inline void encode_s16le(uint8_t** cursor, int16_t val)
    { encode_u16le(cursor, (int16_t)val); }





/* format conversion helpers (convert.c) */

// signature for a fn to convert w pixels
typedef void (*im_convert_fn)( const uint8_t* src, uint8_t* dest, int w);

// pick a conversion fn
extern im_convert_fn pick_convert_fn( ImFmt srcFmt, ImDatatype srcDT, ImFmt destFmt, ImDatatype destDT );


im_img* iread_png_image(im_in* rdr, ImErr *err);
im_img* iread_bmp_image(im_in* rdr, ImErr *err);
im_img* iread_jpeg_image(im_in* rdr, ImErr *err);
im_img* iread_pcx_image(im_in* rdr, ImErr *err);
im_img* iread_targa_image(im_in* rdr, ImErr *err);

#endif


