#include "impy.h"
#include "private.h"
#include "bmp.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>

void* imalloc( size_t size)
{
    return malloc(size);
}

void ifree(void* ptr)
{
    free(ptr);
}



#if 0
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

static struct handler* pick_handler_for_read(im_in* rdr)
{
    int i;

    // is_iff requires 12 bytes...
    uint8_t cookie[16];
    if (im_read(rdr, cookie, sizeof(cookie)) != sizeof(cookie)) {
        return NULL;
    }

    // reset reader
    if( im_seek(rdr, 0, IM_SEEK_SET) != 0 ) {
        return NULL;
    }
    for (i=0; handlers[i] != NULL; ++i) {
        if (handlers[i]->match_cookie(cookie,sizeof(cookie))) {
            return handlers[i];
        }
    }

    return NULL;
}
#endif

// Misc

ImFileFmt im_guess_file_format(const char* filename)
{
    const char* ext = ext_part(filename);
    if (istricmp(ext, ".png") == 0) {
        return IM_FILEFMT_PNG;
    }
    if (istricmp(ext, ".gif") == 0) {
        return IM_FILEFMT_GIF;
    }
    if (istricmp(ext, ".bmp") == 0) {
        return IM_FILEFMT_BMP;
    }
    if (istricmp(ext, ".jpeg") ==0 ) {
        return IM_FILEFMT_JPEG;
    }
    if (istricmp(ext, ".jpg") ==0 ) {
        return IM_FILEFMT_JPEG;
    }
    if (istricmp(ext, ".pcx") ==0 ) {
        return IM_FILEFMT_PCX;
    }
    if (istricmp(ext, ".tga") ==0 ) {
        return IM_FILEFMT_TARGA;
    }
    if (istricmp(ext, ".iff") ==0 ) {
        return IM_FILEFMT_ILBM;
    }
    if (istricmp(ext, ".lbm") ==0 ) {
        return IM_FILEFMT_ILBM;
    }
    if (istricmp(ext, ".ilbm") ==0 ) {
        return IM_FILEFMT_ILBM;
    }
    if (istricmp(ext, ".pbm") ==0 ) {
        return IM_FILEFMT_ILBM;
    }
    return IM_FILEFMT_UNKNOWN;
}

// Writing

extern im_writer* ibmp_new_writer(im_out* out, ImErr *err); // bmp_write.c
extern im_writer* igif_new_writer(im_out* out, ImErr *err); // gif_write.c
extern im_writer* ipng_new_writer(im_out* out, ImErr* err); // png_write.c

im_writer* im_writer_new(ImFileFmt file_fmt, im_out* out, ImErr* err)
{
    switch (file_fmt) {
        case IM_FILEFMT_PNG:
            return ipng_new_writer(out, err);
        case IM_FILEFMT_GIF:
            return igif_new_writer(out, err);
        case IM_FILEFMT_BMP:
            return ibmp_new_writer(out, err);
        default:
           *err = ERR_UNSUPPORTED;
          return NULL; 
    }
}

void im_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt)
    { writer->handler->begin_img(writer, w, h, fmt); }

void im_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t *data)
    { writer->handler->write_rows(writer, num_rows, data); }

void im_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours)
    { writer->handler->set_palette(writer, pal_fmt, num_colours, colours); }

ImErr im_writer_finish(im_writer* writer)
    { return writer->handler->finish(writer); }

ImErr im_writer_err(im_writer* writer)
    { return writer->err; }


im_writer* im_writer_open_file(const char *filename, ImErr* err)
{
    im_out* out;
    ImFileFmt file_fmt;
    
    file_fmt = im_guess_file_format(filename);

    out = im_out_open_file(filename, err);
    if (!out) {
        return NULL;
    }

    return im_writer_new(file_fmt, out, err);
}


// Reading

im_reader* im_reader_new(ImFileFmt file_fmt, im_in* in, ImErr* err)
{
    switch (file_fmt) {
        case IM_FILEFMT_GIF:
            return im_new_gif_reader(in, err);
        case IM_FILEFMT_PNG:
            return im_new_generic_reader(iread_png_image, in ,err);
        case IM_FILEFMT_BMP:
            return im_new_generic_reader(iread_bmp_image, in ,err);
        case IM_FILEFMT_JPEG:
            return im_new_generic_reader(iread_jpeg_image, in ,err);
        case IM_FILEFMT_PCX:
            return im_new_generic_reader(iread_pcx_image, in ,err);
        case IM_FILEFMT_TARGA:
            return im_new_generic_reader(iread_targa_image, in ,err);
        default:
           *err = ERR_UNSUPPORTED;
          return NULL; 
    }
}

im_reader* im_reader_open_file(const char* filename, ImErr* err)
{
    im_in* in;
    ImFileFmt file_fmt;
    
    // TODO: magic cookie sniffing instead of extension guessing here!
    file_fmt = im_guess_file_format(filename);

    in = im_in_open_file(filename, err);
    if (!in) {
        return NULL;
    }

    return im_reader_new(file_fmt, in, err);
}


bool im_get_img(im_reader* reader, im_imginfo* info)
    { return reader->handler->get_img(reader, info); }

ImErr im_reader_finish(im_reader* reader)
    { return reader->handler->finish(reader); }

ImErr im_reader_err(im_reader* reader)
    { return reader->err; }

void im_read_rows(im_reader* reader, unsigned int num_rows, uint8_t* buf)
    { return reader->handler->read_rows(reader, num_rows, buf); }

void im_read_palette(im_reader* reader, uint8_t* buf)
    { return reader->handler->read_palette(reader, buf); }


