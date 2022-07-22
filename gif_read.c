#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <gif_lib.h>

static void gif_reader_finish(im_reader *rdr);
static bool gif_get_img(im_reader *rdr);
static void gif_read_rows(im_reader *rdr, unsigned int num_rows, void *buf, int stride);

static void read_file_header(im_reader* rdr);

static read_handler gif_read_handler = {
    gif_get_img,
    gif_read_rows,
    gif_reader_finish
};


static ImErr translate_err( int gif_err_code )
{
    switch(gif_err_code) {
        case D_GIF_ERR_NOT_GIF_FILE: return ERR_MALFORMED;
        case D_GIF_ERR_NOT_ENOUGH_MEM: return ERR_NOMEM;
       //TODO:
        default: return ERR_MALFORMED; 
    }
}

struct rect {
    int x,y,w,h;
};

typedef struct gif_reader {
    im_reader base;

    // GIF-specific fields
    GifFileType *gif;
    // the currently-active gcb data, if any
    bool gcb_valid;
    GraphicsControlBlock gcb;

    // int loop_count; // from NETSCAPE block
    // TODO: stash any comment extension records we encounter

    // buffer to hold one line (only used for transparency decode)
    uint8_t *linebuf;

    // Coalesce means compose the frames are rendered as go.
    // Without coalesce, each frame will be returned exactly as it is stored
    // in the file (all different sizes/offsets etc).
    bool coalesce;
    im_img* accumulator;    // the latest decoded frame
    im_img* backup; // backup of accumulator, used for DISPOSE_BACKGROUND frames
    struct rect disposalrect;
    int disposal;
} gif_reader;

static void process_image(im_reader *rdr);
static bool process_extension(im_reader *rdr);
static bool apply_palette(GifFileType* gif, im_img* img,  int transparent_idx);
static bool decode(GifFileType* gif, im_img* destimg, int destx, int desty);
static bool decode_trns(im_reader *rdr, im_img* destimg, int destx, int desty, uint8_t trns );
static void blit( const im_img* src, im_img* dest, int destx, int desty, int w, int h);
static void drawrect( im_img* img, int xo, int yo, int w, int h, uint8_t c);

static int input_fn(GifFileType *gif, GifByteType *buf, int size)
{
    im_in* rdr = (im_in*)gif->UserData;
    return (int)im_read(rdr, (void*)buf, (size_t)size);
}


im_reader* im_new_gif_reader( im_in* in, ImErr* err )
{
    gif_reader* gr = imalloc(sizeof(gif_reader));
    if (!gr) {
        *err = ERR_NOMEM;
        return NULL;
    }

    i_reader_init(&gr->base);
    gr->base.handler = &gif_read_handler;
    gr->base.in = in;

    // gif-specific fields
    gr->gif = NULL;
    gr->gcb_valid = false;
    gr->linebuf = NULL;
    gr->coalesce = true;
    gr->accumulator = NULL;
    gr->backup = NULL;
    gr->disposal = DISPOSAL_UNSPECIFIED;

    return (im_reader*)gr;
}


static void read_file_header(im_reader* rdr)
{
    gif_reader* gr = (gif_reader*)rdr;
    int giferr;

    assert(gr->gif == NULL);
    assert(rdr->frame_num == 0);

    gr->gif = DGifOpen( (void*)rdr->in, input_fn, &giferr);
    if (!gr->gif) {
        rdr->err = translate_err(giferr);
        return;
    }

    // we need a buffer big enough to decode a line
    gr->linebuf = imalloc((size_t)gr->gif->SWidth);
    if (!gr->linebuf) {
        rdr->err = ERR_NOMEM;
        return;
    }

    // if we're coalescing, then use an accumulator image big enough for the entire canvas
    if (gr->coalesce) {
        int w = (int)gr->gif->SWidth;
        int h = (int)gr->gif->SHeight;
        gr->accumulator = im_img_new(w, h, 1, IM_FMT_INDEX8);
        if (!gr->accumulator) {
            rdr->err = ERR_NOMEM;
            return;
        }
        gr->disposal = DISPOSAL_UNSPECIFIED;
        // clear it to the background colour
        drawrect(gr->accumulator, 0, 0, w, h, (uint8_t)gr->gif->SBackGroundColor);
    }
}


