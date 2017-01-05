#include "im.h"
#include "private.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>



static struct handler *handlers[] = { &handle_png, &handle_gif, NULL };


static struct handler* pick_handler_for_read(im_reader* rdr);

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

    img->XOffset = 0;
    img->YOffset = 0;

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

// returns true if palettes are equal (same format, same colours)
bool im_pal_equal( im_Pal* a, im_Pal* b )
{
    size_t nbytes;

    if( a->NumColours!=b->NumColours) {
        return false;
    }
    if( a->Format!=b->Format) {
        return false;
    }
    switch (a->Format) {
        case PALFMT_RGB: nbytes=3; break;
        case PALFMT_RGBA: nbytes=4; break;
        default: return false;
    }
    if(memcmp( a->Data, b->Data, nbytes) != 0 ) {
        return false;
    }
    return true;
}


static struct handler* pick_handler_by_filename(const char* filename)
{
    const char* ext = ext_part(filename);
    int i;
    for (i=0; handlers[i] != NULL; ++i) {
        if (handlers[i]->match_ext(ext)) {
            return handlers[i];
        }
    }
    return NULL;
}

static struct handler* pick_handler_for_read(im_reader* rdr)
{
    int i;

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
    for (i=0; handlers[i] != NULL; ++i) {
        if (handlers[i]->match_cookie(cookie,sizeof(cookie))) {
            return handlers[i];
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
    struct handler* h = pick_handler_for_read(rdr);
    if(!h) {
        return NULL;
    }


    if (h->read_img) {
        return h->read_img(rdr);
    }
    if (h->read_bundle) {
        im_Img* img;
        im_bundle* b = h->read_bundle(rdr);
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
    struct handler* h = pick_handler_for_read(rdr);
    if(!h) {
        return NULL;
    }

    if (h->read_bundle) {
        return h->read_bundle(rdr);
    }
    if (h->read_img) {
        im_bundle* b;
        im_Img* img = h->read_img(rdr);
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


bool im_bundle_save( im_bundle* bundle, const char* filename )
{
    bool result = false;
    struct handler* h = pick_handler_by_filename(filename);
    if( !h ) {
        im_err(ERR_UNKNOWN_FILE_TYPE);
        return false;
    }

    im_writer* out = im_open_file_writer(filename);
    if( !out) {
        return false;
    }

    if( h->write_bundle) {
        result = h->write_bundle(bundle,out);
    } else if (h->write_img) {
        // Just write first frame
        SlotID id = {0};
        im_Img* img = im_bundle_get(bundle,id);
        if( img) { 
            result = h->write_img(img,out);
        } else{
            im_err(ERR_BADPARAM);
            result = false;
        }
    }

    if (im_close_writer(out)) {
        return false;
    }
    return result;

    im_err(ERR_UNSUPPORTED);
    return false;
}


