#ifndef IMPY_H
#define IMPY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


typedef enum ImFmt {
    IM_FMT_NONE = 0,
    IM_FMT_INDEX8,
    IM_FMT_RGBA,
    IM_FMT_RGBX,
    IM_FMT_RGB,
    IM_FMT_ARGB,
    IM_FMT_XRGB,
    IM_FMT_BGRA,
    IM_FMT_BGRX,
    IM_FMT_BGR,
    IM_FMT_ABGR,
    IM_FMT_XBGR,
    FMT_ALPHA,
    FMT_LUMINANCE
} ImFmt;

// phasing out:
#define FMT_RGB  IM_FMT_RGB
#define FMT_RGBA IM_FMT_RGBA
#define FMT_BGR IM_FMT_BGR
#define FMT_BGRA IM_FMT_BGRA
#define FMT_COLOUR_INDEX IM_FMT_INDEX8
//    FMT_ALPHA,
//    FMT_LUMINANCE

#define PALFMT_RGBA IM_FMT_RGBA
#define PALFMT_RGB IM_FMT_RGB

// TODO: bit encoding for pixelformats. Want to encode:
// type: 0=rgb, 1=indexed, others... (2 bits)
// alpha: 1 bit
// rgb order: 0=rgb 1=bgr (1 bit)
// bytesperpixel 0-16 (5 bits)
// etc

// Return true if fmt is intended to index into a palette.
static inline bool im_fmt_is_indexed(ImFmt fmt)
    {return fmt == IM_FMT_INDEX8; }

// Return true if fmt has RGB components (in any order).
static inline bool im_fmt_has_rgb(ImFmt fmt)
{
    return fmt != IM_FMT_INDEX8 &&
        fmt != FMT_ALPHA &&
        fmt != FMT_LUMINANCE;
}

// Return true if fmt has an alpha channel.
static inline bool im_fmt_has_alpha(ImFmt fmt)
{
    return fmt == IM_FMT_RGBA ||
        fmt == IM_FMT_BGRA ||
        fmt == IM_FMT_ARGB ||
        fmt == IM_FMT_ABGR ||
        fmt == FMT_ALPHA;
}

static inline size_t im_fmt_bytesperpixel(ImFmt fmt)
{
    size_t s = 0;
    if (im_fmt_is_indexed(fmt)) {
        s += 1;
    } else if (im_fmt_has_rgb(fmt)) {
        s += 3;
    }
    if (im_fmt_has_alpha(fmt)) {
        s += 1;
    }
    return s;
}

typedef enum ImFileFmt {
    IM_FILEFMT_UNKNOWN=0,
    IM_FILEFMT_PNG,
    IM_FILEFMT_GIF,
    IM_FILEFMT_BMP,
    IM_FILEFMT_ILBM,
    IM_FILEFMT_JPEG,
    IM_FILEFMT_TARGA,
    IM_FILEFMT_PCX
} ImFileFmt;

// data types
// NOTE: U8 is the only real supported format right now
typedef enum ImDatatype {
    DT_U8=0,
    DT_S8,
    DT_U16,
    DT_S16,
    DT_U32,
    DT_S32,
    DT_FLOAT16,
    DT_FLOAT32,
    DT_FLOAT64
} ImDatatype;


// TODO: group into user-facing errors and coder errors
typedef enum ImErr {
    ERR_NONE=0,
    ERR_NOMEM,          // a memory allocation failed
    ERR_BADPARAM,
    ERR_COULDNTOPEN,
    ERR_MALFORMED,      // data looks borked
    ERR_UNSUPPORTED,    // file has stuff we don't (yet) support
    ERR_ANIM_UNSUPPORTED,   // format doesn't support animation.
    ERR_NOCONV,         // unsupported pixel conversion (eg rgb->indexed)
    ERR_FILE,           // general IO/file error
    ERR_UNKNOWN_FILE_TYPE,
    ERR_EXTLIB,         // any unspecified error in external lib

    ERR_UNSUPPORTED_FMT,    // format doesn't support img format
    ERR_PALETTE_TOO_BIG,    // format doesn't support palette with that many colours
    // Bad uses of API:
    ERR_BAD_STATE,      //
    ERR_NOT_IN_IMG,     // not in begin_image() state
    ERR_TOO_MANY_ROWS,  // read or write too many rows
    ERR_UNFINISHED_IMG, // read or write incomplete image
    ERR_NO_PALETTE,     // expected a set_palette() call, or tried to get a
                        // non-existent palette.
} ImErr;


