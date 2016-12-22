#include "im.h"

#include <stdlib.h>

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


im_Img* im_img_new(int w, int h, ImFmt fmt, ImDatatype datatype)
{
    im_Img* img;
    int bytesPerPixel;
    int datsize;

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
    img->Depth = 1;
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



im_Pal* im_pal_new( int numColours )
{
    im_Pal* pal = imalloc(sizeof(im_Pal));
    if (!pal) {
        im_err(ERR_NOMEM);
        return NULL;
    }

    pal->Data = imalloc(numColours * 4);
    if (!pal->Data) {
        ifree(pal);
        im_err(ERR_NOMEM);
        return NULL;
    }
}

void im_pal_free( im_Pal* pal )
{
    ifree(pal->Data);
    ifree(pal);
}

