#include "impy.h"
#include "private.h"
#include "bmp.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>

static struct handler handle_bmp = {
    im_is_bmp,
    im_img_read_bmp,
    NULL,
    im_ext_match_bmp,
    im_img_write_bmp,
    NULL };


static struct handler *handlers[] = { &handle_png, &handle_gif, &handle_iff, &handle_bmp, &handle_pcx, &handle_jpeg, NULL };


static struct handler* pick_handler_for_read(im_reader* rdr);

void* imalloc( size_t size)
{
    return malloc(size);
}

void ifree(void* ptr)
{
    free(ptr);
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
    } else {
        *err = ERR_UNSUPPORTED;
    }

    if (im_close_writer(out)) {
        return false;
    }
    return result;
}