static void gif_reader_finish(im_reader* rdr)
{
    gif_reader* gr = (gif_reader*)rdr;

    if (gr->backup) {
        im_img_free(gr->backup);
        gr->backup = NULL;
    }
    if (gr->accumulator) {
        im_img_free(gr->accumulator);
        gr->accumulator = NULL;
    }
    if (gr->linebuf) {
        ifree(gr->linebuf);
        gr->linebuf = NULL;
    }
    if (gr->gif) {
        int giferr;
        if(DGifCloseFile(gr->gif, &giferr)!=GIF_OK) {
            if (rdr->err == ERR_NONE) {
                rdr->err = translate_err(giferr);
            }
        }
        gr->gif = NULL;
    }
}

// Returns true if a new image is successfully prepped for reading.
static bool gif_get_img(im_reader* rdr)
{
    gif_reader* gr = (gif_reader*)rdr;

    if (rdr->frame_num == 0) {
        // First frame, so open the file and get the file header et al.
        read_file_header(rdr);
        if (rdr->err != ERR_NONE) {
            return false;
        }
    }

    while(true) {
        GifRecordType rec_type;
        if (DGifGetRecordType(gr->gif, &rec_type) != GIF_OK) {
            rdr->err = ERR_MALFORMED;
            return false;
        }
        switch(rec_type) {
            case UNDEFINED_RECORD_TYPE:
            case SCREEN_DESC_RECORD_TYPE:
                rdr->err = ERR_MALFORMED;
                return false;
            case IMAGE_DESC_RECORD_TYPE:
                // read the frame
                process_image(rdr);
                if (rdr->err != ERR_NONE) {
                    return false;
                }
                {
                    // Populate curr from the accumulator image.
                    im_img* img = gr->accumulator;
                    im_imginfo* info = &rdr->curr;
                    info->w = img->w;
                    info->h = img->h;
                    info->x_offset = img->x_offset;
                    info->y_offset = img->y_offset;
                    info->fmt = img->format;
                    info->pal_num_colours = img->pal_num_colours;

                    if (img->pal_num_colours>0) {
                        // Copy out palette, in RGBA format.
                        rdr->pal_data = irealloc(rdr->pal_data, img->pal_num_colours * im_fmt_bytesperpixel(IM_FMT_RGBA));
                        if (!rdr->pal_data) {
                            rdr->err = ERR_NOMEM;
                            return false;
                        }
                        im_convert_fn cvt_fn = i_pick_convert_fn(img->pal_fmt, IM_FMT_RGBA);
                        if (!cvt_fn) {
                            rdr->err = ERR_NOCONV;
                            return false;
                        }
                        cvt_fn(img->pal_data, rdr->pal_data, img->pal_num_colours, 0, NULL);
                    }
                }
                return true;    // got an image.
            case EXTENSION_RECORD_TYPE:
                // TODO: set error code!
                if( !process_extension(rdr) ) {
                    rdr->err = ERR_MALFORMED;
                    return false;
                }
                break;
            case TERMINATE_RECORD_TYPE:
                return false;   // happy exit.
            default:
                break;
        }
    }
}

static void gif_read_rows(im_reader *rdr, unsigned int num_rows, void *buf, int stride)
{
    gif_reader* gr = (gif_reader*)rdr;
    // Read rows out from the accumulator image.
    im_img* img = gr->accumulator;
    size_t bytes_per_row;

    assert(rdr->state == READSTATE_BODY);
    assert(img);

    bytes_per_row = im_fmt_bytesperpixel(img->format) * img->w;
    for (unsigned int row = 0; row < num_rows; ++row) {
        unsigned int y = rdr->rows_read + row;
        memcpy(buf, im_img_row(img, y), bytes_per_row);
        buf += stride;
    }
}

