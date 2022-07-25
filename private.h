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
    ImFiletype file_fmt;
    void (*pre_img)(im_write* writer);
    void (*emit_header)(im_write* writer);
    void (*emit_rows)(im_write *writer, unsigned int num_rows, const void *data, int stride);
    void (*post_img)(im_write* writer);
    void (*finish)(im_write* writer);
} write_handler;

// The common fields shared by all writers.
typedef struct im_write {
    write_handler* handler;
    ImErr err;
    im_out* out;
    bool out_owned; // close and free `out` when done?

    // Overall writer state
    enum {WRITESTATE_READY, WRITESTATE_HEADER, WRITESTATE_BODY} state;
    unsigned int num_frames;

    // Set by im_write_img()
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

    // Palette - set by im_write_palette(), persists between frames.
    // Always stored here in IM_FMT_RGBA format.
    unsigned int pal_num_colours;
    uint8_t* pal_data;
} im_write;

// im_write.c
void i_write_init(im_write* writer);
void i_write_set_internal_fmt(im_write* writer, ImFmt internal_fmt);

/**********
 * read API support
 */

typedef struct i_read_handler {
    ImFiletype file_format;
    bool (*match_cookie)(const uint8_t* buf, int nbytes);
    im_read* (*create)(im_in *in, ImErr *err);
    bool (*get_img)(im_read* rdr);
    void (*read_rows)(im_read *rdr, unsigned int num_rows, void *buf, int stride);
    void (*finish)(im_read* rdr);
} i_read_handler;


// The common fields shared by all readers.
typedef struct im_read {
    i_read_handler* handler;
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
} im_read;

// From im_read.c
void i_read_init(im_read* rdr);

// From gif_read.c
//extern im_read* i_new_gif_reader(im_in * in, ImErr *err);

// From generic_read.c
im_read* i_new_generic_reader(im_img* (*load_single)(im_in *, ImErr *), i_read_handler* handler, im_in* in, ImErr* err );
bool i_generic_read_img(im_read* rdr);
void i_generic_read_rows(im_read *rdr, unsigned int num_rows, void* buf, int stride);
void i_generic_read_finish(im_read* rdr);

// Read handers (from various files).
extern i_read_handler i_gif_read_handler;
extern i_read_handler i_png_read_handler;
extern i_read_handler i_bmp_read_handler;
extern i_read_handler i_jpeg_read_handler;
extern i_read_handler i_pcx_read_handler;
extern i_read_handler i_targa_read_handler;

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


