#ifndef PRIVATE_H
#define PRIVATE_H

// private stuff, internal to library
#include "impy.h"
#include "img.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/***********
 * format conversion helpers (convert.c)
 */

// signature for a fn to convert w pixels
typedef void (*im_convert_fn)( const uint8_t* src, uint8_t* dest, unsigned int w, unsigned int nrgba, const uint8_t* rgba);

// pick a conversion fn
extern im_convert_fn i_pick_convert_fn(ImFmt srcFmt, ImFmt destFmt);


/***********
 * write API support
 */

typedef struct write_handler {
    ImFileFmt file_fmt;
    void (*pre_img)(im_writer* writer);
    void (*emit_header)(im_writer* writer);
    void (*emit_rows)(im_writer *writer, unsigned int num_rows, const void *data, int stride);
    void (*post_img)(im_writer* writer);
    void (*finish)(im_writer* writer);
} write_handler;

// The common fields shared by all writers.
typedef struct im_writer {
    write_handler* handler;
    ImErr err;
    im_out* out;
    bool out_owned; // close and free `out` when done?

    // Overall writer state
    enum {WRITESTATE_READY, WRITESTATE_HEADER, WRITESTATE_BODY} state;
    unsigned int num_frames;

    // Set by im_begin_img()
    int x_offset;
    int y_offset;
    unsigned int w;
    unsigned int h;
    ImFmt fmt;

    // Number of rows from the current frame which have been written out
    // by im_write_rows().
    unsigned int rows_written;

    // If we need to pixel-convert internally...
    ImFmt internal_fmt;
    // rowbuf and cvt_fn used if internal fmt different from fmt
    uint8_t* rowbuf;
    im_convert_fn row_cvt_fn;

    // Palette - set by im_set_palette(), persists between frames.
    // Always stored here in IM_FMT_RGBA format.
    unsigned int pal_num_colours;
    uint8_t* pal_data;
} im_writer;

// im_write.c
void i_writer_init(im_writer* writer);
void i_writer_set_internal_fmt(im_writer* writer, ImFmt internal_fmt);

/**********
 * read API support
 */

typedef struct read_handler {
    bool (*get_img)(im_reader* rdr);
    void (*read_rows)(im_reader *rdr, unsigned int num_rows, void *buf, int stride);
    void (*finish)(im_reader* rdr);
} read_handler;


// The common fields shared by all readers.
typedef struct im_reader {
    read_handler* handler;
    ImErr err;
    im_in* in;
    bool in_owned; // close and free `in` when done?

    enum {READSTATE_READY, READSTATE_HEADER, READSTATE_BODY} state;
    int frame_num;

    im_imginfo curr;
    unsigned int rows_read;

    // Internal palette fmt is IM_FMT_RGBA.
    // Palette size in curr->pal_num_colours.
    uint8_t* pal_data;

    // If user requests a different pixelformat im_read_rows() will convert
    // on-the-fly.
    ImFmt external_fmt;
    uint8_t* rowbuf;
    im_convert_fn row_cvt_fn;
} im_reader;

// From im_read.c
void i_reader_init(im_reader* rdr);


// From generic_read.c
extern im_reader* im_new_generic_reader(im_img* (*load_single)(im_in *, ImErr *), im_in* in, ImErr* err );

// From various readers.
im_img* iread_png_image(im_in* rdr, ImErr *err);
im_img* iread_bmp_image(im_in* rdr, ImErr *err);
im_img* iread_jpeg_image(im_in* rdr, ImErr *err);
im_img* iread_pcx_image(im_in* rdr, ImErr *err);
im_img* iread_targa_image(im_in* rdr, ImErr *err);


// From im.c
extern void* imalloc(size_t size);
extern void* irealloc(void *ptr, size_t size);
extern void ifree(void* ptr);

// From util.c
extern int istricmp(const char* a, const char* b);
extern bool is_path_sep(char c);
extern const char* ext_part( const char* path);

// Binary decode helpers
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





#endif


