#include "im.h"
#include "private.h"

#include <string.h>

// mask values
#define mskNone	0
#define mskHasMask	1
#define mskHasTransparentColor	2
#define mskLasso	3

// compression values
#define cmpNone	0
#define cmpByteRun1	1

typedef struct {
	uint16_t w, h;	/* raster width & height in pixels	*/
    int16_t  x, y;	/* pixel position for this image	*/
	uint8_t nPlanes;	/* # source bitplanes	*/
	uint8_t masking;
	uint8_t compression;
	uint8_t pad1;	/* unused; for consistency, put 0 here	*/
	uint16_t transparentColor;	/* transparent "color number" (sort of)	*/
	uint8_t xAspect, yAspect;	/* pixel aspect, a ratio width : height	*/
	int16_t  pageWidth, pageHeight;	/* source "page" size in pixels	*/
	} BitMapHeader;


typedef struct context context;

typedef struct context {
    char kind[4];       // "ILBM", "PBM ", "ANIM" (or "root")
    context* parent;
    int level;
    context** children;
    int length;
    int capacity;

    // ilbm/pbm fields
    bool got_bmhd;
    BitMapHeader bmhd;
    uint8_t* image_data;
    uint8_t* mask_data;
    uint8_t* cmap_data;
    int cmap_len;
} context;


static context* ctx_new(context* parent, const char* form_kind) {
    context* ctx = imalloc(sizeof(context));
    memset(ctx,0,sizeof(context));

    ctx->parent = parent;
    memcpy(ctx->kind, form_kind, 4);

    if (parent) {
        ctx->level = parent->level+1;
        if (parent->length == parent->capacity) {
            // need space to add new child
            const int initial_cap = 8;
            int newcap = parent->children ? parent->capacity*2 : initial_cap;
            context** newdata = imalloc( sizeof(context*) * newcap);
            if (!newdata) {
                ifree(ctx);
                return NULL;
            }
            if (parent->children) {
                memcpy(newdata, parent->children, sizeof(context*)*parent->length);
                ifree(parent->children);
            }
            parent->children = newdata;
            parent->capacity = newcap;
        }

        parent->children[parent->length] = ctx;
        ++parent->length;
    }

    return ctx;
}

static context* ctx_free(context* ctx)
{
    int i;
    if( ctx->children) {
        // free children first
        for( i=0;i<ctx->length; ++i) {
            ctx_free( ctx->children[i] );
        }
        ifree(ctx->children);
    }
    if (ctx->image_data) {
        ifree(ctx->image_data);
    }
    if (ctx->mask_data) {
        ifree(ctx->mask_data);
    }
    if( ctx->cmap_data) {
        ifree(ctx->cmap_data);
    }

    ifree(ctx);
}


static inline uint32_t padlen( uint32_t n) {
    return (n&1) ? n+1 : n;
}
static inline bool chkcc( const uint8_t* a, const uint8_t* b) {
    return (a[0]==b[0]) && (a[1]==b[1]) && (a[2]==b[2]) && (a[3]==b[3]);
}

static bool is_iff(const uint8_t* buf, int nbytes);
static im_bundle* read_iff_bundle( im_reader* rdr, ImErr* err );
static bool handle_FORM( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err);
static bool handle_BMHD( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err);
static bool handle_CMAP( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err);
static bool handle_BODY( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err);
static bool handle_ANHD( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err);
static bool handle_DLTA( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err);

static bool decodeLine( im_reader* rdr, uint8_t* dest, int nbytes);


struct handler handle_iff = {is_iff, NULL, read_iff_bundle, NULL, NULL, NULL};

static int parse_chunk( context* ctx, im_reader* rdr, ImErr* err );


static bool is_iff(const uint8_t* buf, int nbytes)
{
    if (!chkcc(buf,"FORM") ) {
        return false;
    }
    if (chkcc(buf+8,"ILBM")) {
        return true;
    }
    if (chkcc(buf+8,"PBM ")) {
        return true;
    }
    if (chkcc(buf+8,"ANIM")) {
        return true;
    }
    return false;
}