// fetches the next frame into the accumulator img.
// Upon error sets the err field.
static void process_image(im_reader* rdr)
{
    gif_reader* gr = (gif_reader*)rdr;
    GifFileType* gif = gr->gif;
    int trns = NO_TRANSPARENT_COLOR;
    if (gr->gcb_valid) {
        trns = gr->gcb.TransparentColor;
    }

    if (DGifGetImageDesc(gif) != GIF_OK) {
        rdr->err = translate_err(gif->Error);
        return;
    }

    if (gr->coalesce) {
        // We're combining frames into the accumulator image as we go along.
        int disposal = DISPOSAL_UNSPECIFIED;
        if (gr->gcb_valid) {
            disposal = gr->gcb.DisposalMode;
        }


        // apply frame disposal...
        if (rdr->frame_num > 0 ) {
            switch (gr->disposal) {
                case DISPOSAL_UNSPECIFIED:
                case DISPOSE_DO_NOT:
                    break;
                case DISPOSE_BACKGROUND:
                    {
                        struct rect *r = &gr->disposalrect;
                        drawrect( gr->accumulator,r->x, r->y, r->w, r->h, (uint8_t)gif->SBackGroundColor);
                    }
                    break;
                case DISPOSE_PREVIOUS:
                    // restore from backup
                    blit(gr->backup, gr->accumulator,0,0,(int)gif->SWidth,(int)gif->SHeight);
                    break;
            }
        }

        // back up the current accumulator if we'll need to roll back
        if (disposal == DISPOSE_PREVIOUS) {
            if (gr->backup) {
                im_img_free(gr->backup);
            }
            gr->backup = im_img_clone(gr->accumulator);
            if (!gr->backup) {
                rdr->err = ERR_NOMEM;
                return;
            }
        }

        // decode the new frame into the accumulator
        if (trns == NO_TRANSPARENT_COLOR) {
            if (!decode(gif, gr->accumulator, gif->Image.Left, gif->Image.Top)) {
                rdr->err = translate_err(gif->Error);
                return;
            }
        } else {
            if (!decode_trns(rdr, gr->accumulator, gif->Image.Left, gif->Image.Top, (uint8_t)trns)) {
                rdr->err = translate_err(gif->Error);
                return;
            }
        }

        // update the palette
        if (!apply_palette(gif, gr->accumulator, trns)) {
            rdr->err = ERR_NOMEM;
            return;
        }

        // remember what we need to do next time
        gr->disposal = disposal;
        gr->disposalrect.x = (int)gif->Image.Left;
        gr->disposalrect.y = (int)gif->Image.Top;
        gr->disposalrect.w = (int)gif->Image.Width;
        gr->disposalrect.h = (int)gif->Image.Height;
    } else {
        // load each frame as separate image
        im_img* img = NULL;
        // TODO: could avoid mallocs each frame and reuse img buffer if big enough.
        // im_img_reuse()?
        img = im_img_new((int)gif->Image.Width, (int)gif->Image.Height,1,IM_FMT_INDEX8);
        if (!img) {
            rdr->err = ERR_NOMEM;
            return;
        }
        if (!decode(gif, img, 0, 0)) {
            rdr->err = translate_err(gif->Error);
            im_img_free(img);
            return;
        }

        if (!apply_palette(gif, img, trns)) {
            rdr->err = ERR_NOMEM;
            im_img_free(img);
            return;
        }

        img->x_offset = (int)gif->Image.Left;
        img->y_offset = (int)gif->Image.Top;
        // TODO: record disposal details here

        if (gr->accumulator) {
            im_img_free(gr->accumulator);
        }
        gr->accumulator = img;
    }
}


static bool decode( GifFileType* gif, im_img* destimg, int destx, int desty )
{
    int w = (int)gif->Image.Width;
    int h = (int)gif->Image.Height;
    int y;
    if (gif->Image.Interlace) {
        // GIF interlacing stores the lines in the order:
        // 0, 8, 16, ...(8n)
        // 4, 12, ...(8n+4)
        // 2, 6, 10, 14, ...(4n+2)
        // 1, 3, 5, 7, 9, ...(2n+1).
        int offsets[4] = {0,4,2,1};
        int jumps[4] = {8,8,4,2};
        int pass;
        for (pass=0; pass<4; ++pass) {
            for(y=offsets[pass]; y<h; y+= jumps[pass]) {
                uint8_t *dest = im_img_pos(destimg, destx+0, desty+y);
                if (DGifGetLine(gif, dest, w) != GIF_OK) {
                    // TODO: set error?
                    return false;
                }
            }
        }
    } else {
        for (y=0; y<h; ++y) { 
            uint8_t *dest = im_img_pos(destimg, destx+0, desty+y);
            if (DGifGetLine(gif, dest, w) != GIF_OK) {
                // TODO: set error?
                return false;
            }
        }
    }
    return true;
}


