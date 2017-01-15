#include "im.h"
#include "private.h"

#include <string.h>


// Good collection of anims to test with at:
// http://www.randelshofer.ch/animations/


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

typedef struct {
    uint8_t operation;     /*  The compression method:
                            =0 set directly (normal ILBM BODY),
                            =1 XOR ILBM mode,
                            =2 Long Delta mode,
                            =3 Short Delta mode,
                            =4 Generalized short/long Delta mode,
            			    =5 Byte Vertical Delta mode
			                =7 short/long Vertical Delta mode
                            =74 (ascii 'J') reserved for Eric Graham's
                               compression technique (details to be
                               released later).
                               */
    uint8_t mask;         /* XOR mode only */
    uint16_t w,h;
    int16_t  x,y;
    uint32_t abstime;
    uint32_t reltime;      /* in jiffies */
    uint8_t interleave;
    uint8_t pad0;
    uint32_t bits;
    uint8_t pad[16];
} AnimHeader;



typedef struct frame {
    int num;    // which frame number this one is (ie index in ctx->frames[])
    char kind[4];   // ILBM or PBM
    // ilbm/pbm fields
    bool got_bmhd;
    BitMapHeader bmhd;
    uint8_t* image_data;
    uint8_t* mask_data;
    uint8_t* cmap_data;
    int cmap_len;

    bool got_anhd;
    AnimHeader anhd;

} frame;

typedef struct context {
//    char kind[4];       // "ILBM", "PBM ", "ANIM" (or "root")
    int level;
    frame** frames;
    int nframes;
    int capacity;
    // a general-purpose buffer
    uint8_t* buf;
    size_t bufsize;
} context;


static context* ctx_new() {
    context* ctx = imalloc(sizeof(context));
    memset(ctx,0,sizeof(context));
    return ctx;
}

static bool ctx_size_buffer(context* ctx, size_t newsize) {
    if (newsize < ctx->bufsize) {
        return true;    // big enough already
    }

    if (ctx->buf) {
        ifree(ctx->buf);
    }
    ctx->buf = imalloc(newsize);
    if (!ctx->buf) {
        ctx->bufsize = 0;
        return false;
    }

    ctx->bufsize = newsize;
    return true;
}

static frame* ctx_add_frame(context* ctx, const char* kind) 
{
    frame* f;
    if (ctx->nframes == ctx->capacity) {
        // need space to add new child
        const int initial_cap = 8;
        int newcap = ctx->frames ? ctx->capacity*2 : initial_cap;
        frame** newdata = imalloc( sizeof(frame*) * newcap);
        if (!newdata) {
            return NULL;
        }
        if (ctx->frames) {
            memcpy(newdata, ctx->frames, sizeof(frame*)*ctx->nframes);
            ifree(ctx->frames);
        }
        ctx->frames = newdata;
        ctx->capacity = newcap;
    }

    // create and link in new frame
    f = imalloc(sizeof(frame));
    if (!f) {
        return NULL;
    }
    memset(f,0,sizeof(frame));
    f->num = ctx->nframes;
    memcpy( f->kind, kind, 4);

    ctx->frames[ctx->nframes] = f;
    ++ctx->nframes;

    return f;
}

