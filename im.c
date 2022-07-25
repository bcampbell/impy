#include "impy.h"
#include "private.h"

#include <stdlib.h>
#include <string.h> // for memcmp
#include <stdio.h>

void* imalloc(size_t size)
{
    return malloc(size);
}

void* irealloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void ifree(void *ptr)
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

ImFiletype im_filetype_from_filename(const char* filename)
{
    const char* ext = ext_part(filename);
    if (istricmp(ext, ".png") == 0) {
        return IM_FILETYPE_PNG;
    }
    if (istricmp(ext, ".gif") == 0) {
        return IM_FILETYPE_GIF;
    }
    if (istricmp(ext, ".bmp") == 0) {
        return IM_FILETYPE_BMP;
    }
    if (istricmp(ext, ".jpeg") ==0 ) {
        return IM_FILETYPE_JPEG;
    }
    if (istricmp(ext, ".jpg") ==0 ) {
        return IM_FILETYPE_JPEG;
    }
    if (istricmp(ext, ".pcx") ==0 ) {
        return IM_FILETYPE_PCX;
    }
    if (istricmp(ext, ".tga") ==0 ) {
        return IM_FILETYPE_TARGA;
    }
    if (istricmp(ext, ".iff") ==0 ) {
        return IM_FILETYPE_ILBM;
    }
    if (istricmp(ext, ".lbm") ==0 ) {
        return IM_FILETYPE_ILBM;
    }
    if (istricmp(ext, ".ilbm") ==0 ) {
        return IM_FILETYPE_ILBM;
    }
    if (istricmp(ext, ".pbm") ==0 ) {
        return IM_FILETYPE_ILBM;
    }
    return IM_FILETYPE_UNKNOWN;
}


