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
    context* parent;
    int level;
    context** children;
    int length;
    int capacity;

    // ilbm/pbm fields
    bool got_bmhd;
    BitMapHeader bmhd;
    uint8_t* planes[8];
    uint8_t* mask;
    uint8_t* cmap_data;
    int cmap_len;
} context;


static context* ctx_new(context* parent) {
    context* ctx = imalloc(sizeof(context));
    memset(ctx,0,sizeof(context));

    ctx->parent = parent;

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
    for( i=0;i<8;++i) {
        if (ctx->planes[i]) {
            ifree(ctx->planes[i]);
        }
    }
    if (ctx->mask) {
        ifree(ctx->mask);
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

    child = ctx_new(ctx);
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
    BitMapHeader* bmhd = &ctx->bmhd;
    if (im_seek(rdr,chunklen,SEEK_CUR)!=0) {
        *err = ERR_FILE;
        return false;
    }
    if (!ctx->got_bmhd) {
        *err = ERR_MALFORMED;   // got BODY before BMHD
        return false;
    }

    if( bmhd->compression == cmpByteRun1) {
        *err = ERR_UNSUPPORTED;
        return false;
    }

 
    if( bmhd->compression == cmpNone) {
    }


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
    context* root = ctx_new(NULL);
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