static void ctx_free(context* ctx)
{
    int i;
    if( ctx->frames) {
        for( i=0; i<ctx->nframes; ++i) {
            frame* f = ctx->frames[i];
            if (f->image_data) {
                ifree(f->image_data);
            }
            if (f->mask_data) {
                ifree(f->mask_data);
            }
            if (f->cmap_data) {
                ifree(f->cmap_data);
            }
            ifree(f);
        }
        ifree(ctx->frames);
    }
    if( ctx->buf) {
        ifree(ctx->buf);
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
static bool decodeANIM5chunk(const uint8_t* src, size_t chunklen, uint8_t* dest,
        int ncols, int nplanes, int height);

static im_img* frame_to_img(context* ctx, int frameidx, ImErr* err);
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

    printf("%sformtype=%c%c%c%c\n", indent(ctx->level), kind[0], kind[1], kind[2], kind[3]);

    if (!chkcc(kind,"ILBM") && !chkcc(kind,"PBM ") && !chkcc(kind,"ANIM")) {
        // unsupported FORM type (eg 8SVX sound)- skip it
        printf("%s->skip\n", indent(ctx->level));
        if(im_seek(rdr,remaining, SEEK_CUR) != 0 ) {
            *err = ERR_FILE;
            return false;
        }
        return true;
    }

    // starting a new image/frame?
    if (chkcc(kind,"ILBM") || chkcc(kind,"PBM ")) {
        frame* f = ctx_add_frame(ctx,kind);
        if (!f) {
            *err = ERR_NOMEM;
            return false;
        }
    }

    while (remaining>0) {
        int n = parse_chunk(ctx,rdr,err);
        if (n < 0) {
            return false;
        }
        remaining -= n;
    }
    return true;
}


static frame* ctx_curframe(context *ctx) {
    if( ctx->frames && ctx->nframes>0) {
        return ctx->frames[ctx->nframes-1];
    } else {
        return NULL;
    }
}

static bool handle_BMHD(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    BitMapHeader* bmhd;
    frame* f = ctx_curframe(ctx);

    if( !f) {
        *err = ERR_MALFORMED;
        return false;
    }
    bmhd = &f->bmhd;

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
    printf("%s %dx%d planes: %d masking: %d compression: 0x%02x\n", indent(ctx->level), bmhd->w, bmhd->h, bmhd->nPlanes, bmhd->masking, bmhd->compression);
    f->got_bmhd = true;
    return true;
}


static bool handle_CMAP(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    uint8_t* data;
    frame* f = ctx_curframe(ctx);

    if( !f) {
        *err = ERR_MALFORMED;
        return false;
    }

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

    if( f->cmap_data) {
        ifree(f->cmap_data);
    }

    f->cmap_data = data;
    f->cmap_len = chunklen;
    return true;
}


// TODO: FIX! Not reading right num of bytes!
static bool handle_BODY(context* ctx, im_reader* rdr, uint32_t chunklen, ImErr *err )
{
    int consumed = 0;
    int words_per_line, bytes_per_line, data_size;
    frame* f = ctx_curframe(ctx);

    if( !f) {
        *err = ERR_MALFORMED;
        return false;
    }

    // we'll read in and uncompress any data, but for now
    // we'll leave it in planar form, to make it easier to
    // decode DLTA chunks.

    if (!f->got_bmhd) {
        *err = ERR_MALFORMED;   // got BODY before BMHD
        return false;
    }

    if (f->bmhd.masking !=0) {
        printf("MASKING=%d\n",f->bmhd.masking );
        *err = ERR_MALFORMED;   // got BODY before BMHD
        return false;
    }

    switch (f->bmhd.compression) {
        case cmpByteRun1: break;
        case cmpNone: break;
        default:
            *err = ERR_UNSUPPORTED;
            return false;
    }

    if (f->image_data) {
        // already had a BODY chunk?
        *err = ERR_MALFORMED;
        return false;
    }

    if (f->bmhd.nPlanes >32) {
        *err = ERR_MALFORMED;
        return false;
    }

    // TODO: just slurp chunk into ctx->buf and decode from memory

    // actual image data must be multiple of 16 pixels wide
    words_per_line = (f->bmhd.w+15)/16;
    bytes_per_line = f->bmhd.nPlanes * (words_per_line*2);
    data_size = f->bmhd.h * bytes_per_line;
    f->image_data = imalloc(data_size);
    if( f->bmhd.compression == cmpNone) {

        int n = im_read(rdr,f->image_data, data_size);
        if (n != data_size) {
            // don't need to free image data - context will handle it
            *err = ERR_MALFORMED;
            return false;
        }
        consumed += data_size;
    } else if (f->bmhd.compression == cmpByteRun1) {
        uint8_t* dest = f->image_data;
        int y;
        // the compression breaks at the end of each line
        // but can run across planes.
        // TODO: I think the mask is included in the run
        for (y=0; y<f->bmhd.h; ++y) {
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
// TODO: make this memory-based
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
    frame* f = ctx_curframe(ctx);
    AnimHeader* anhd;

    if( !f) {
        *err = ERR_MALFORMED;
        return false;
    }
    anhd = &f->anhd;

    anhd->operation = im_read_u8(rdr);
    anhd->mask = im_read_u8(rdr);
    anhd->w = im_read_u16be(rdr);
    anhd->h = im_read_u16be(rdr);
    anhd->x = im_read_s16be(rdr);
    anhd->y = im_read_s16be(rdr);
    anhd->abstime = im_read_u32be(rdr);
    anhd->reltime = im_read_u32be(rdr);
    anhd->interleave = im_read_u8(rdr);
    anhd->pad0 = im_read_u8(rdr);
    anhd->bits = im_read_u32be(rdr);
    im_read(rdr,anhd->pad,16);

    if(im_error(rdr)) {
        *err = im_eof(rdr) ? ERR_MALFORMED : ERR_FILE;
        return false;
    }
    printf("%s op: 0x%02x mask: 0x%02x %d,%d %dx%d reltime: %d interleave: 0x%02x, bits: 0x%08x\n",
        indent(ctx->level),
        anhd->operation,
        anhd->mask,
        anhd->x,
        anhd->y,
        anhd->w,
        anhd->h,
        anhd->reltime,
        anhd->interleave,
        anhd->bits);

    f->got_anhd = true;    
    return true;
}

static bool handle_DLTA( context* ctx, im_reader* rdr, uint32_t chunklen, ImErr* err)
{
    frame* first;
    frame* cur;
    frame* from;    //
    int fromidx;
    int wordswide;
    int image_size;
    int height;
    int nplanes;
    int num_cols;

    if( !ctx_size_buffer(ctx,chunklen)) {
        *err = ERR_NOMEM;
        return false;
    }

    if(im_read(rdr,ctx->buf,chunklen)!=chunklen) {
        *err = im_eof(rdr) ? ERR_MALFORMED: ERR_FILE;
        return false;
    }

    if (ctx->nframes < 1) {
        *err = ERR_MALFORMED;
        return false;
    }

    first = ctx->frames[0];
    cur = ctx_curframe(ctx);
    if (!first || !cur || !first->got_bmhd || !cur->got_anhd) {
        *err = ERR_MALFORMED;
        return false;
    }

    if (cur->anhd.operation != 0x05) {
        // we currently only handle ANIM5
        *err = ERR_UNSUPPORTED;
        return false;
    }


    // first, copy the frame we're deltaing from...
    if (cur->anhd.interleave==0) {
        fromidx = cur->num - 2;       // default is two frames back
    } else {
        fromidx = cur->num - cur->anhd.interleave;
    }
    if (fromidx<0) {
        fromidx = 0;
//        *err = ERR_MALFORMED;
 //       return false;
    }
    printf("copy from frame %d\n",fromidx);

    from = ctx->frames[fromidx];
    if(!from->image_data) {
        *err = ERR_MALFORMED;
        return false;
    } 
    wordswide = (first->bmhd.w +15)/16;
    height = first->bmhd.h;
    nplanes = first->bmhd.nPlanes;
    image_size = (wordswide*2)*nplanes*height;
    cur->image_data = imalloc(image_size);
    if (!cur->image_data) {
        *err = ERR_NOMEM;
        return false;
    }
    memcpy(cur->image_data, from->image_data, image_size);



    // TODO: copy/merge the colourmap
    cur->cmap_data = imalloc(first->cmap_len);
    cur->cmap_len  = first->cmap_len;
    memcpy( cur->cmap_data, first->cmap_data, first->cmap_len);


    // now decode the delta upon the image
    num_cols = (first->bmhd.w + 7)/8;   // TODO: should be wordswide*2 ?
    if (!decodeANIM5chunk(ctx->buf, chunklen,
        cur->image_data,
        num_cols,
        first->bmhd.nPlanes,
        first->bmhd.h))
    {
        *err = ERR_MALFORMED;
        return false;
    }
    return true;
}


static bool decodeANIM5chunk(const uint8_t* src, size_t srclen, uint8_t* dest,
        int ncols, int nplanes, int height)
{
    int plane;
    uint32_t pos;
    uint32_t start[8];
    int pitch = ncols*nplanes;  // dest: bytes per line



    // first, unpack the 8 src pointers (and ignore 8 unused ones)
    if (srclen<16*4) {
        return false;
    }

    for (plane=0; plane<nplanes; ++plane) {
        const uint8_t* p = src + plane*4;
        start[plane] = (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3]; 
        printf("plane %d @ 0x%08x\n",plane,start[plane]);
    }
    int j;
    for( j=0; j<srclen; ++j) {
        if( (j&7) == 0 ) {
            printf("\n%08x: ",j);
        }
        printf("%02x ",src[j]);
    }
    printf("\n");

    for (plane=0; plane<nplanes; ++plane) {
        pos = start[plane];


        // TODO: READ OPCOUNT! first byte, says how many ops in column
        printf("plane %d (pos %d)\n", plane,pos);
        int col;
        int y;
        uint8_t op;
        for (col=0; col<ncols; ++col) {
            uint8_t* out = dest + (ncols*plane) + col;
            int opcnt;
            int i;
            if (pos+1>srclen) {
                printf("expect more cols\n");
                return false;
            }
            opcnt = (int)src[pos++];

            printf(" col %d/%d (%d ops)\n", col, ncols, opcnt);
            y=0;
            for (i=0; i<opcnt; ++i) {
                printf("  y=%d\n", y);
                if (pos+1 > srclen) {
                    return false;
                }
                op = src[pos++];
                if (op==0) {
                    uint8_t cnt,val;
                    // repeat
                    if (pos+2 > srclen) {
                        return false;
                    }
                    cnt = src[pos++];
                    val = src[pos++];
                    printf("    rep %d %d\n", cnt,val);
                    if (y+cnt > height) {
                        return false;
                    }
                    while (cnt>0) {
                        *out = val;
                        out += pitch;
                        ++y;
                        --cnt;
                    }
                } else if (op<128) {
                    // skip
                    int cnt = (int)op;
                    printf("    skip %d\n", cnt);
                    if (y+cnt > height) {
                        return false;
                    }
                    out += pitch*cnt;
                    y+=cnt;
                } else if (op>=128) {
                    int cnt = (int)(op&0x7f);
                    printf("    uniq %d\n", cnt);
                    if (pos+cnt > srclen || y+cnt > height ) {
                        return false;
                    }
                    while (cnt>0) {
                        *out = src[pos++];
                        out += pitch;
                        --cnt;
                        ++y;
                    }
                }
            }
        }
    }

#if 0
    // fetch
    // <128 :skip
    // 0, cnt, val : repeat val cnt times
    // <=128 : copy (n & 0x7f) bytes

#endif
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

    printf("%s%c%c%c%c [%d bytes]\n", indent(ctx->level), buf[0], buf[1], buf[2], buf[3], chunklen );
    if (chkcc(buf,"FORM")) {
//        printf("%d: enter FORM\n", ctx->level);
        ++ctx->level;
        // FORM chunks have children
        success = handle_FORM(ctx,rdr,chunklen, err);
        --ctx->level;
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

    context* ctx = ctx_new();
    if (!ctx) {
        *err = ERR_NOMEM;
        goto bailout;
    }
    if (parse_chunk(ctx,rdr,err)<0 ) {
        goto bailout;
    }

    bundle = im_bundle_new();
    if (!bundle)
    {
        *err = ERR_NOMEM;
        goto bailout;
    }

    if (!ctx_collect_bundle(ctx, bundle, err)) {
        goto bailout;
    }

    ctx_free(ctx);
    return bundle;

bailout:
    if (ctx) {
        ctx_free(ctx);
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
    for (i=0; i<ctx->nframes; ++i) {
        frame* f = ctx->frames[i];
        printf("add frame %d\n", i);
        img = frame_to_img(ctx, i, err);
        if (img) {
            SlotID id = {0};
            id.frame = f->num;
            if (!im_bundle_set(out,id,img)) {
                *err = ERR_NOMEM;
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

static im_img* frame_to_img(context* ctx, int frameidx, ImErr* err)
{
    int y;
    int plane_pitch;
    frame* f;
    frame* first;
    BitMapHeader* bmhd;

    f = ctx->frames[frameidx];
    first = ctx->frames[0];

    if( !first->got_bmhd || !f->image_data) {
        *err = ERR_MALFORMED;
        return NULL;
    }

    bmhd = &first->bmhd;

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
            const uint8_t* src = f->image_data + (y*plane_pitch*bmhd->nPlanes) + x/8;
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
    if (f->cmap_data) {
        if( !im_img_pal_set(img, PALFMT_RGB, f->cmap_len/3, f->cmap_data) ) {
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



