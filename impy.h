#ifndef IMPY_H
#define IMPY_H

#ifdef __cplusplus
extern "C" {
#endif

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


typedef enum ImErr {
    ERR_NONE=0,
    ERR_NOMEM,          // a memory allocation failed
    ERR_BADPARAM,
    ERR_COULDNTOPEN,
    ERR_MALFORMED,      // data looks borked
    ERR_UNSUPPORTED,    // file has stuff we don't (yet) support
    ERR_NOCONV,         // unsupported pixel conversion (eg rgb->indexed)
    ERR_FILE,           // general IO/file error
    ERR_UNKNOWN_FILE_TYPE,
    ERR_EXTLIB,         // any unspecified error in external lib
} ImErr;

// palette format
typedef enum ImPalFmt {
    PALFMT_RGBA =0,
    PALFMT_RGB
} ImPalFmt;


typedef struct im_img {
    int w;
    int h;
    int d;

    ImFmt format;     // FMT_*
    ImDatatype datatype;     // DT_*

    int pitch;  // bytes per line
    void* pixel_data;

    // palette
    int pal_num_colours;    // 0=no palette
    ImPalFmt pal_fmt;
    void* pal_data;     // can be NULL, iff num_colours==0

    // metadata
    int x_offset;
    int y_offset;
    // TODO: disposal, frame duration, etc...
} im_img;


/**********
 * IO stuff
 **********/

/* IO abstraction - basically follows stdio.h style */
// TODO: make these an enum
#define IM_SEEK_SET 0
#define IM_SEEK_CUR 1
#define IM_SEEK_END 2
//typedef struct im_reader im_reader;

typedef struct im_reader im_reader;

// abstracted interface for reading
struct im_reader {
    size_t (*read)(im_reader*, void* , size_t );
    int (*seek)(im_reader*, long , int);
    int (*tell)(im_reader*);
    int (*eof)(im_reader*);
    int (*error)(im_reader*);
    int (*close)(im_reader*);
};


// open a file for reading (binary mode)
// the returned reader uses stdio (fopen, fread etc...)
extern im_reader* im_open_file_reader( const char* filename, ImErr* err);

// TODO: memory-based reader
//extern im_reader* im_open_mem_reader( const void* data, size_t nbytes );

// Close and free reader
extern int im_close_reader(im_reader* rdr);

// im_read reads nbytes from the reader, and returns the actual number read.
// im_read does not distiguish between EOF and other error conditions, so
// use im_eof() to check if the end of the data has been hit.
static inline size_t im_read(im_reader* rdr, void* buf, size_t nbytes)
    { return rdr->read(rdr, buf, nbytes); }

// returns 0 for success, non-zero for error
static inline int im_seek(im_reader* rdr, long pos, int whence)
    { return rdr->seek(rdr, pos, whence); }
 
// returns current position, -1 for error
static inline int im_tell(im_reader* rdr)
    { return rdr->tell(rdr); }


// im_eof returns non-zero if the reader is out of data
static inline int im_eof(im_reader* rdr)
    { return rdr->eof(rdr); }

// im_error returns non-zero if the reader is in error state (including eof)
static inline int im_error(im_reader* rdr)
    { return rdr->error(rdr); }



// TODO: kill the byte-unpacking io fns? They don't handle errors well.
static inline uint8_t im_read_u8(im_reader* rdr)
{
    uint8_t dat;
    if (im_read(rdr,&dat,sizeof(dat)) != sizeof(dat)) {
        return 0;
    }
    return dat;
}

static inline uint16_t im_read_u16be(im_reader* rdr)
{
    uint8_t dat[2];
    if (im_read(rdr,dat,sizeof(dat)) != sizeof(dat)) {
        return 0;
    }
    return ((uint16_t)dat[0]<<8) | ((uint16_t)dat[1]);
}

// cheesy
static inline int16_t im_read_s16be(im_reader* rdr)
    { return (int16_t)im_read_u16be(rdr); }
static inline int8_t im_read_s8(im_reader* rdr)
    { return (int8_t)im_read_u8(rdr); }

static inline uint16_t im_read_u16le(im_reader* rdr)
{
    uint8_t dat[2];
    if (im_read(rdr,dat,sizeof(dat)) != sizeof(dat)) {
        return 0;
    }
    return ((uint16_t)dat[1]<<8) | ((uint16_t)dat[0]);
}

static inline uint32_t im_read_u32be(im_reader* rdr)
{
    uint8_t dat[4];
    if (im_read(rdr,dat,sizeof(dat)) != sizeof(dat)) {
        return 0;
    }
    return ((uint32_t)dat[0]<<24) |
        ((uint32_t)dat[1]<<16) |
        ((uint32_t)dat[2]<<8) |
        ((uint32_t)dat[3]);
}

static inline uint32_t im_read_u32le(im_reader* rdr)
{
    uint8_t dat[4];
    if (im_read(rdr,dat,sizeof(dat)) != sizeof(dat)) {
        return 0;
    }
    return ((uint32_t)dat[3]<<24) |
        ((uint32_t)dat[2]<<16) |
        ((uint32_t)dat[1]<<8) |
        ((uint32_t)dat[0]);
}


// abstracted interface for writing
typedef struct im_writer {
    size_t (*write)(struct im_writer*, const void* , size_t);
    int (*close)(struct im_writer*);
} im_writer;


// open a file for writing
// (backed by fopen/fwrite etc)
extern im_writer* im_open_file_writer( const char* filename, ImErr* err);
//extern im_writer* im_open_mem_writer( void* buf, size_t buf_size );

// Close and free writer returns error code...
extern int im_close_writer(im_writer* w);


// Write data out. returns number of bytes successfully written
// if <nbytes, an error has occurred
static inline size_t im_write( im_writer* w, const void* data, size_t nbytes)
    { return w->write(w,data,nbytes); }



/**********************
 * Image handling stuff
 **********************/

// creates a new image (no palette, even if indexed)
extern im_img* im_img_new(int w, int h, int d, ImFmt fmt, ImDatatype datatype);

// Free an image (and its content and palette)
extern void im_img_free(im_img *img);

// Create a new copy of an existing image
extern im_img* im_img_clone(const im_img* src_img);

// Convert an image to another format/datatype (a copy is returned, 
extern im_img* im_img_convert( const im_img* srcImg, ImFmt destFmt, ImDatatype destDatatype );

// Load a single image from a file
// If the file has multiple frames, only the first one will be returned
extern im_img* im_img_load( const char* filename, ImErr* err);

// Read a single image from an abstracted reader
extern im_img* im_img_read( im_reader* rdr, ImErr* err);

// TODO:
// Save a single image to a file
// extern bool im_img_save( im_img* img, const char* filename, ImErr* err);
// extern bool im_img_write( im_img* img, im_writer* w, ImErr* err);


// Helper to calculate the bytes per pixel required for a given format/datatype combination
extern size_t im_bytesperpixel(ImFmt fmt, ImDatatype datatype);

// Fetch a pointer to a specific pixel
static inline void* im_img_pos(const im_img *img, int x, int y)
    { return ((uint8_t*)(img->pixel_data)) + (y*img->pitch) + (x*im_bytesperpixel(img->format, img->datatype)); }

// Fetch a pointer to the start of a specific row
static inline void* im_img_row(const im_img *img, int row)
    { return im_img_pos(img,0,row); }

static inline size_t im_img_bytesperpixel(const im_img *img)
    { return im_bytesperpixel(img->format, img->datatype); }

// TODO: metadata access...
// - animation frame delay
// - frame disposal
// - comments/datestamps/etc...


// image palette fns

// set a palette. If data is non-NULL, the palette will be initialised using
// the colours it points to.
// If data is NULL, the palette data will be zeroed.
// ncolours==0 means no palette.
extern bool im_img_pal_set( im_img* img, ImPalFmt fmt, int ncolours, const void* data);

// load colours into existing palette, converting format if necessary
extern bool im_img_pal_write( im_img* img, int first_colour, int num_colours, ImPalFmt data_fmt, const void* data);

// read colours out of palette, converting format if necessary
extern bool im_img_pal_read( im_img* img, int first_colour, int num_colours, ImPalFmt dest_fmt, void* dest);

// returns true if images have identical palettes (same format,
// number of colours and colour data)
extern bool im_img_pal_equal(const im_img* a, const im_img* b);






/**********************
 * bundle handling stuff
 **********************/

// a bundle is a collection of images (eg an animation)
// currently, only multi frame bundles are supported,
// but the intention is that it can handle mipmaps, cubemap faces
// layers etc...
// The bundle owns all the images it contains. When the bundle is
// freed the images are also freed.
typedef struct im_bundle im_bundle;

// each image in a bundle has an id which gives it's frame/miplevel/layer/face
// TODO: this name will change
typedef struct SlotID {
    int frame,mipmap,layer,face;
} SlotID;


// Load a bundle from a file. This is fine for file formats which only
// contain a single image.
// If an error occurs, NULL is returned and err is set.
extern im_bundle* im_bundle_load( const char* filename, ImErr* err);

// Read a bundle from an abstracted file.
// If an error occurs, NULL is returned and err is set.
extern im_bundle* im_bundle_read( im_reader* rdr, ImErr* err);

// Write a bundle out to a file
// Upon error, NULL is returned and err is set.
extern bool im_bundle_save( im_bundle* bundle, const char* filename, ImErr* err);

// TODO:
// extern bool im_bundle_write( im_bundle* bundle, im_writer* w, ImErr* err);


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
//extern SlotID im_bundle_extents(im_bundle* b);


// return the number of animation frames in a bundle
extern int im_bundle_num_frames(im_bundle* b);

// return a single animation frame from a bundle
// (bundle still retains ownership of the image)
extern im_img* im_bundle_get_frame(im_bundle* b, int n);


//TODO:
// Detach an image from a bundle. Ownership of the image
// is passed to the caller.
// extern im_img* im_bundle_detach(im_bundle* b, SlotID id);


#ifdef __cplusplus
}
#endif

#endif // IMPY_H

