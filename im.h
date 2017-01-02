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
} ImErr;

typedef enum ImPalFmt {
    PALFMT_RGBA =0,
    PALFMT_RGB
} ImPalFmt;

typedef struct im_Pal {
    void* Data;
    ImPalFmt Format;
    // TODO: palette format - assume RGB[AX] for now
    int NumColours;
} im_Pal;


typedef struct im_Img {
    int Width;
    int Height;
    int Depth;


    ImFmt Format;     // FMT_*
    ImDatatype Datatype;     // DT_*
    int BytesPerPixel;

    int Pitch;  // bytes per line

    void* Data;
    im_Pal* Palette;

    // offset, disposition, frame duration etc....
} im_Img;

/* IO abstraction - basically follows stdio.h style */
#define IM_SEEK_SET 0
#define IM_SEEK_CUR 1
typedef struct im_reader im_reader;

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


extern im_reader* im_open_file_reader( const char* filename);
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
typedef struct im_writer {
    size_t (*write)(im_writer*, const void* , size_t);
    int (*close)(im_writer*);
} im_writer;

extern im_writer* im_open_file_writer( const char* filename);
//extern im_writer* im_open_mem_writer( void* buf, size_t buf_size );

extern int im_close_writer(im_writer* w);
extern int im_close_reader(im_reader* rdr);

static inline size_t im_write( im_writer* w, const void* data, size_t nbytes)
    { return w->write(w,data,nbytes); }



// creates a new image (no palette, even if indexed)
extern im_Img* im_img_new( int w, int h, int d, ImFmt fmt, ImDatatype datatype );
extern void im_img_free(im_Img *img);
extern void* im_img_row(im_Img *img, int row);

extern im_Img* im_img_convert( const im_Img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

extern im_Pal* im_pal_new( ImPalFmt fmt, int numColours );
extern void im_pal_free( im_Pal* pal );

extern void im_err(ImErr err);



extern im_Img* im_img_load( const char* filename);
extern im_Img* im_img_read( im_reader* rdr);

extern bool isPng(const uint8_t* buf, int nbytes);
extern im_Img* readPng(im_reader* rdr);
extern im_Img* loadPng(const char* fileName);

extern bool isGif(const uint8_t* buf, int nbytes);
extern bool writePng(im_writer* out, im_Img* img);

extern im_Img* readGif(im_reader* rdr);
extern im_bundle* multiReadGif( im_reader* rdr );


im_bundle* im_bundle_new();
void im_bundle_free(im_bundle* bundle);
bool im_bundle_set(im_bundle* bundle, const SlotID id, im_Img* img);
im_Img* im_bundle_get(im_bundle* bundle, const SlotID id);

// PRIVATE
extern void* imalloc( size_t size);
extern void ifree(void* ptr);

#endif // IM_H

