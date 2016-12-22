#ifndef IM_H
#define IM_H

// image formats
typedef enum ImFmt {
    FMT_RGB,
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
    ERR_UNSUPPORTED     // file has stuff we don't (yet) support
} ImErr;



typedef struct im_Pal {
    void* Data;
    // TODO: palette format
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



// creates a new image (no palette, even if indexed)
im_Img* im_img_new( int w, int h, ImFmt fmt, ImDatatype datatype );
void im_img_free(im_Img *img);
void* im_img_row(im_Img *img, int row);

im_Pal* im_pal_new( int numColours );
void im_pal_free( im_Pal* pal );


extern void im_err(ImErr err);

extern im_Img* loadPng(const char* fileName);

#endif // IM_H

