#include "im.h"
#include <png.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static void custom_write(png_structp png_ptr,
    png_bytep data, png_size_t length);
static void custom_flush(png_structp png_ptr);
static bool suss_color_type( ImFmt fmt, int* color_type );
static bool suss_bit_depth( ImDatatype dt, int *bit_depth);
static bool plonk_palette(png_structp png_ptr, png_infop info_ptr, const im_img *img);


bool write_png_image(im_img* img, im_writer* out, ImErr* err)
{
    png_structp png_ptr;
    png_infop info_ptr;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,NULL,NULL);
    if (!png_ptr) {
        *err = ERR_NOMEM;
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
       png_destroy_write_struct(&png_ptr, NULL);
       *err = ERR_NOMEM;
       return false;
    }


    if (setjmp(png_jmpbuf(png_ptr)))
    {
       png_destroy_write_struct(&png_ptr, &info_ptr);
       // TODO: check error in writer, else post extlib error
       *err = ERR_EXTLIB;
       return false;
    }

    png_set_write_fn(png_ptr,
        (voidp)out, custom_write, custom_flush);

      //voidp write_io_ptr = png_get_io_ptr(write_ptr);

    {
        png_uint_32 width,height;
        int bit_depth,color_type;
        int y;
        ImFmt img_fmt;
        width = im_img_w(img);
        height = im_img_h(img);
        img_fmt = im_img_format(img);

        if (!suss_color_type(img_fmt, &color_type)) {
            *err = ERR_UNSUPPORTED;
            goto bailout;
        }

        if (!suss_bit_depth(im_img_datatype(img), &bit_depth)) {
            *err = ERR_UNSUPPORTED;
            goto bailout;
        }


        png_set_IHDR(png_ptr, info_ptr,
            width, height,
            bit_depth, color_type,
            PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT,
            PNG_FILTER_TYPE_DEFAULT);

        if (img_fmt == FMT_COLOUR_INDEX)
        {
            if( !plonk_palette( png_ptr, info_ptr, img) ) {
                *err = ERR_UNSUPPORTED;
                goto bailout;
            }
        }


        // TODO: set other chunks here? text etc...


        // write out everything up to the image data
        png_write_info(png_ptr, info_ptr);

        // set up any transforms
        if (img_fmt == FMT_BGR || img_fmt == FMT_BGRA) {
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
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return true;

bailout:
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return false;
}



// calculate png colour type PNG_COLOR_whatever
// return false if unsupported
static bool suss_color_type( ImFmt fmt, int* color_type )
{
    switch (fmt) {
        case FMT_RGB:
        case FMT_BGR:
            *color_type = PNG_COLOR_TYPE_RGB;
            break;
        case FMT_RGBA:
        case FMT_BGRA:
            *color_type = PNG_COLOR_TYPE_RGB_ALPHA;
            break;
        case FMT_COLOUR_INDEX:
            *color_type = PNG_COLOR_TYPE_PALETTE;
            break;
        case FMT_LUMINANCE:
            *color_type = PNG_COLOR_TYPE_GRAY;
            break;
        case FMT_ALPHA:
            return false;
    }
    return true;
}

// calculate png bit depth (8 or 16)
// return false if unsupported
static bool suss_bit_depth( ImDatatype dt, int *bit_depth)
{
    switch (dt)
    {
        case DT_U8: *bit_depth=8; break;
        case DT_U16: *bit_depth=16; break;
        case DT_S8:
        case DT_S16:
        case DT_U32:
        case DT_S32:
        case DT_FLOAT16:
        case DT_FLOAT32:
        case DT_FLOAT64:
        default:
            return false;
    }
    return true;
}


// return false if anything unsupported or otherwise odd
static bool plonk_palette(png_structp png_ptr, png_infop info_ptr, const im_img *img)
{
    png_color rgb[256];
    png_byte trans[256];
    int maxtrans = -1;
    int num_colours = im_img_pal_num_colours(img);
    ImPalFmt pal_fmt = im_img_pal_fmt(img);
    int i;

    if (num_colours>256) {
        return false;
    }

    if (pal_fmt == PALFMT_RGB) {
        uint8_t *src = im_img_pal_data(img);
        for ( i=0; i<num_colours; ++i ) {
            rgb[i].red = *src++;
            rgb[i].green = *src++;
            rgb[i].blue = *src++;
        }
    } else if (pal_fmt == PALFMT_RGBA) {
        uint8_t *src = im_img_pal_data(img);
        for ( i=0; i<num_colours; ++i ) {
            rgb[i].red = *src++;
            rgb[i].green = *src++;
            rgb[i].blue = *src++;
            trans[i] = *src++;
            if( trans[i]!=255) {
                maxtrans=i;
            }
        }
    } else {
        return false;
    }

    png_set_PLTE(png_ptr, info_ptr, rgb, num_colours);
    if (maxtrans!=-1) {
        png_set_tRNS(png_ptr, info_ptr, trans, maxtrans+1, NULL);
    }
    return true;
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
    // TODO
}

