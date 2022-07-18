#include "impy.h"
#include "private.h"

#include <png.h>
#include <stdio.h>
#include <stdint.h>

static void info_callback(png_structp png_ptr, png_infop info_ptr);
static void row_callback(png_structp png_ptr, png_bytep new_row, png_uint_32 row_num, int pass);
static void end_callback(png_structp png_ptr, png_infop info);
static bool apply_palette(png_structp png_ptr, png_infop info, im_img* img);


// struct to track stuff needed during png progressive reading
struct cbdat {
    ImErr err;
    int num_passes;
    im_img* image;
};

#if 0
static bool is_png(const uint8_t* buf, int nbytes);
static bool match_png_ext(const char* file_ext);

static bool match_png_ext(const char* file_ext)
{
    return (istricmp(file_ext,".png")==0);
}


static bool is_png(const uint8_t* buf, int nbytes)
{
    if( png_sig_cmp((png_bytep)buf,0,nbytes) == 0 ) {
        return true;
    } else {
        return false;
    }
}
#endif

im_img* iread_png_image(im_in* rdr, ImErr *err)
{
    struct cbdat cbDat = {ERR_NONE,0,NULL};
    png_structp png_ptr;
    png_infop info_ptr;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,NULL,NULL);
    if (!png_ptr) {
        *err = ERR_NOMEM;
        return NULL;
    }


    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
       png_destroy_read_struct(&png_ptr, NULL, NULL);
       *err = ERR_NOMEM;
       return NULL;
    }


    //png_set_read_fn( png_ptr, fp, readFunc);

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        if (cbDat.image) {
            im_img_free(cbDat.image);
        }
        *err = cbDat.err;
        if (*err == ERR_NONE) {
            *err = ERR_EXTLIB;
        }
        return NULL;
    }

//   png_set_sig_bytes(png_ptr, cookieRead);

    // use the progressive read mechanism - we'll pump the data into
    // libpng with png_process_data()
    png_set_progressive_read_fn(png_ptr, (void*)&cbDat, info_callback, row_callback, end_callback);

    while(1) {
        uint8_t buf[4096];

        size_t n;
        n = im_read(rdr, buf, sizeof(buf));
        if (n>0) {
            png_process_data(png_ptr, info_ptr, buf, n);
        } else {
            if ( im_eof(rdr) ) {
                break;
            }
            // an error has occurred
            // TODO: set error from rdr
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            if (cbDat.image) {
               im_img_free(cbDat.image);
            }
            return NULL;
        }
    }

    // success - clean up and exit
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return cbDat.image;
}

//
static void info_callback(png_structp png_ptr, png_infop info_ptr)
{
    struct cbdat* cbDat = (struct cbdat*)png_get_progressive_ptr(png_ptr);

    png_uint_32 width, height;
    int bitDepth, colourType, interlaceType, compressionType, filterMethod;
    //int number_passes;

    png_get_IHDR(png_ptr, info_ptr, &width, &height,
        &bitDepth, &colourType, &interlaceType,
        &compressionType, &filterMethod);

    // setup transformations

    // if there's a transparency chunk but no palette, transform into proper alpha channel
	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        if (!png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE)) {
            png_set_tRNS_to_alpha(png_ptr);
        }
    }

    if (bitDepth<8) {
        if (colourType==PNG_COLOR_TYPE_GRAY) {
            // scale up grayscale values
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        } else {
            // unpack pixels into their own bytes (but keep original value)
    		png_set_packing(png_ptr);
        }
    }

    // TODO: endianness for multi-byte channels? (png_set_swap())
    // TODO: gamma handling?

    //
    cbDat->num_passes = png_set_interlace_handling(png_ptr);

    png_read_update_info(png_ptr, info_ptr);


    /*********** create image *************/
    {
        ImFmt fmt;
        ImDatatype dt;
        switch (colourType) {
            case PNG_COLOR_TYPE_RGB:        fmt = IM_FMT_RGB; break;
            case PNG_COLOR_TYPE_RGB_ALPHA:  fmt = IM_FMT_RGBA; break;
            case PNG_COLOR_TYPE_PALETTE:    fmt = IM_FMT_INDEX8; break;
            // TODO:
            case PNG_COLOR_TYPE_GRAY:
            case PNG_COLOR_TYPE_GRAY_ALPHA:
            default:
                cbDat->err = ERR_UNSUPPORTED;
                png_error(png_ptr, "unsupported color type");
        }

        if (bitDepth<=8 ) {
            dt = DT_U8;
        } else if (bitDepth==16) {
            dt = DT_U16;
        } else {
            cbDat->err = ERR_UNSUPPORTED;
            png_error(png_ptr, "unsupported color type");
        }
        
        cbDat->image = im_img_new(width,height,1,fmt,dt);
        if (cbDat->image == NULL) {
            cbDat->err = ERR_NOMEM;
            png_error(png_ptr, "im_img_new() failed");
        }

        // if there's a palette, install it
        if (!apply_palette(png_ptr, info_ptr, cbDat->image)) {
            cbDat->err = ERR_NOMEM;
            png_error(png_ptr, "failed while installing palette");
        }
    }
}


static void row_callback(png_structp png_ptr, png_bytep new_row,
    png_uint_32 row_num, int pass)
{
    struct cbdat* cbDat = (struct cbdat*)png_get_progressive_ptr(png_ptr);
    void* destpixels = im_img_row(cbDat->image, row_num);
    //printf("row %d (pass %d of %d, ptr=%p)\n", row_num, pass, cbDat->num_passes, new_row);
    png_progressive_combine_row(png_ptr, destpixels, new_row);
} 

static void end_callback(png_structp png_ptr, png_infop info)
{
}


static bool apply_palette(png_structp png_ptr, png_infop info_ptr, im_img* img) {
    png_colorp colours;
    int num_colours;
    int i;
    png_bytep trans = NULL;
    int  num_trans;
    ImFmt palfmt;

    if (png_get_PLTE(png_ptr, info_ptr, &colours, &num_colours) != PNG_INFO_PLTE) {
        // no PLTE chunk, so our work here is done
        return true;
    }

    // png palettes are RGB only, so if there is a tRNS chunk, we'll
    // use it to provide alpha values
    
    // get any tRNS data
    if (png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, NULL) == PNG_INFO_tRNS) {
    } else {
        num_trans = 0;
    }

    // alloc our palette
    if (num_trans>0) {
        palfmt = IM_FMT_RGBA;
    } else {
        palfmt = IM_FMT_RGB;
    }

    if (!im_img_pal_set( img, palfmt, num_colours,NULL)) {
        return false;
    }
    //
    switch (palfmt) {
        case IM_FMT_RGB:
            {
                uint8_t* colp = img->pal_data;
                for (i = 0; i < num_colours; ++i) {
                    *colp++ = colours[i].red;
                    *colp++ = colours[i].green;
                    *colp++ = colours[i].blue;
                }
            }
            break;
        case IM_FMT_RGBA:
            {
                uint8_t* colp = img->pal_data;
                for (i = 0; i < num_colours; ++i) {
                    *colp++ = colours[i].red;
                    *colp++ = colours[i].green;
                    *colp++ = colours[i].blue;
                    *colp++ = (i<num_trans) ? trans[i] : 255;
                }
            }
            break;
        default:
            break;
    }

    return true;
}

