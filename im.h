#ifndef IM_H
#define IM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// image formats
typedef enum ImFmt {
    FMT_RGB=0,
    FMT_RGBA,
    FMT_BGR,
    FMT_BGRA,
    FMT_COLOUR_INDEX,
    FMT_ALPHA,
    FMT_LUMINANCE
} ImFmt;


// data types
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


typedef enum ImErr {
    ERR_NONE=0,
    ERR_NOMEM,
    ERR_BADPARAM,
    ERR_COULDNTOPEN,
    ERR_MALFORMED,      // data looks borked
    ERR_UNSUPPORTED,     // file has stuff we don't (yet) support
    ERR_NOCONV,         // unsupported pixel conversion (eg rgb->indexed)
    ERR_FILE,           // stdlib file error, check errno
    ERR_UNKNOWN_FILE_TYPE,
    ERR_EXTLIB,         // any unspecified error in external lib
} ImErr;

typedef enum ImPalFmt {
    PALFMT_RGBA =0,
    PALFMT_RGB
} ImPalFmt;

typedef struct im_pal {
    void* Data;
    ImPalFmt Format;
    int NumColours;
} im_pal;


typedef struct im_img {
    int Width;
    int Height;
    int Depth;

    int XOffset;
    int YOffset;

    ImFmt Format;     // FMT_*
    ImDatatype Datatype;     // DT_*
    int BytesPerPixel;

    int Pitch;  // bytes per line

    void* Data;
    im_pal* Palette;

    // disposal, frame duration, other metadata...
} im_img;


/* IO abstraction - basically follows stdio.h style */
#define IM_SEEK_SET 0
#define IM_SEEK_CUR 1
typedef struct im_reader im_reader;


// abstracted interface for reading
typedef struct im_reader {
    size_t (*read)(im_reader*, void* , size_t );
    int (*seek)(im_reader*, long , int);
    int (*eof)(im_reader*);
    int (*close)(im_reader*);
} im_reader;


/* Bundle-related stuff */
typedef struct im_bundle im_bundle;
typedef struct SlotID {
    int frame,mipmap,layer,face;
} SlotID;

// open a file for reading (binary mode)
// the returned reader uses stdio (fopen, fread etc...)
extern im_reader* im_open_file_reader( const char* filename, ImErr* err);

// TODO: memory-based reader
//extern im_reader* im_open_mem_reader( const void* data, size_t nbytes );


// im_read reads nbytes from the reader, and returns the actual number read.
// im_read does not distiguish between EOF and other error conditions, so
// use im_eof() to check if the end of the data has been hit.
static inline size_t im_read(im_reader* rdr, void* buf, size_t nbytes)
    { return rdr->read(rdr, buf, nbytes); }

//
static inline int im_seek(im_reader* rdr, long pos, int whence)
    { return  rdr->seek(rdr, pos, whence); }

// im_eof returns non-zero if the reader is out of data
static inline int im_eof(im_reader* rdr)
    { return rdr->eof(rdr); }


typedef struct im_writer im_writer;

// abstracted interface for writing
typedef struct im_writer {
    size_t (*write)(im_writer*, const void* , size_t);
    int (*close)(im_writer*);
} im_writer;


// open a file for writing
// (backed by fopen/fwrite etc)
extern im_writer* im_open_file_writer( const char* filename, ImErr* err);
//extern im_writer* im_open_mem_writer( void* buf, size_t buf_size );

// close and free writer returns error code...
extern int im_close_writer(im_writer* w);

// close and free reader
extern int im_close_reader(im_reader* rdr);

static inline size_t im_write( im_writer* w, const void* data, size_t nbytes)
    { return w->write(w,data,nbytes); }



// creates a new image (no palette, even if indexed)
extern im_img* im_img_new( int w, int h, int d, ImFmt fmt, ImDatatype datatype );
extern void im_img_free(im_img *img);
extern void* im_img_row(im_img *img, int row);

extern im_img* im_img_convert( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

extern im_pal* im_pal_new( ImPalFmt fmt, int numColours );
extern void im_pal_free( im_pal* pal );
extern bool im_pal_equal( im_pal* a, im_pal* b );



extern im_img* im_img_load( const char* filename, ImErr* err);
extern im_img* im_img_read( im_reader* rdr, ImErr* err);
extern im_bundle* im_bundle_load( const char* filename, ImErr* err);
extern im_bundle* im_bundle_read( im_reader* rdr, ImErr* err);
extern bool im_bundle_save( im_bundle* bundle, const char* filename, ImErr* err);

// create a new (empty) bundle. NULL if out of memory.
extern im_bundle* im_bundle_new();


// free a bundle and all its images
extern void im_bundle_free(im_bundle* bundle);

// add/replace an image in a bundle.
// bundle takes ownership.
// returns false if out of memory.
extern bool im_bundle_set(im_bundle* bundle, const SlotID id, im_img* img);

// fetch an image from a bundle (NULL if not found)
extern im_img* im_bundle_get(im_bundle* bundle, const SlotID id);
extern SlotID im_bundle_extents(im_bundle* b);


extern int im_bundle_num_frames(im_bundle* b);
extern im_img* im_bundle_get_frame(im_bundle* b, int n);


#endif // IM_H

