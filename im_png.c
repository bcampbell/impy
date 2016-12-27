#include "im.h"
#include <png.h>
#include <stdio.h>
#include <stdint.h>


static void info_callback(png_structp png_ptr, png_infop info_ptr);
static void row_callback(png_structp png_ptr, png_bytep new_row, png_uint_32 row_num, int pass);
static void end_callback(png_structp png_ptr, png_infop info);


// struct to track stuff needed during png progressive reading
struct cbdat {
    int num_passes;
    im_Img* image;
};


im_Img* loadPng(const char* fileName)
{
    struct cbdat cbDat = {0};

    //uint8_t cookieBuf[8] = {0};
    //size_t cookieRead;
    im_reader* rdr = im_open_file_reader(fileName);
    if (!rdr) {
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
            im_close_reader(rdr);
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            if (cbDat.image) {
               im_img_free(cbDat.image);
            }
            return NULL;
        }
    }

    // success - clean up and exit
    im_close_reader(rdr);
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

        // if there's a palette, install it
        {

            png_colorp colours;
            int num_colours;
            int i;
            if (png_get_PLTE(png_ptr, info_ptr, &colours, &num_colours) == PNG_INFO_PLTE) {
                // there is a palette
                // png palettes are RGB only, so if there is a tRNS chunk, we'll
                // use it to provide alpha values
                im_Pal *pal;
                uint8_t* colp;
                png_bytep trans = NULL;
                int  num_trans;
                
                // alloc our palette
                pal = im_pal_new(num_colours);
                if (!pal) {
                    png_error(png_ptr, "im_pal_new() failed");
                }
                cbDat->image->Palette = pal;
                
                // get any tRNS data
                if (png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, NULL) == PNG_INFO_tRNS) {
                } else {
                    num_trans = 0;
                }

                //
                colp = pal->Data;
                for (i = 0; i < num_colours; ++i) {
                    *colp++ = colours[i].red;
                    *colp++ = colours[i].green;
                    *colp++ = colours[i].blue;
                    if (i<num_trans) {
                        *colp++ = trans[i];
                    } else {
                        *colp++ = 255;
                    }
                }
            }
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
}