static bool decode_trns(im_reader* rdr, im_img* destimg, int destx, int desty, uint8_t trns )
{
    gif_reader* gr = (gif_reader*)rdr;
    GifFileType* gif = gr->gif;
    int w = (int)gif->Image.Width;
    int h = (int)gif->Image.Height;
    int x,y;
    if (gif->Image.Interlace) {
        // GIF interlacing stores the lines in the order:
        // 0, 8, 16, ...(8n)
        // 4, 12, ...(8n+4)
        // 2, 6, 10, 14, ...(4n+2)
        // 1, 3, 5, 7, 9, ...(2n+1).
        int offsets[4] = {0,4,2,1};
        int jumps[4] = {8,8,4,2};
        int pass;
        for (pass = 0; pass < 4; ++pass) {
            for(y=offsets[pass]; y<h; y+= jumps[pass]) {
                uint8_t *buf = gr->linebuf;
                uint8_t *dest = im_img_pos(destimg, destx+0, desty+y);
                if (DGifGetLine(gif, buf, w) != GIF_OK) {
                    return false;
                }
                for( x=0; x<w; ++x) {
                    uint8_t c = *buf++;
                    if (c != trns) {
                        *dest = c;
                    }
                    ++dest;
                }
            }
        }
    } else {
        for (y = 0; y < h; ++y) { 
            uint8_t *buf = gr->linebuf;
            uint8_t *dest = im_img_pos(destimg, destx+0, desty+y);
            if (DGifGetLine(gif, buf, w) != GIF_OK) {
                return false;
            }
            for( x=0; x<w; ++x) {
                uint8_t c = *buf++;
                if (c != trns) {
                    *dest = c;
                }
                ++dest;
            }
        }
    }
    return true;
}


static bool process_extension(im_reader *rdr)
{
    gif_reader* gr = (gif_reader*)rdr;
    GifFileType* gif = gr->gif;
    GifByteType* buf;
    int ext_code;

    if (DGifGetExtension(gif, &ext_code, &buf) != GIF_OK) {
        return false;
    }

    if (ext_code==GRAPHICS_EXT_FUNC_CODE) {
        if(DGifExtensionToGCB(buf[0], buf+1, &gr->gcb)!=GIF_OK) {
            return false;
        }
        gr->gcb_valid = true;
        /*
        printf("ext (GCB) disposal=%d delay=%d trans=%d\n",
                state->gcb.DisposalMode, state->gcb.DelayTime, state->gcb.TransparentColor );
                */
    } else if (ext_code==APPLICATION_EXT_FUNC_CODE) {
        // TODO: check for NETSCAPE block with loop count
        /*
        printf("ext (0xff - application) %d bytes: '%c%c%c%c%c%c%c%c'\n",
             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
             */
    } else {
        /*printf("ext (0x%02x)\n",ext_code); */
    }

    while (1) {
        if (DGifGetExtensionNext(gif,&buf) != GIF_OK) {
            return false;
        }
        if (buf==NULL) {
            //printf("  endext\n");
            break;
        }
        // TODO: collect comment blocks here
        //printf("  extnext (%d bytes)\n", buf[0]);
    }
    return true;
}

static bool apply_palette(GifFileType* gif, im_img* img,  int transparent_idx)
{
    uint8_t buf[256*4];
    uint8_t* dest;
    int i;
    ImFmt palfmt;
    ColorMapObject* cm = gif->Image.ColorMap;
    if (!cm) {
        cm = gif->SColorMap;    // fall back to global palette
    }
    if( !cm ) {
        // it's valid (but bonkers) for there to be no palette at all
        return true;
    }

    // GifColorType doesn't seem to be byte-packed, so pack it ourselves
    // (and add alpha while we're at it)
    dest = buf;
    for (i=0; i<cm->ColorCount; ++i) {
        GifColorType *src = &cm->Colors[i];
        *dest++ = src->Red;
        *dest++ = src->Green;
        *dest++ = src->Blue;
        *dest++ = (transparent_idx == i) ? 0 : 255;
    }

    palfmt = (transparent_idx==NO_TRANSPARENT_COLOR) ? IM_FMT_RGB : IM_FMT_RGBA;
    if( !im_img_pal_set( img, palfmt, cm->ColorCount, NULL )) {
        return false;
    }
    return im_img_pal_write( img, 0, cm->ColorCount, IM_FMT_RGBA, buf);
}


// copy an image onto another
static void blit( const im_img* src, im_img* dest, int destx, int desty, int w, int h)
{
    size_t rowbytes = w * im_fmt_bytesperpixel(src->format);

    assert(destx >=0 && desty >=0);
    assert( w <= src->w && w <= dest->w);
    assert( h <= src->h && h <= dest->h);
    assert( destx+w <= dest->w);
    assert( desty+h <= dest->h);

    assert( src->format == dest->format);

    for (int y=0; y<h; ++y) {
        memcpy( im_img_pos(dest,destx,desty+y), im_img_pos(src, 0,y), rowbytes);
    }
}

static void drawrect( im_img* img, int xo, int yo, int w, int h, uint8_t c)
{
    assert( xo>=0 && yo>=0);
    assert( xo+w <= img->w);
    assert( yo+h <= img->h);
    int x,y;

    for (y=0; y<h; ++y) {
        uint8_t* dest = im_img_pos(img,xo,yo+y);
        for (x=0; x<w; ++x) {
            *dest++ = c;
        }
    }
}

