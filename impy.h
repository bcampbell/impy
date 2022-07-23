#ifndef IMPY_H
#define IMPY_H

/* Impy
 *
 * A library to load/save images, including animated images, supporting
 * a variety of file formats.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// The pixelformats we support.
// X = pad byte
// A = alpha byte
// R,G,B = colour components (bytes)
typedef enum ImFmt {
    IM_FMT_NONE = 0,
    IM_FMT_INDEX8,  // Bytes indexing a palette.
    IM_FMT_RGB,
    IM_FMT_RGBA,
    IM_FMT_RGBX,
    IM_FMT_ARGB,
    IM_FMT_XRGB,
    IM_FMT_BGR,
    IM_FMT_BGRA,
    IM_FMT_BGRX,
    IM_FMT_ABGR,
    IM_FMT_XBGR,
    IM_FMT_ALPHA,
    IM_FMT_LUMINANCE    // Not really supported yet.
} ImFmt;

// Supported file formats.
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


// TODO: group into user-facing errors and coder errors
typedef enum ImErr {
    IM_ERR_NONE=0,
    IM_ERR_NOMEM,          // a memory allocation failed
    IM_ERR_BADPARAM,
    IM_ERR_COULDNTOPEN,
    IM_ERR_MALFORMED,      // data looks borked
    IM_ERR_UNSUPPORTED,    // file has stuff we don't (yet) support
    IM_ERR_ANIM_UNSUPPORTED,   // format doesn't support animation.
    IM_ERR_NOCONV,         // unsupported pixel conversion (eg rgb->indexed)
    IM_ERR_FILE,           // general IO/file error
    IM_ERR_UNKNOWN_FILE_TYPE,
    IM_ERR_EXTLIB,         // any unspecified error in external lib

    IM_ERR_UNSUPPORTED_FMT,    // format doesn't support img format
    IM_ERR_PALETTE_TOO_BIG,    // format doesn't support palette with that many colours
    // Bad uses of API:
    IM_ERR_BAD_STATE,      //
    IM_ERR_NOT_IN_IMG,     // not in write_image() state
    IM_ERR_TOO_MANY_ROWS,  // read or write too many rows
    IM_ERR_UNFINISHED_IMG, // read or write incomplete image
    IM_ERR_NO_PALETTE,     // expected a write_palette() call, or tried to get a
                           // non-existent palette.
} ImErr;



typedef struct im_in im_in;
typedef struct im_out im_out;
typedef struct im_read im_read;
typedef struct im_write im_write;

/*****************
 * Reading
 *
 * General steps:
 * 1. Create an im_read object (eg using im_read_open_file()).
 * 2. Call im_read_img to get the image details.
 * 3. (optional) call im_read_set_fmt(), im_read_palette() etc...
 * 4. Read the image data out using im_read_rows().
 * 5. If reading an animation, loop back to step 2.
 * 5. Call im_read_finish().
 *
 * It is safe to defer error checking until the final im_read_finish() call.
 * If an error occurs at any point, most calls upon the im_read object are
 * just considered no-ops.
 * You can use im_read_err() to check the error state at any point.
 */

/* im_imginfo is a struct returned by im_read_img() to describe the details
 * of the incoming image.
 */
typedef struct im_imginfo {
    ImFmt fmt;                      // One of IM_FMT_*
    unsigned int w;                 // Width
    unsigned int h;                 // Height
    int x_offset;
    int y_offset;
    unsigned int pal_num_colours;   // Palette size.
} im_imginfo;

/* Create a read object by opening a file.
 * Returns NULL upon failure, and err will be set to report what went wrong.
 */
im_read* im_read_open_file(const char *filename, ImErr *err);

/* Create a read object to read from an im_in stream.
 * Returns NULL upon failure, and err will be set to report what went wrong.
 */
im_read* im_read_new(ImFileFmt file_fmt, im_in *in, ImErr *err);

/* Read in the image details and fills out the given im_imginfo struct.
 * Returns true if an image was obtained, false otherwise.
 * If an error occured, the im_read objects err code will contain it.
 */
bool im_read_img(im_read *reader, im_imginfo *info);

/* Request the image format that should be returned by im_read_rows().
 *
 * The im_imginfo filled out by im_read_img() contains the default format that
 * im_read_rows() will provide. This function lets you set a specific format
 * instead.
 *
 * It will happily reorder RGBA components for you, or add/remove an alpha
 * channel or expand paletted images up to RGB. But it will refuse in some cases.
 * It will not perform quantisation to convert from RGB to a paletted format,
 * for example. In such cases, a IM_ERR_NOCONV error will be set upon the
 * im_read object.
 */
void im_read_set_fmt(im_read* rdr, ImFmt fmt);

/* Read out some (or all) of the image data.
 * It can be called multiple times.
 * `buf` must point to a buffer large enough to contain the resultant rows of
 * image data.
 * `stride` is the number of bytes to go from the beginning of one row to the
 * next. Without padding, most images would have a stride of w*bytesperpixel.
 * A negative `stride` is allowed, so buffers can be filled from the bottom
 * up.
 * The pixel format of the image data will be whatever im_read_img() returned,
 * unless it was successfully overridden by im_read_set_fmt().
 */
void im_read_rows(im_read *reader, unsigned int num_rows, void *buf, int stride);

/* Fetch the palette for the current image. Assumes buf is big enough for
 * info->pal_num_colours in format pal_fmt.
 */
void im_read_palette(im_read *reader, ImFmt pal_fmt, uint8_t *buf);

/* Finish the read operation, clean up and return the final error state. */
ImErr im_read_finish(im_read *reader);

/* Returns the current error state of the im_read object. */
ImErr im_read_err(im_read *reader);


