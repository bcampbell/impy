#include "im.h"
#include "private.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>



static struct handler *handlers[] = { &handle_png, &handle_gif, NULL };


static struct handler* pick_handler_for_read(im_reader* rdr);
static void copy_pal_range( ImPalFmt src_fmt, const uint8_t* src, ImPalFmt dest_fmt, uint8_t *dest, int first_colour, int num_colours);

void* imalloc( size_t size)
{
    return malloc(size);
}

void ifree(void* ptr)
{
    free(ptr);
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


// returns true if palettes are equal (same format, same colours)
bool im_pal_equal( const im_img* a, const im_img* b )
{
    size_t nbytes;

    if (im_img_pal_num_colours(a) != im_img_pal_num_colours(b)) {
        return false;
    }
    if (im_img_pal_fmt(a) != im_img_pal_fmt(b)) {
        return false;
    }
    switch (im_img_pal_fmt(a)) {
        case PALFMT_RGB: nbytes=3; break;
        case PALFMT_RGBA: nbytes=4; break;
        default: return false;
    }
    if(memcmp( im_img_pal_data(a), im_img_pal_data(b), nbytes) != 0 ) {
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


im_img* im_img_load( const char* filename, ImErr* err)
{
    im_reader* rdr = im_open_file_reader(filename,err);
    if (!rdr) {
        return NULL;
    }
    im_img* img = im_img_read(rdr,err);
    im_close_reader(rdr);
    return img;
}

im_img* im_img_read( im_reader* rdr, ImErr* err)
{
    struct handler* h = pick_handler_for_read(rdr);
    if(!h) {
        *err = ERR_UNKNOWN_FILE_TYPE;
        return NULL;
    }


    if (h->read_img) {
        return h->read_img(rdr, err);
    }
    if (h->read_bundle) {
        im_img* img;
        im_bundle* b = h->read_bundle(rdr, err);
        SlotID id = {0};
        if (!b) {
            return NULL;
        }
        img = im_bundle_get(b,id);
        // TODO: detach image and delete bundle!
        return img;
    }

    *err = ERR_UNKNOWN_FILE_TYPE;
    return NULL;
}

im_bundle* im_bundle_load( const char* filename, ImErr* err)
{
    im_reader* rdr = im_open_file_reader(filename, err);
    if(!rdr) {
        return NULL;
    }
    im_bundle* b = im_bundle_read(rdr, err);
    im_close_reader(rdr);
    return b;
}

im_bundle* im_bundle_read( im_reader* rdr, ImErr* err)
{
    struct handler* h = pick_handler_for_read(rdr);
    if(!h) {
        *err = ERR_UNKNOWN_FILE_TYPE;
        return NULL;
    }

    if (h->read_bundle) {
        return h->read_bundle(rdr,err);
    }
    if (h->read_img) {
        im_bundle* b;
        im_img* img = h->read_img(rdr,err);
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
            *err = ERR_NOMEM;
            return NULL;
        }
        return b;
    }

    *err = ERR_UNKNOWN_FILE_TYPE;
    return NULL;
}


bool im_bundle_save( im_bundle* bundle, const char* filename, ImErr* err )
{
    bool result = false;
    struct handler* h = pick_handler_by_filename(filename);
    if( !h ) {
        *err = ERR_UNKNOWN_FILE_TYPE;
        return false;
    }

    im_writer* out = im_open_file_writer(filename,err);
    if( !out) {
        return false;
    }

    if( h->write_bundle) {
        result = h->write_bundle(bundle,out,err);
    } else if (h->write_img) {
        // Just write first frame
        SlotID id = {0};
        im_img* img = im_bundle_get(bundle,id);
        if( img) { 
            result = h->write_img(img,out,err);
        } else{
            *err = ERR_BADPARAM;    // bundle has no frame 0
            result = false;
        }
    }

    if (im_close_writer(out)) {
        return false;
    }
    return result;
}


// load colours into palette
bool im_img_pal_write( im_img* img, int first_colour, int num_colours, ImPalFmt data_fmt, const void* data)
{
    if (first_colour+num_colours > im_img_pal_num_colours(img)) {
        return false;
    }

    copy_pal_range(
        data_fmt, data,
        im_img_pal_fmt(img), im_img_pal_data(img),
        first_colour, num_colours);
    return true;
}


// read colours from palette
bool im_img_pal_read( im_img* img, int first_colour, int num_colours, ImPalFmt dest_fmt, void* dest)
{
    if (first_colour+num_colours > im_img_pal_num_colours(img)) {
        return false;
    }

    copy_pal_range(
        im_img_pal_fmt(img), im_img_pal_data(img),
        dest_fmt, dest,
        first_colour, num_colours);
    return true;
}


// helper func - copy/convert palette colours
static void copy_pal_range( ImPalFmt src_fmt, const uint8_t* src, ImPalFmt dest_fmt, uint8_t *dest, int first_colour, int num_colours) {

    if (src_fmt==PALFMT_RGB && dest_fmt==PALFMT_RGB) {
        memcpy(dest + (first_colour*3), src, num_colours*3);
    } else if (src_fmt==PALFMT_RGBA && dest_fmt==PALFMT_RGBA) {
        memcpy(dest + (first_colour*4), src, num_colours*4);
    } else if (src_fmt == PALFMT_RGB && dest_fmt==PALFMT_RGBA) {
        int i;
        dest += 4*first_colour;
        for (i=0; i<num_colours; ++i) {
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = 255;
        }
    } else if (src_fmt == PALFMT_RGBA && dest_fmt==PALFMT_RGB) {
        int i;
        dest += 3*first_colour;
        for (i=0; i<num_colours; ++i) {
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = *src++;
            ++src;
        }
    }
}
