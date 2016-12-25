#include "im.h"
#include <png.h>
#include <stdio.h>
#include <stdint.h>

/*
static readFunc(png_structp png_ptr, png_bytep data, png_size_t length)
{
    FILE* fp = (FILE*)png_get_io_ptr(png_ptr);
    size_t n = fread(data, 1,length,fp);
    if (n != length) {
        png_error(png_ptr, "expected more data" );
    }
}
*/

static void info_callback(png_structp png_ptr, png_infop info_ptr);
static void row_callback(png_structp png_ptr, png_bytep new_row, png_uint_32 row_num, int pass);
static void end_callback(png_structp png_ptr, png_infop info);


//
struct cbdat {
    int num_passes;
    im_Img* image;
};


im_Img* loadPng(const char* fileName)
{
    struct cbdat cbDat = {0};

    //uint8_t cookieBuf[8] = {0};
    //size_t cookieRead;
    FILE *fp = fopen(fileName, "rb");
    if (!fp) {
        im_err(ERR_COULDNTOPEN);
        return NULL;
    }
/*
    cookieRead = fread(cookieBuf, 1, sizeof(cookieBuf), fp);
    if (cookieRead<sizeof(cookieBuf)) {
        im_err(ERR_MALFORMED);
        fclose(fp);
        return NULL;
    }

    if (png_sig_cmp(cookieBuf, 0, cookieRead) != 0)
    {
        im_err(ERR_MALFORMED);
        return NULL;
    }
*/

    png_structp png_ptr;
    png_infop info_ptr;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,NULL,NULL);
    if (!png_ptr) {
        im_err(ERR_NOMEM);
        return NULL;
    }


    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
       png_destroy_read_struct(&png_ptr, NULL, NULL);
       im_err(ERR_NOMEM);
       return NULL;
    }


    //png_set_read_fn( png_ptr, fp, readFunc);

    if (setjmp(png_jmpbuf(png_ptr)))
    {
       png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
       if (cbDat.image) {
           im_img_free(cbDat.image);
       }
       // TODO: if no im_err set, set a generic error
       return NULL;
    }

//   png_set_sig_bytes(png_ptr, cookieRead);

    // use the progressive read mechanism - we'll pump the data into
    // libpng with png_process_data()
    png_set_progressive_read_fn(png_ptr, (void*)&cbDat, info_callback, row_callback, end_callback);

    while(!feof(fp)) {
        uint8_t buf[4096];

        size_t n;
        n = fread(buf,1,sizeof(buf),fp);
        if (n>0) {
            png_process_data(png_ptr, info_ptr, buf, n);
        }

        if (ferror(fp)) {
            fclose(fp);
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            if (cbDat.image) {
               im_img_free(cbDat.image);
            }
            return NULL;
        }
    }

    // success - clean up and exit
    fclose(fp);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return cbDat.image;
}

//
static void info_callback(png_structp png_ptr, png_infop info_ptr)
{
    struct cbdat* cbDat = (struct cbdat*)png_get_progressive_ptr(png_ptr);

    im_Img* img = NULL;
    png_uint_32 width, height;
    int bitDepth, colourType, interlaceType, compressionType, filterMethod;
    int number_passes;

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
            case PNG_COLOR_TYPE_RGB:        fmt = FMT_RGB; break;
            case PNG_COLOR_TYPE_RGB_ALPHA:  fmt = FMT_RGBA; break;
            case PNG_COLOR_TYPE_PALETTE:    fmt = FMT_COLOUR_INDEX; break;
            // TODO:
            case PNG_COLOR_TYPE_GRAY:
            case PNG_COLOR_TYPE_GRAY_ALPHA:
            default:
                im_err(ERR_UNSUPPORTED);
                png_error(png_ptr, "unsupported color type");
        }

        if (bitDepth<=8 ) {
            dt = DT_U8;
        } else if (bitDepth==16) {
            dt = DT_U16;
        } else {
            im_err(ERR_UNSUPPORTED);
            png_error(png_ptr, "unsupported color type");
        }
        
        cbDat->image = im_img_new(width,height,1,fmt,dt);
        if (cbDat->image == NULL) {
            png_error(png_ptr, "im_img_new() failed");
        }
    }
    


 //   png_set_progressive_read_fn(png_ptr, img, info_callback, row_callback, end_callback);
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
    // TODO: handle palette here?
}