/****************
 * Writing
 *
 * The general sequence is:
 *  1. create an im_write object
 *  2. call im_write_img() to describe the image properties.
 *  3. call optional extra functions, such as im_write_palette().
 *  4. call im_write_rows one or more times to write the image data.
 *  5. If the format supports it, go back to step 2 to write another image.
 *  6. im_write_finish() when done.
 *
 * At any point, you can call im_write_err() to check the error state of the
 * writer. It's safe to call the write functions after an error occurs - they
 * will just act as a no-op.
 * So it's OK to skip error checking just check the final code returned by
 * im_write_finish() to see if the write was successful or not.
 * If an error did occur, there might be partially-written data.
 */

/* Create an im_write object to write to a file.
 * Returns NULL upon error, and returns a specific error code via err.
 */
im_write* im_write_open_file(const char *filename, ImErr *err);

/* Create a writer to write to an sbstracted im_out stream */
im_write* im_write_new(ImFileFmt file_fmt, im_out *out, ImErr *err);

/* Begin writing an image of the given width, height and pixel format. */
void im_write_img(im_write *writer, unsigned int w, unsigned int h, ImFmt fmt);

/* Set the current palette, after a im_write_img() call.
 * For multiple-image writes, the palette stays in effect for subsequent
 * frames. So if you're writing out an animation which uses a single global
 * palette, it's sufficient to just write it out once, for the first frame.
 */
void im_write_palette(im_write *writer, ImFmt pal_fmt, unsigned int num_colours, const uint8_t *colours);

/* Write the image data.
 * Can be called multiple times until the whole image is complete.
 * `data` is expected contain `num_rows` worth of source in the pixel format
 * given in im_write_img().
 * `stride` is the number of bytes to go from the beginning of one row to the
 * next. Without padding, most images would have a stride of w*bytesperpixel.
 * A negative `stride` is allowed, to allow the caller to write rows from the
 * bottom-up.
 */
void im_write_rows(im_write *writer, unsigned int num_rows, const void *data, int stride);

/* Finish the write operation, clean up and return the final error state. */
ImErr im_write_finish(im_write *writer);

/* Returns the current error state of the im_write object. */
ImErr im_write_err(im_write *writer);


/* im_guess_file_format tries to guess the most appropriate file format based
 * on the given filename.
 */
ImFileFmt im_guess_file_format(const char *filename);


/******
 * Pixelformat helpers
 */

// TODO: bit encode details into the IM_FMT_ values, to simplify these
// functions.

/* Return true if fmt is intended to index into a palette. */
static inline bool im_fmt_is_indexed(ImFmt fmt)
    {return fmt == IM_FMT_INDEX8; }

/* Return true if fmt has RGB components (in any order). */
static inline bool im_fmt_has_rgb(ImFmt fmt)
{
    return fmt != IM_FMT_INDEX8 &&
        fmt != IM_FMT_ALPHA &&
        fmt != IM_FMT_LUMINANCE;
}

/* Return true if fmt has an alpha channel. */
static inline bool im_fmt_has_alpha(ImFmt fmt)
{
    return fmt == IM_FMT_RGBA ||
        fmt == IM_FMT_BGRA ||
        fmt == IM_FMT_ARGB ||
        fmt == IM_FMT_ABGR ||
        fmt == IM_FMT_ALPHA;
}

/* Return the number of bytes for each pixel in this format. */
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

/**********
 * IO stuff
 **********/

/* IO abstraction - basically follows stdio.h style */
#define IM_SEEK_SET 0
#define IM_SEEK_CUR 1
#define IM_SEEK_END 2


// abstracted interface for reading
typedef struct im_in {
    size_t (*read)(im_in *, void * , size_t );
    int (*seek)(im_in *, long , int);
    int (*tell)(im_in *);
    int (*eof)(im_in *);
    int (*error)(im_in *);
    int (*close)(im_in *);
} im_in;


// open a file for reading (binary mode)
// the returned reader uses stdio (fopen, fread etc...)
extern im_in *im_in_open_file(const char *filename, ImErr *err);

// TODO: memory-based reader
//extern im_in* im_open_mem_reader( const void* data, size_t nbytes );

// Close and free reader
extern int im_in_close(im_in *in);

// im_in_read reads nbytes from the reader, and returns the actual number read.
// im_in_read does not distiguish between EOF and other error conditions, so
// use im_eof() to check if the end of the data has been hit.
static inline size_t im_in_read(im_in *in, void *buf, size_t nbytes)
    { return in->read(in, buf, nbytes); }

// returns 0 for success, non-zero for error
static inline int im_in_seek(im_in *in, long pos, int whence)
    { return in->seek(in, pos, whence); }
 
// returns current position, -1 for error
static inline int im_in_tell(im_in *in)
    { return in->tell(in); }


// im_eof returns non-zero if the input is out of data
static inline int im_in_eof(im_in *in)
    { return in->eof(in); }

// im_error returns non-zero if the input is in error state (including eof)
static inline int im_in_error(im_in *in)
    { return in->error(in); }

// abstracted interface for writing
typedef struct im_out {
    size_t (*write)(struct im_out *, const void *, size_t);
    int (*close)(struct im_out*);
} im_out;

// open a file for writing
// (backed by fopen/fwrite etc)
im_out* im_out_open_file(const char *filename, ImErr *err);
//im_out* im_open_mem_writer( void* buf, size_t buf_size );

// Close and free writer returns error code...
int im_out_close(im_out * w);

// Write data out. returns number of bytes successfully written
// if <nbytes, an error has occurred
static inline size_t im_out_write(im_out *w, const void *data, size_t nbytes)
    { return w->write(w,data,nbytes); }




#ifdef __cplusplus
}
#endif

#endif // IMPY_H