static bool handle_FORM( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err)
{
    uint8_t kind[4];
    context* child = NULL;
    int remaining = (int)chunklen;

    if (remaining<4) {
        *err = ERR_MALFORMED;
        return false;
    }

    if (im_read(rdr, kind,4) != 4) {
        *err = ERR_MALFORMED;   // TODO: distingush between read error and eof
        return false;
    }
    remaining -= 4;


    if( chkcc(kind,"ILBM") ) {
        printf("%d:->ILBM\n", ctx->level);
    } else if( chkcc(kind,"PBM ") ) {
        printf("%d:->PBM\n", ctx->level);
    } else if( chkcc(kind,"ANIM") ) {
        printf("%d:->ANIM\n", ctx->level );
    } else {
        // unsupported FORM type
        *err = ERR_MALFORMED;
        return false;
    }

    child = ctx_new(ctx, kind);
    if (!child) {
        *err = ERR_NOMEM;
        return false;
    }

    while (remaining>0) {
        int childchunklen = parse_chunk(child,rdr,err);
        if (childchunklen < 0) {
            // child already owned by ctx, so don't need to clean it up here
            return false;
        }
        remaining -= childchunklen;
    }
    return true;
}



static bool handle_BMHD(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    BitMapHeader* bmhd = &ctx->bmhd;

    if (chunklen != 20) {
        *err = ERR_MALFORMED;
        return false;
    }
    bmhd->w = im_read_u16be(rdr);
    bmhd->h = im_read_u16be(rdr);
    bmhd->x = im_read_s16be(rdr);
    bmhd->y = im_read_s16be(rdr);
    bmhd->nPlanes = im_read_u8(rdr);
    bmhd->masking = im_read_u8(rdr);
    bmhd->compression = im_read_u8(rdr);
    bmhd->pad1 = im_read_u8(rdr);
    bmhd->transparentColor = im_read_u16be(rdr);
    bmhd->xAspect = im_read_u8(rdr);
    bmhd->yAspect = im_read_u8(rdr);
    bmhd->pageWidth = im_read_s16be(rdr);
    bmhd->pageHeight = im_read_s16be(rdr);

    if(im_error(rdr)) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }

    ctx->got_bmhd = true;
    return true;
}


static bool handle_CMAP(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    uint8_t* data;

/*    if( !ctx->got_bmhd) {
        // cmap without bmhd
        *err = ERR_MALFORMED;
    }
    */
    // for now, just stash the chunk verbatim
    data = imalloc(chunklen);
    if (!data) {
        *err = ERR_NOMEM;
        return false;
    }
    if (im_read(rdr,data,chunklen)!=chunklen) {
        ifree(data);
        *err = ERR_FILE;
        return false;
    }

    if( ctx->cmap_data) {
        ifree(ctx->cmap_data);
    }

    ctx->cmap_data = data;
    ctx->cmap_len = chunklen;
    return true;
}

static bool handle_BODY(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    int words_per_line, bytes_per_line, data_size;
    BitMapHeader* bmhd = &ctx->bmhd;

    // we'll read in and uncompress any data, but for now
    // we'll leave it in planar form, to make it easier to
    // decode DLTA chunks.

    if (!ctx->got_bmhd) {
        *err = ERR_MALFORMED;   // got BODY before BMHD
        return false;
    }

    switch (bmhd->compression) {
        case cmpByteRun1: break;
        case cmpNone: break;
        default:
            *err = ERR_UNSUPPORTED;
            return false;
    }

    if (ctx->image_data) {
        // already had a BODY chunk?
        *err = ERR_MALFORMED;
        return false;
    }

    if (bmhd->nPlanes >32) {
        *err = ERR_MALFORMED;
        return false;
    }

    // actual image data must be multiple of 16 pixels wide
    words_per_line = (bmhd->w+15)/16;
    bytes_per_line = bmhd->nPlanes * (words_per_line*2);
    data_size = bmhd->h * bytes_per_line;
    ctx->image_data = imalloc(data_size);
    printf("alloc %d bytes\n", data_size);
    if( bmhd->compression == cmpNone) {

        int n = im_read(rdr,ctx->image_data, data_size);
        if (n != data_size) {
            // don't need to free image data - context will handle it
            *err = ERR_MALFORMED;
            return false;
        }
    } else if (bmhd->compression == cmpByteRun1) {
        printf("decompress\n");
        uint8_t* dest = ctx->image_data;
        int y;
        // the compression breaks at the end of each line
        // but can run across planes.
        // TODO: I think the mask is included in the run
        for (y=0; y<bmhd->h; ++y) {
            printf("decode %d\n",y);
            if (!decodeLine(rdr,dest,bytes_per_line)) {
                *err = ERR_MALFORMED;
                return false;
            }
            dest += bytes_per_line;
        }
    }

    return true;
}


