#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>



// format reference:
// https://en.wikipedia.org/wiki/PCX#References
// http://bespin.org/~qz/pc-gpe/pcx.txt
//
// pcx code from gimp:
// https://git.gnome.org/browse/gimp/tree/plug-ins/common/file-pcx.c

static bool is_pcx(const uint8_t* buf, int nbytes)
{
    const uint8_t magic = buf[0], v=buf[1], enc=buf[2], bpp=buf[3];
    assert(nbytes>=4);
    if (magic != 0x0a) {
        return false;
    }
    if (!(v==0 || v==2 || v==3 || v==4 || v==5)) {
        return false;
    }
    if (enc !=0 && enc !=1) {
        return false;
    }
    if( bpp!=1 && bpp!=2 && bpp!=4 && bpp!=8 && bpp!=24) {
        return false;
    }
    return true;
}

#if 0
// returns true if filename extenstion is ".pcx" (case-insensitive)
static bool match_pcx_ext(const char* file_ext)
{
    return (istricmp(file_ext,".pcx")==0);
}
#endif

typedef struct header {
    int version, enc, depth, w, h, xmin, xmax, ymin, ymax, planes, xdpi, ydpi;
    size_t bytesperline;
    int paltype;
    uint8_t *scanbuf;
} header;

static bool read_header( header* pcx, im_in* in, ImErr *err);
static void decode_scanline( header* pcx, im_in* in);


im_img* iread_pcx_image( im_in* in, ImErr* err )
{
    im_img* img = NULL;
    header pcx = {0};
    *err = ERR_NONE;

    if (!read_header( &pcx, in, err)) {
        goto cleanup;
    }

    // TODO: handle depth<8

    if (pcx.depth==8 && pcx.planes==1) {
        int y;
        uint8_t cmap[1 + (256*3)] = {0};

        img = im_img_new( pcx.w, pcx.h, 1, IM_FMT_INDEX8);
        if (!img) {
            *err = ERR_NOMEM;
            goto cleanup;
        }

        for (y=0; y<pcx.h; ++y) {
            decode_scanline(&pcx, in);
            memcpy( im_img_row(img,y), pcx.scanbuf, pcx.w);
        }

        // need to seek back from end of file to get to palette. ugh.
        // some files have dodgy RLE, so you can't always tell where the image data ends.
        im_in_seek(in, -(1+(256*3)), IM_SEEK_END);
        if (im_in_read(in, cmap, 1+(256*3)) != 1+(256*3)) {
            *err = ERR_MALFORMED;
            goto cleanup;
        }
        // cmap is preceeded by marker byte 0x0c;
        if( cmap[0] != 0x0c ) {
            *err = ERR_MALFORMED;
            goto cleanup;
        }
        im_img_pal_set(img, IM_FMT_RGB, 256, cmap+1);

    } else if (pcx.depth==8 && pcx.planes==3) {
        int y;

        img = im_img_new( pcx.w, pcx.h, 1, IM_FMT_RGB);
        if (!img) {
            *err = ERR_NOMEM;
            goto cleanup;
        }

        for (y=0; y<pcx.h; ++y) {
            uint8_t* r = pcx.scanbuf;
            uint8_t* g = pcx.scanbuf + pcx.bytesperline;
            uint8_t* b = pcx.scanbuf + (pcx.bytesperline*2);
            uint8_t* dest = im_img_row(img,y);
            int x;
            decode_scanline(&pcx, in);
            for(x=0;x<pcx.w; ++x) {
                *dest++ = *r++;
                *dest++ = *g++;
                *dest++ = *b++;
            }
        }
    } else {
        *err = ERR_UNSUPPORTED;
        goto cleanup;
    }


cleanup:

    if (pcx.scanbuf) {
        ifree(pcx.scanbuf);
    }

    if( img && *err != ERR_NONE) {
        im_img_free(img);
        img = NULL;
    }

    return img;


}




static bool read_header( header* pcx, im_in* in, ImErr *err)
{
    uint8_t buf[128];
    uint8_t* p;

    if (im_in_read( in, buf, 128) != 128) {
        *err = im_in_eof(in) ? ERR_MALFORMED:ERR_FILE;
        return false;
    }

    p=buf;
    ++p;    // 0x0a magic cookie
    pcx->version = (int)*p++;
    pcx->enc = (int)*p++;
    pcx->depth = (int)*p++;
    pcx->xmin = decode_u16le(&p);
    pcx->ymin = decode_u16le(&p);
    pcx->xmax = decode_u16le(&p);
    pcx->ymax = decode_u16le(&p);
    pcx->w = (pcx->xmax-pcx->xmin)+1;
    pcx->h = (pcx->ymax-pcx->ymin)+1;
    pcx->xdpi = decode_u16le(&p); // xdpi or width
    pcx->ydpi = decode_u16le(&p); // ydpi or height
    p += 48;     // 16-colour palette
    ++p;        // reserved
    pcx->planes = (int)*p++;
    pcx->bytesperline = (size_t)decode_u16le(&p);
    pcx->paltype = (int)decode_u16le(&p);

    /*printf("min: %d,%d max: %d,%d (%dx%d) version: %d enc: %d depth: %d planes: %d xdpi: %d ydpi: %d bytesperline: %d paltype: %d\n",
            pcx->xmin,pcx->ymin, pcx->xmax,pcx->ymax, pcx->w,pcx->h,pcx->version, pcx->enc, pcx->depth, pcx->planes, pcx->xdpi, pcx->ydpi, pcx->bytesperline, pcx->paltype);
    */

    // some sanity checks
    if (!is_pcx(buf,128) ) {
        *err = ERR_MALFORMED;
        return false;
    }
    if( pcx->planes < 1 || pcx->planes > 4) {
        *err = ERR_MALFORMED;
        return false;
    }

    pcx->scanbuf = imalloc(pcx->bytesperline * pcx->planes);
    if (!pcx->scanbuf) {
        *err = ERR_NOMEM;
    }

    return true;
}


// decode a line (can be multiple planes, as rle can span planes)
// we're pretty tolerant of dodgy data.
static void decode_scanline( header* pcx, im_in* in)
{
    int outcnt = (pcx->bytesperline * pcx->planes);
    uint8_t val,reps;
    uint8_t* dest = pcx->scanbuf;

    reps = 0;
    while (outcnt>0) {
        if (reps==0) {
            if (im_in_read(in,&val,1) != 1) {
                //printf("ERROR: BADBYTE\n");
                /**err = im_eof(in)? ERR_MALFORMED:ERR_FILE;
                return false;
                */
                val=0;
            }
            if (val >= 0xc0) {
                reps = (val - 0xc0);
                if (im_in_read(in,&val,1) != 1) {
                    //*err = im_eof(in)? ERR_MALFORMED:ERR_FILE;
                    //printf("ERROR: BADREP\n");
                    val=0;
                }
                //printf("0x%02x x%d (%d left)\n",val, reps, outcnt);
            } else {
                reps = 1;
                //printf("0x%02x (%d left)\n",val, outcnt);
            }
        }
        *dest++ = val;
        --reps;
        --outcnt;
    }
    //if( reps>0) {
    //    printf("WARN: leftover %d reps\n",(int)reps);
    //}
}