/**********
 * IO stuff
 **********/

/* IO abstraction - basically follows stdio.h style */
// TODO: make these an enum
#define IM_SEEK_SET 0
#define IM_SEEK_CUR 1
#define IM_SEEK_END 2

typedef struct im_in im_in;

// abstracted interface for reading
struct im_in {
    size_t (*read)(im_in*, void* , size_t );
    int (*seek)(im_in*, long , int);
    int (*tell)(im_in*);
    int (*eof)(im_in*);
    int (*error)(im_in*);
    int (*close)(im_in*);
};


// open a file for reading (binary mode)
// the returned reader uses stdio (fopen, fread etc...)
extern im_in* im_in_open_file(const char* filename, ImErr* err);

// TODO: memory-based reader
//extern im_in* im_open_mem_reader( const void* data, size_t nbytes );

// Close and free reader
extern int im_in_close(im_in* in);

// im_read reads nbytes from the reader, and returns the actual number read.
// im_read does not distiguish between EOF and other error conditions, so
// use im_eof() to check if the end of the data has been hit.
static inline size_t im_read(im_in* in, void* buf, size_t nbytes)
    { return in->read(in, buf, nbytes); }

// returns 0 for success, non-zero for error
static inline int im_seek(im_in* in, long pos, int whence)
    { return in->seek(in, pos, whence); }
 
// returns current position, -1 for error
static inline int im_tell(im_in* in)
    { return in->tell(in); }


// im_eof returns non-zero if the reader is out of data
static inline int im_eof(im_in* in)
    { return in->eof(in); }

// im_error returns non-zero if the reader is in error state (including eof)
static inline int im_error(im_in* in)
    { return in->error(in); }

// abstracted interface for writing
typedef struct im_out {
    size_t (*write)(struct im_out*, const void* , size_t);
    int (*close)(struct im_out*);
} im_out;

// open a file for writing
// (backed by fopen/fwrite etc)
extern im_out* im_out_open_file( const char* filename, ImErr* err);
//extern im_out* im_open_mem_writer( void* buf, size_t buf_size );

// Close and free writer returns error code...
extern int im_out_close(im_out* w);


// Write data out. returns number of bytes successfully written
// if <nbytes, an error has occurred
static inline size_t im_write( im_out* w, const void* data, size_t nbytes)
    { return w->write(w,data,nbytes); }



/****************
 * filetype stuff
 */

ImFileFmt im_guess_file_format(const char* filename);

/****************
 * API for writing
 */

typedef struct im_writer im_writer;

im_writer* im_writer_open_file(const char *filename, ImErr* err);
// ownership of out is passed to writer.
im_writer* im_writer_new(ImFileFmt file_fmt, im_out* out, ImErr* err);

void im_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt);
void im_set_palette(im_writer* writer, ImFmt pal_fmt, unsigned int num_colours, const uint8_t *colours);
void im_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data);

ImErr im_writer_finish(im_writer* writer);
ImErr im_writer_err(im_writer* writer);


/*****************
 * API for reading
 */

typedef struct im_imginfo {
    ImFmt fmt;
    unsigned int w;
    unsigned int h;
    int x_offset;
    int y_offset;
    unsigned int pal_num_colours;
} im_imginfo;

typedef struct im_reader im_reader;

im_reader* im_new_gif_reader(im_in *in, ImErr *err);

im_reader* im_reader_new(ImFileFmt file_fmt, im_in *in, ImErr *err);
im_reader* im_reader_open_file(const char *filename, ImErr *err);

bool im_get_img(im_reader *reader, im_imginfo *info);
ImErr im_reader_err(im_reader *reader);
void im_read_rows(im_reader *reader, unsigned int num_rows, uint8_t *buf);

// Fetch the current palette. Assumes buf is big enough for info->pal_num_colours
// in format pal_fmt.
void im_read_palette(im_reader *reader, ImFmt pal_fmt, uint8_t *buf);

ImErr im_reader_finish(im_reader *reader);

#ifdef __cplusplus
}
#endif

#endif // IMPY_H