// decode a line of ILBM BODY compression
static bool decodeLine( im_reader* rdr, uint8_t* dest, int nbytes)
{
    while (nbytes>0) {
        uint8_t c = im_read_u8(rdr);
        if(im_error(rdr)) {
            return false;
        }
        if( c==128) {
            break;
        }
        if (c>128) {
            int i;
            int cnt = (257-c);
            uint8_t v;
            if (nbytes<cnt) {
                return false;   // overrun
            }
            v = im_read_u8(rdr);
            if(im_error(rdr)) {
                return false;
            }
            for (i=0; i<cnt; ++i) {
                *dest++ = v;
            }
            nbytes -= cnt;
        } else {
            int cnt = c+1;
            if (nbytes<cnt) {
                return false;   // overrun
            }
            if (im_read( rdr, dest, cnt) != cnt) {
                return false;
            }
            dest += cnt;
            nbytes -= cnt;
        }
    }
//    printf("%d bytes left over\n",nbytes);
    return true;
}



static bool handle_ANHD( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err)
{
    if(im_seek(rdr,chunklen,SEEK_CUR)!=0) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}

static bool handle_DLTA( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err)
{
    if(im_seek(rdr,chunklen,SEEK_CUR)!=0) {
        *err = ERR_FILE;
        return false;
    }
    return true;
}

// process a single chunk and all it's children, returns num of bytes consumed (excluding any pad byte)
static int parse_chunk( context* ctx, im_reader* rdr, ImErr* err ) {
    uint8_t buf[8];
    bool success;
    if(im_read(rdr, buf,8) != 8) {
        *err = ERR_MALFORMED;   // TODO: distingush between read error and eof
        return -1;
    }
    uint32_t chunklen = ((uint32_t)buf[4])<<24 |
        ((uint32_t)buf[5])<<16 |
        ((uint32_t)buf[6])<<8 |
        ((uint32_t)buf[7]);

    printf("%d:%c%c%c%c (%d bytes)\n", ctx->level, buf[0], buf[1], buf[2], buf[3], chunklen );

    if (chkcc(buf,"FORM")) {
        printf("%d: enter FORM\n", ctx->level);
        // FORM chunks have children
        success = handle_FORM(ctx,rdr,chunklen, err);
        printf("%d: exit FORM (%d)\n", ctx->level, success?1:0);
    } else if (chkcc(buf,"BMHD")) {
        success = handle_BMHD(ctx,rdr,chunklen, err);
    } else if (chkcc(buf,"CMAP")) {
        success = handle_CMAP(ctx,rdr,chunklen, err);
    } else if (chkcc(buf,"BODY")) {
        success = handle_BODY(ctx,rdr,chunklen, err);
    } else if (chkcc(buf,"ANHD")) {
        success = handle_ANHD(ctx,rdr,chunklen, err);
    } else if (chkcc(buf,"DLTA")) {
        success = handle_DLTA(ctx,rdr,chunklen, err);
    } else {
        // unknown/unhandled chunk type. skip it.
        if (im_seek(rdr, chunklen, SEEK_CUR)!=0) {
            *err = ERR_FILE;
            success = false;
        } else {
            success = true;
        }
    }

    if (!success) {
        return -1;
    }

    if (chunklen&1) {
        // read the pad byte
        if (im_seek(rdr, 1, SEEK_CUR)!=0) {
            *err = ERR_MALFORMED;
            return -1;
        }
    }

    // +8 to account for chunk header
    return (int)8+chunklen;
}


static im_bundle* read_iff_bundle( im_reader* rdr, ImErr* err )
{
    // TODO: could do away with root context perhaps...
    context* root = ctx_new(NULL,"root");
    if( !root) {
        *err = ERR_NOMEM;
        goto bailout;
    }
    if( parse_chunk(root,rdr,err)<0 ) {
        goto bailout;
    }

    ctx_free(root);

    *err = (ImErr)69;
    return NULL;

bailout:
    ctx_free(root);
    return NULL;
}


