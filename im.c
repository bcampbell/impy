#include "im.h"

#include <stdlib.h>

struct driver {
    bool (*match_cookie)(const uint8_t* buf, int nbytes);
    im_Img* (*read_img)( im_reader* rdr);
    im_bundle* (*read_bundle)( im_reader* rdr);

    bool (*match_ext)(const char* file_extension );
    bool (*write_img)( im_writer* out, im_Img* img );
    bool (*write_bundle)( im_writer* out, im_bundle* bundle );
};

static struct driver pngdriver = {isPng,readPng, NULL, NULL, writePng, NULL};
static struct driver gifdriver = {isGif,NULL, multiReadGif, NULL, NULL, NULL};
static struct driver *drivers[2] = { &pngdriver, &gifdriver };


static struct driver* pick_driver_for_read(im_reader* rdr);

void* imalloc( size_t size)
{
    return malloc(size);
}

void ifree(void* ptr)
{
    free(ptr);
}


static ImErr errCode = ERR_NONE;


void im_err(ImErr err)
{
    errCode = err;
}

// calc bytes per pixel
static int bpp(ImFmt fmt, ImDatatype datatype) {
    int n=0;

    switch (datatype) {
        case DT_U8:
        case DT_S8:
            n=1;
            break;
        case DT_U16:
        case DT_S16:
        case DT_FLOAT16:
            n=2;
            break;
        case DT_U32:
        case DT_S32:
        case DT_FLOAT32:
            n=4;
            break;
        case DT_FLOAT64:
            n=8;
            break;
    }

    switch (fmt) {
        case FMT_RGB: return 3*n;
        case FMT_RGBA: return 4*n;
        case FMT_BGR: return 3*n;
        case FMT_BGRA: return 4*n;
        case FMT_COLOUR_INDEX: return 1*n;
        case FMT_ALPHA: return 1*n;
        case FMT_LUMINANCE: return 1*n;
    }
    return 0;
}


im_Img* im_img_new(int w, int h, int d, ImFmt fmt, ImDatatype datatype)
{
    im_Img* img;
    int bytesPerPixel;
    int datsize;

    if( w<1 || h<1 || d<1 ) {
        im_err(ERR_BADPARAM);
        return NULL;
    }

    bytesPerPixel = bpp(fmt,datatype);
    if(bytesPerPixel==0) {
        im_err(ERR_BADPARAM);
        return NULL;
    }


    datsize = bytesPerPixel * w * h;

    img = imalloc(datsize);
    if (img==NULL) {
        im_err(ERR_NOMEM);
        return 0;
    }

    img->Width = w;
    img->Height = h;
    img->Depth = d;
    img->Format = fmt;
    img->Datatype = datatype;
    img->BytesPerPixel = bytesPerPixel;
    img->Pitch = img->BytesPerPixel * img->Width;
    img->Data = imalloc(img->Height * img->Pitch * img->Depth);
    if (!img->Data) {
        im_err(ERR_NOMEM);
        ifree(img);
        return NULL;
    }
    img->Palette = NULL;

    return img;
}

void im_img_free(im_Img *img)
{
    if (img->Palette) {
        im_pal_free(img->Palette);
    }

    ifree(img);
}

void* im_img_row(im_Img *img, int row)
{
    return img->Data + (row * img->Pitch);
}



im_Pal* im_pal_new( ImPalFmt fmt, int numColours )
{
    im_Pal* pal = imalloc(sizeof(im_Pal));
    size_t entsize;
    if (!pal) {
        im_err(ERR_NOMEM);
        return NULL;
    }
    pal->Format = fmt;
    pal->NumColours = numColours;

    switch(fmt) {
        case PALFMT_RGB: entsize = 3; break;
        case PALFMT_RGBA: entsize = 4; break;
    }

    pal->Data = imalloc(numColours * entsize);
    if (!pal->Data) {
        ifree(pal);
        im_err(ERR_NOMEM);
        return NULL;
    }
    return pal;
}

void im_pal_free( im_Pal* pal )
{
    ifree(pal->Data);
    ifree(pal);
}




static struct driver* pick_driver_for_read(im_reader* rdr)
{
    int i;
    struct driver* d = NULL;

    uint8_t cookie[8];
    if (im_read(rdr, cookie, sizeof(cookie)) != sizeof(cookie)) {
        im_err(ERR_FILE);
        return NULL;
    }

    // reset reader
    if( im_seek(rdr, 0, IM_SEEK_SET) != 0 ) {
        im_err(ERR_FILE);
        return NULL;
    }
    for (i=0; i<2; ++i) {
        d = drivers[i];
        if (d->match_cookie(cookie,sizeof(cookie))) {
            return d;
        }
    }

    im_err(ERR_UNKNOWN_FILE_TYPE);
    return NULL;
}


im_Img* im_img_load( const char* filename)
{
    im_reader* rdr = im_open_file_reader(filename);
    im_Img* img = im_img_read(rdr);
    im_close_reader(rdr);
    return img;
}

im_Img* im_img_read( im_reader* rdr)
{
    struct driver* drv = pick_driver_for_read(rdr);
    if(!drv) {
        return NULL;
    }


    if (drv->read_img) {
        return drv->read_img(rdr);
    }
    if (drv->read_bundle) {
        im_Img* img;
        im_bundle* b = drv->read_bundle(rdr);
        SlotID id = {0};
        if (!b) {
            return NULL;
        }
        img = im_bundle_get(b,id);
        // TODO: detach image and delete bundle!
        return img;
    }

    im_err(ERR_UNKNOWN_FILE_TYPE);
    return NULL;
}

im_bundle* im_bundle_load( const char* filename)
{
    im_reader* rdr = im_open_file_reader(filename);
    im_bundle* b = im_bundle_read(rdr);
    im_close_reader(rdr);
    return b;
}

im_bundle* im_bundle_read( im_reader* rdr)
{
    struct driver* drv = pick_driver_for_read(rdr);
    if(!drv) {
        return NULL;
    }

    if (drv->read_bundle) {
        return drv->read_bundle(rdr);
    }
    if (drv->read_img) {
        im_bundle* b;
        im_Img* img = drv->read_img(rdr);
        if (!img) {
            return NULL;
        }
        b = im_bundle_new();
        if (!b) {
            im_img_free(img);
            return NULL;
        }
        SlotID id = {0};
        if (!im_bundle_set(b, id, img)) {
            im_img_free(img);
            im_bundle_free(b);
            return NULL;
        }
        return b;
    }

    im_err(ERR_UNKNOWN_FILE_TYPE);
    return NULL;
}

