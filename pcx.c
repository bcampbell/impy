#include "impxy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>



// format reference:
// https://en.wikipedia.org/wiki/PCX#References
// http://bespin.org/~qz/pc-gpe/pcx.txt


static bool is_pcx(const uint8_t* buf, int nbytes);
static bool match_pcx_ext(const char* file_ext);
static im_img* read_pcx_image( im_reader* rdr, ImErr* err );
static bool write_pcx_img(im_img* img, im_writer* out, ImErr* err);

struct handler handle_pcx = {is_pcx, read_pcx_image, NULL, match_pcx_ext, NULL, NULL};




//
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

// returns true if filename extenstion is ".pcx" (case-insensitive)
static bool match_pcx_ext(const char* file_ext)
{
    return (istricmp(file_ext,".pcx")==0);
}


typedef struct header {
    int version, enc, depth, w, h, xmin, xmax, ymin, ymax, planes, xdpi, ydpi;
    size_t bytesperline;
    int paltype;
    uint8_t *scanbuf;
} header;

static bool read_header( header* pcx, im_reader* rdr, ImErr *err);
static bool decode_scanline( header* pcx, im_reader* rdr, ImErr *err);
static bool read_palette( header* pcx, im_reader* rdr, im_img* img, ImErr *err);


static im_img* read_pcx_image( im_reader* rdr, ImErr* err )
{
    im_img* img;
    header pcx = {0};
    *err = ERR_NONE;

    if (!read_header( &pcx, rdr, err)) {
        goto cleanup;
    }

    img = im_img_new( pcx.w, pcx.h, 1, FMT_COLOUR_INDEX, DT_U8);
    if (!img) {
        *err = ERR_NOMEM;
        goto cleanup;
    }


    int y;
    for (y=0; y<pcx.h; ++y) {
        printf("Decode line %d\n",y);
        if (!decode_scanline(&pcx, rdr, err)) {
            //goto cleanup;
        }
        // fudge for indexed
        {
            int x;
            uint8_t* dest = im_img_row(img,y);
            for( x=0;x<pcx.w; ++x) {
                *dest++ = pcx.scanbuf[x];
            }
        }
    }

    if( !read_palette(&pcx,rdr,img,err) ) {
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




static bool read_header( header* pcx, im_reader* rdr, ImErr *err)
{
    uint8_t buf[128];
    uint8_t* p;

    if (im_read( rdr, buf, 128) != 128) {
        *err = im_eof(rdr) ? ERR_MALFORMED:ERR_FILE;
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

    printf("min: %d,%d max: %d,%d (%dx%d) version: %d enc: %d depth: %d planes: %d xdpi: %d ydpi: %d bytesperline: %d paltype: %d\n",
            pcx->xmin,pcx->ymin, pcx->xmax,pcx->ymax, pcx->w,pcx->h,pcx->version, pcx->enc, pcx->depth, pcx->planes, pcx->xdpi, pcx->ydpi, pcx->bytesperline, pcx->paltype);


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
static bool decode_scanline( header* pcx, im_reader* rdr, ImErr *err)
{
    int expect = pcx->bytesperline * pcx->planes;
    int got=0;  // num bytes emitted so far
    uint8_t n,v;
    uint8_t* dest = pcx->scanbuf;

    printf("EXPECT %d\n",expect);
    while (got<expect) {
        if (im_read(rdr,&n,1) != 1) {
            printf("ERROR: BADBYTE\n");
            *err = im_eof(rdr)? ERR_MALFORMED:ERR_FILE;
            return false;
        }
        if ((n & 0xc0) == 0xc0) {
            // repeat next byte
            int reps = (int)(n & 0x3f);
            if (im_read(rdr,&v,1) != 1) {
                *err = im_eof(rdr)? ERR_MALFORMED:ERR_FILE;
                printf("ERROR: BADREP\n");
                return false;
            }
            printf( "%d-%d ", got, got+reps);
            printf("0x%02x\n", (int)v);
            got += reps;
            if(got>expect) {
                *err = ERR_MALFORMED;
                printf("ERROR: overflow\n");
                return false;
            }
            while(reps>0) {
                *dest++ = v;
                --reps;
            }
        } else {
            printf( "%d: 0x%02x\n", got, (int)n);
            *dest++ = n;
            ++got;
        }
    }
    printf("decode done\n");
    return true;
}


static bool read_palette( header* pcx, im_reader* rdr, im_img* img, ImErr *err)
{
    uint8_t buf[32];
    int y;
    int i;

    if (im_read(rdr, &buf,16) != 16) {
        printf("semprini.\n");
    } else {
        printf("palette dump:\n");
        for (i=0; i<32;++i) {
            printf("%02x ",(int)buf[i]);
        }
        printf("\n");
    }


    im_img_pal_set(img, PALFMT_RGB, 256, NULL);
    uint8_t* p = im_img_pal_data(img);
    for(y=0; y<=255; ++y) {
        *p++ = y;
        *p++ = y;
        *p++ = y;
    }
    return true;
}


