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


// we use a context struct for each FORM we encounter...
// (and a dummy "root" one to act as a sort of catchall)
// The can be nested (eg ANIM, containing multiple ILBMs).
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

static int decodeLine( im_reader* rdr, uint8_t* dest, int nbytes);
static im_img* ctx_ilbm_to_img(context* ctx, ImErr* err) ;
static bool ctx_collect_bundle( context* ctx, im_bundle* out, ImErr* err);

static const char* indent( int n );
int dump_chunk( int nest, im_reader *rdr );

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

    printf("%sformtype=%c%c%c%c\n", indent(ctx->level+1), kind[0], kind[1], kind[2], kind[3]);

    if( chkcc(kind,"ILBM") ) {
    } else if( chkcc(kind,"PBM ") ) {
    } else if( chkcc(kind,"ANIM") ) {
    } else {
        // unsupported FORM type (eg 8SVX sound)- skip it
        printf("%s->skip\n", indent(ctx->level+1));
        if(im_seek(rdr,remaining, SEEK_CUR) != 0 ) {
            *err = ERR_FILE;
            return false;
        }
        return true;
    }

    child = ctx_new(ctx, kind);
    if (!child) {
        *err = ERR_NOMEM;
        return false;
    }

    while (remaining>0) {
        int n = parse_chunk(child,rdr,err);
        if (n < 0) {
            // child already owned by ctx, so don't need to clean it up here
            return false;
        }
        remaining -= n;
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


// TODO: FIX! Not reading right num of bytes!
static bool handle_BODY(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    int consumed = 0;
    int words_per_line, bytes_per_line, data_size;
    BitMapHeader* bmhd = &ctx->bmhd;

    // we'll read in and uncompress any data, but for now
    // we'll leave it in planar form, to make it easier to
    // decode DLTA chunks.

    if (!ctx->got_bmhd) {
        *err = ERR_MALFORMED;   // got BODY before BMHD
        return false;
    }

    if (ctx->bmhd.masking !=0) {
        printf("MASKING=%d\n",ctx->bmhd.masking );
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
    if( bmhd->compression == cmpNone) {

        int n = im_read(rdr,ctx->image_data, data_size);
        if (n != data_size) {
            // don't need to free image data - context will handle it
            *err = ERR_MALFORMED;
            return false;
        }
        consumed += data_size;
    } else if (bmhd->compression == cmpByteRun1) {
        uint8_t* dest = ctx->image_data;
        int y;
        // the compression breaks at the end of each line
        // but can run across planes.
        // TODO: I think the mask is included in the run
        for (y=0; y<bmhd->h; ++y) {
            int n = decodeLine(rdr,dest,bytes_per_line);
            if (n<0) {
                *err = ERR_MALFORMED;
                return false;
            }
            consumed += n;
            dest += bytes_per_line;
        }
    }


    if( consumed<chunklen) {
        printf("%s*** warn: decode left %d bytes\n", indent(ctx->level), chunklen-consumed);
        if (im_seek(rdr,chunklen-consumed, SEEK_CUR) != 0 ) {
            return false;
        }
    }



    return true;
}


// decode a line of ILBM BODY compression
static int decodeLine( im_reader* rdr, uint8_t* dest, int nbytes)
{
    int consumed = 0;
    while (nbytes>0) {
        uint8_t c = im_read_u8(rdr);
        if(im_error(rdr)) {
            return -1;
        }
        consumed++;
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
            consumed++;
            for (i=0; i<cnt; ++i) {
                *dest++ = v;
            }
            nbytes -= cnt;
        } else {
            int cnt = c+1;
            if (nbytes<cnt) {
                return -1;   // overrun
            }
            if (im_read( rdr, dest, cnt) != cnt) {
                return -1;
            }
            consumed += cnt;
            dest += cnt;
            nbytes -= cnt;
        }
    }
//    printf("%d bytes left empty\n",nbytes);
    return consumed;
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
    uint32_t chunklen;
    bool success;
    int consumed = 0;
    if(im_read(rdr, buf,8) != 8) {
            printf("POO1\n");
        *err = ERR_MALFORMED;   // TODO: distingush between read error and eof
        return -1;
    }
    consumed += 8;
    chunklen = ((uint32_t)buf[4])<<24 |
        ((uint32_t)buf[5])<<16 |
        ((uint32_t)buf[6])<<8 |
        ((uint32_t)buf[7]);

    printf("%s%c%c%c%c (%d bytes)\n", indent(ctx->level), buf[0], buf[1], buf[2], buf[3], chunklen );
    if (chkcc(buf,"FORM")) {
//        printf("%d: enter FORM\n", ctx->level);
        // FORM chunks have children
        success = handle_FORM(ctx,rdr,chunklen, err);
//        printf("%d: exit FORM (%d)\n", ctx->level, success?1:0);
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
            printf("POO2\n");
        return -1;
    }

    consumed += chunklen;

    if (chunklen&1) {
        // read the pad byte
        if (im_seek(rdr, 1, SEEK_CUR)!=0) {
            *err = ERR_MALFORMED;
            printf("POO3\n");
            return -1;
        }
        consumed += 1;
    }

    // +8 to account for chunk header
    return (int)consumed;
}


static im_bundle* read_iff_bundle( im_reader* rdr, ImErr* err )
{
    /*
    dump_chunk(0,rdr);
    *err = (ImErr)69;
    return false;
    */


    im_bundle* bundle = NULL;

    // TODO: could do away with root context perhaps...
    context* root = ctx_new(NULL,"root");
    if( !root) {
        *err = ERR_NOMEM;
        goto bailout;
    }
    if( parse_chunk(root,rdr,err)<0 ) {
        goto bailout;
    }



    bundle = im_bundle_new();
    if(!bundle)
    {
        *err = ERR_NOMEM;
        goto bailout;
    }

    if(!ctx_collect_bundle(root, bundle, err)) {
        goto bailout;
    }

    ctx_free(root);
    return bundle;

bailout:
    if( root) {
        ctx_free(root);
    }
    if( bundle) {
        im_bundle_free(bundle);
    }

    return NULL;
}


static bool ctx_collect_bundle( context* ctx, im_bundle* out, ImErr* err)
{
    int i;
    im_img* img = NULL;
printf("collect (level %d)\n", ctx->level);
    if (chkcc(ctx->kind,"ILBM") ) {
        img = ctx_ilbm_to_img(ctx, err);
        if (img) {
            SlotID id = {0};
            id.frame = im_bundle_num_frames(out)-1;
            printf("add frame %d\n", id.frame);
            if (!im_bundle_set(out,id,img)) {
                *err = ERR_NOMEM;
                return false;
            }
            return true;    // BAIL OUT AFTER 1ST IMAGE
        } else {
            printf("img fail\n");
            return false;
        }
    }


    for( i=0; i<ctx->length; ++i) {
        if (!ctx_collect_bundle(ctx->children[i], out, err)) {
            return false;
        }
    }

    return true;
}

static im_img* ctx_ilbm_to_img(context* ctx, ImErr* err)
{
    int y;
    int plane_pitch;
    BitMapHeader* bmhd = &ctx->bmhd;
    if( !ctx->got_bmhd || !ctx->image_data) {
        *err = ERR_MALFORMED;
        return NULL;
    }

    // TODO:
    if( bmhd->nPlanes>8) {
        *err = ERR_UNSUPPORTED;
        return NULL;
    }

    // TODO: handle 24 and 32-plane images (FMT_RGB and FMT_RGBA)
    im_img* img = im_img_new(bmhd->w, bmhd->h, 1, FMT_COLOUR_INDEX, DT_U8);
    if( !img) {
        *err = ERR_NOMEM;
        return NULL;
    }

    plane_pitch = 2*((bmhd->w+15)/16);
    // convert planar data to 8bit indexed
    // TODO: handle mask plane!
    for (y=0; y<bmhd->h; ++y) {
        uint8_t* dest = im_img_row(img,y);
        int x;
        for( x=0; x<bmhd->w; ++x) {
            uint8_t pix=0;
            const uint8_t* src = ctx->image_data + (y*plane_pitch*bmhd->nPlanes) + x/8;
            int srcbit = 7 - (x&7);
            int destbit;
            for(destbit = 0; destbit<bmhd->nPlanes; ++destbit) {
                *dest |= (((*src) >> srcbit)&0x01)<<destbit;
                src += plane_pitch;
            }
            ++dest;
        }
    }

    // install palette
    // TODO: pad out to at least 2^nPlanes entries
    if (ctx->cmap_data) {
        if( !im_img_pal_set(img, PALFMT_RGB, ctx->cmap_len/3, ctx->cmap_data) ) {
            *err = ERR_NOMEM;
            im_img_free(img);
            return NULL;
        }
    }

    return img;
}


#define MAXINDENT 16
static const char* indent( int n ) {
    static char buf[(MAXINDENT*2)+1] = {0};
    int i;
    if(buf[0]==0) {
        // init
        for( i=0; i<(MAXINDENT*2); ++i) {
            buf[i] = ' ';
        }
    }
    if (n>MAXINDENT) {
        n=MAXINDENT;
    }

    return buf + (MAXINDENT-n)*2;
}


int dump_chunk( int nest, im_reader *rdr )
{

    uint8_t kind[4];
    uint32_t len;
    int consumed = 0;
    if( im_read(rdr, kind, 4)!=4) {
        printf("ERR reading fourCC (eof=%d)\n", im_eof(rdr));
        return -1;
    }
    len = im_read_u32be(rdr);
    if (im_error(rdr)) {
        printf("ERR2\n");
        return -1;
    }
    consumed += 8;

    printf("%s%c%c%c%c (%d bytes)\n", indent(nest), kind[0], kind[1], kind[2], kind[3], len);
    if(chkcc(kind,"FORM")) {
        char ft[4];
        int remaining = len;
        if( !im_read(rdr, ft, 4)) {
            printf("ERR reading form type\n");
            return -1;
        }
        printf("%sformtype=%c%c%c%c\n", indent(nest+1), ft[0], ft[1], ft[2], ft[3]);
        consumed += 4;
        remaining -= 4;
        while(remaining>0) {
            int n = dump_chunk(nest+1,rdr);
            if( n<0) {
                printf("ERR dump_failed (%d remaining)\n", remaining);
                return -1;
            }
            remaining -= n;
            consumed += n;
        }
    } else {
        if( im_seek(rdr, len, SEEK_CUR) != 0 ) {
            printf("ERR skipping chunk\n");
            return -1;
        }
        consumed += len;
    }

    // handle even-padding rule
    if( len&1) {
        if( im_seek(rdr, 1, SEEK_CUR) != 0 ) {
            printf("ERR skipping padbyte\n");
            return false;
        }
        printf("%spad\n", indent(nest));
        consumed += 1;
    }
    return consumed;
}



