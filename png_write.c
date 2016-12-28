#include "im.h"
#include <png.h>
#include <stdio.h>
#include <stdint.h>

static void custom_write(png_structp png_ptr,
    png_bytep data, png_size_t length);
static void custom_flush(png_structp png_ptr);


bool writePng( im_writer* out, im_Img* img )
{
    png_structp png_ptr;
    png_infop info_ptr;


    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,NULL,NULL);
    if (!png_ptr) {
        im_err(ERR_NOMEM);
        return false;
    }


    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
       png_destroy_write_struct(&png_ptr, NULL);
       im_err(ERR_NOMEM);
       return false;
    }


    if (setjmp(png_jmpbuf(png_ptr)))
    {
       png_destroy_write_struct(&png_ptr, &info_ptr);
       // TODO: if no im_err set, set a generic error
       return false;
    }

    png_set_write_fn(png_ptr,
        (voidp)out, custom_write, custom_flush);

      //voidp write_io_ptr = png_get_io_ptr(write_ptr);

    {
        png_uint_32 width,height;
        int bit_depth,color_type;
        int y;
        width = img->Width;
        height = img->Height;

        switch (img->Format)
        {
            case FMT_RGB:
            case FMT_BGR:
                color_type = PNG_COLOR_TYPE_RGB;
                break;
            case FMT_RGBA:
            case FMT_BGRA:
                color_type = PNG_COLOR_TYPE_RGB_ALPHA;
                break;
            case FMT_COLOUR_INDEX:
                color_type = PNG_COLOR_TYPE_PALETTE;
                break;
            case FMT_LUMINANCE:
                color_type = PNG_COLOR_TYPE_GRAY;
                break;
            case FMT_ALPHA:
                im_err(ERR_UNSUPPORTED);
                goto bailout;
                break;
        }
        switch (img->Datatype)
        {
            case DT_U8: bit_depth=8; break;
            case DT_U16: bit_depth=16; break;
            case DT_S8:
            case DT_S16:
            case DT_U32:
            case DT_S32:
            case DT_FLOAT16:
            case DT_FLOAT32:
            case DT_FLOAT64:
            default:
                im_err(ERR_UNSUPPORTED);
                goto bailout;
                break;
        }


        png_set_IHDR(png_ptr, info_ptr,
            width, height,
            bit_depth, color_type,
            PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT,
            PNG_FILTER_TYPE_DEFAULT);

        if (img->Palette) {
            png_set_PLTE(png_ptr, info_ptr, palette, num_palette);
            png_set_tRNS(png_ptr, info_ptr, trans, num_trans,
                                  trans_values);
        }


        // TODO: set other chunks here? text etc...


        // write out everything up to the image data
        png_write_info(png_ptr, info_ptr);

        // set up any transforms
        if (img->Format == FMT_BGR || img->Format == FMT_BGRA) {
            png_set_bgr(png_ptr);
        }

        // now write the image data
        for(y=0; y<height; ++y) {
            png_bytep row = im_img_row(img,y);
            png_write_row(png_ptr, row);
        }


        png_write_end(png_ptr, info_ptr);
    }
    // success! clean up and exit
    png_destroy_write_struct(&png_ptr, &info_ptr, NULL);
    return true;

bailout:
    png_destroy_write_struct(&png_ptr, &info_ptr, NULL);
    return false;
}



static void custom_write(png_structp png_ptr,
    png_bytep data, png_size_t length)
{
    im_writer* w = (im_writer*)png_get_io_ptr(png_ptr);
    size_t n = im_write(w, data, length);
    if (n!=length) {
        png_error(png_ptr, "write error");
    }
}

static void custom_flush(png_structp png_ptr)
{
}

