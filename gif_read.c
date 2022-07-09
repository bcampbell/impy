#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <gif_lib.h>

static ImErr gif_reader_finish(im_reader* reader);
static bool gif_get_img(im_reader* reader, im_imginfo* info);
static void gif_read_rows(im_reader* reader, unsigned int num_rows, uint8_t* buf);
static void gif_read_palette(im_reader* reader, uint8_t *buf);

static read_handler gif_read_handler = {
    gif_get_img,
    gif_read_rows,
    gif_read_palette,
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
    // im_reader members
    read_handler* handler;
    ImErr err;
    im_in* in;

    // gif-specific fields from here on
    enum {READY, HEADER, BODY} state;
    unsigned int rows_read;

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
    int frame_num;
    bool coalesce;
    im_img* accumulator;    // the latest decoded frame
    im_img* backup; // backup of accumulator, used for DISPOSE_BACKGROUND frames
    struct rect disposalrect;
    int disposal;
} gif_reader;

static void process_image(gif_reader *rdr);
static bool process_extension(gif_reader *rdr);
static bool apply_palette(GifFileType* gif, im_img* img,  int transparent_idx);
static bool decode(GifFileType* gif, im_img* destimg, int destx, int desty);
static bool decode_trns( gif_reader *rdr, im_img* destimg, int destx, int desty, uint8_t trns );
static void blit( const im_img* src, im_img* dest, int destx, int desty, int w, int h);
static void drawrect( im_img* img, int xo, int yo, int w, int h, uint8_t c);

static int input_fn(GifFileType *gif, GifByteType *buf, int size)
{
    im_in* rdr = (im_in*)gif->UserData;
    return (int)im_read(rdr, (void*)buf, (size_t)size);
}


im_reader* im_new_gif_reader( im_in* in, ImErr* err )
{
    int giferr;
    gif_reader* rdr = imalloc(sizeof(gif_reader));
    if (!rdr) {
        *err = ERR_NOMEM;
        return NULL;
    }

    // im_reader fields
    rdr->handler = &gif_read_handler;
    rdr->err = ERR_NONE;
    rdr->in = in;

    // gif-specific fields
    rdr->state = READY;
    rdr->rows_read = 0;
    rdr->gif = NULL;
    rdr->gcb_valid = false;
    rdr->linebuf = NULL;
    rdr->frame_num = 0;
    rdr->coalesce = true;
    rdr->accumulator = NULL;
    rdr->backup = NULL;
    rdr->disposal = DISPOSAL_UNSPECIFIED;

    rdr->gif = DGifOpen( (void*)rdr->in, input_fn, &giferr);
    if (!rdr->gif) {
        *err = translate_err(giferr);
        goto bailout;
    }
    // we need a buffer big enough to decode a line
    rdr->linebuf = imalloc((size_t)rdr->gif->SWidth);
    if (!rdr->linebuf) {
        *err = ERR_NOMEM;
        goto bailout;
    }

    // if we're coalescing, then use an accumulator image big enough for the entire canvas
    if (rdr->coalesce) {
        int w = (int)rdr->gif->SWidth;
        int h = (int)rdr->gif->SHeight;
        rdr->accumulator = im_img_new(w, h, 1, FMT_COLOUR_INDEX, DT_U8);
        if (!rdr->accumulator) {
            *err = ERR_NOMEM;
            goto bailout;
        }
        rdr->disposal = DISPOSAL_UNSPECIFIED;
        // clear it to the background colour
        drawrect(rdr->accumulator, 0, 0, w, h, (uint8_t)rdr->gif->SBackGroundColor);
    }

    return (im_reader*)rdr;
bailout:
    // err already set.
    gif_reader_finish((im_reader*)rdr);
    return NULL;
}


static ImErr gif_reader_finish(im_reader* reader)
{
    ImErr err;
    gif_reader* rdr = (gif_reader*)reader;

    if (rdr->backup) {
        im_img_free(rdr->backup);
    }
    if (rdr->accumulator) {
        im_img_free(rdr->accumulator);
    }
    if (rdr->linebuf) {
        ifree(rdr->linebuf);
    }
    if (rdr->gif) {
        int giferr;
        if(DGifCloseFile(rdr->gif, &giferr)!=GIF_OK) {
            if (rdr->err == ERR_NONE) {
                rdr->err = translate_err(giferr);
            }
        }
    }

    if (rdr->in) {
        if (im_in_close(rdr->in) < 0) {
            if (rdr->err != ERR_NONE) {
                rdr->err = ERR_FILE;
            }
        }
    }

    err = rdr->err;
    ifree(rdr);
    return err;
}

// Returns true if a new image is successfully prepped for reading.
static bool gif_get_img(im_reader* reader, im_imginfo* info)
{
    gif_reader* rdr = (gif_reader*)reader;

    if (rdr->err != ERR_NONE) {
        return false;
    }
    if (rdr->state != READY) {
        rdr->err = ERR_BAD_STATE;
        return false;
    }

    while(true) {
        GifRecordType rec_type;
        if (DGifGetRecordType(rdr->gif, &rec_type) != GIF_OK) {
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
                    im_img* acc = rdr->accumulator;
                    info->w = acc->w;
                    info->h = acc->h;
                    info->x_offset = acc->x_offset;
                    info->y_offset = acc->y_offset;
                    info->fmt = acc->format;
                    info->pal_num_colours = acc->pal_num_colours;
                    info->pal_fmt = acc->pal_fmt;
                }
                rdr->state = HEADER;
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

static void gif_read_rows(im_reader* reader, unsigned int num_rows, uint8_t* buf)
{
    gif_reader* rdr = (gif_reader*)reader;

    if (rdr->err != ERR_NONE) {
        return;
    }
    if (rdr->state == READY) {
        rdr->err = ERR_BAD_STATE;
        return;
    }

    if (rdr->state == HEADER) {
        // start reading.
        rdr->state = BODY;
        rdr->rows_read = 0;
    }

    assert(rdr->state == BODY);
    {
        size_t bytes_per_row;
        im_img* img = rdr->accumulator;
        assert(img);

        bytes_per_row = im_img_bytesperpixel(img) * img->w;
        if (rdr->rows_read + num_rows > img->h) {
            rdr->err = ERR_TOO_MANY_ROWS;
            return;
        }
        for (unsigned int row = 0; row < num_rows; ++row) {
            unsigned int y = rdr->rows_read + row;
            memcpy(buf, im_img_row(img, y), bytes_per_row);
            buf += bytes_per_row;
        }
        rdr->rows_read += num_rows;

        // Read them all?
        if (rdr->rows_read == img->h) {
            rdr->state = READY;
        }
    }
}

static void gif_read_palette(im_reader* reader, uint8_t *buf)
{
    gif_reader* rdr = (gif_reader*)reader;
    if (rdr->err != ERR_NONE) {
        return;
    }
    if (rdr->state != HEADER) {
        rdr->err = ERR_BAD_STATE;
        return;
    }

    {
        im_img *acc = rdr->accumulator;
        assert(acc);

        if (acc->pal_num_colours > 0) {
            size_t s = (acc->pal_fmt == PALFMT_RGBA) ? 4 : 3;
            memcpy(buf, acc->pal_data, acc->pal_num_colours * s);
        }
    }
}

// fetches the next frame into the accumulator img.
// Upon error sets the err field.
static void process_image(gif_reader* rdr)
{
    GifFileType* gif = rdr->gif;
    int trns = NO_TRANSPARENT_COLOR;
    if (rdr->gcb_valid) {
        trns = rdr->gcb.TransparentColor;
    }

    if (DGifGetImageDesc(gif) != GIF_OK) {
        rdr->err = translate_err(gif->Error);
        return;
    }

    if (rdr->coalesce) {
        // We're combining frames into the accumulator image as we go along.
        int disposal = DISPOSAL_UNSPECIFIED;
        if (rdr->gcb_valid) {
            disposal = rdr->gcb.DisposalMode;
        }


        // apply frame disposal...
        if (rdr->frame_num > 0 ) {
            switch (rdr->disposal) {
                case DISPOSAL_UNSPECIFIED:
                case DISPOSE_DO_NOT:
                    break;
                case DISPOSE_BACKGROUND:
                    {
                        struct rect *r = &rdr->disposalrect;
                        drawrect( rdr->accumulator,r->x, r->y, r->w, r->h, (uint8_t)gif->SBackGroundColor);
                    }
                    break;
                case DISPOSE_PREVIOUS:
                    // restore from backup
                    blit(rdr->backup, rdr->accumulator,0,0,(int)gif->SWidth,(int)gif->SHeight);
                    break;
            }
        }

        // back up the current accumulator if we'll need to roll back
        if (disposal == DISPOSE_PREVIOUS) {
            if (rdr->backup) {
                im_img_free(rdr->backup);
            }
            rdr->backup = im_img_clone(rdr->accumulator);
            if (!rdr->backup) {
                rdr->err = ERR_NOMEM;
                return;
            }
        }

        // decode the new frame into the accumulator
        if (trns == NO_TRANSPARENT_COLOR) {
            if (!decode(gif, rdr->accumulator, gif->Image.Left, gif->Image.Top)) {
                rdr->err = translate_err(gif->Error);
                return;
            }
        } else {
            if (!decode_trns(rdr, rdr->accumulator, gif->Image.Left, gif->Image.Top, (uint8_t)trns)) {
                rdr->err = translate_err(gif->Error);
                return;
            }
        }

        // update the palette
        if (!apply_palette(gif, rdr->accumulator, trns)) {
            rdr->err = ERR_NOMEM;
            return;
        }

        // remember what we need to do next time
        rdr->disposal = disposal;
        rdr->disposalrect.x = (int)gif->Image.Left;
        rdr->disposalrect.y = (int)gif->Image.Top;
        rdr->disposalrect.w = (int)gif->Image.Width;
        rdr->disposalrect.h = (int)gif->Image.Height;
    } else {
        // load each frame as separate image
        im_img* img = NULL;
        // TODO: could avoid mallocs each frame and reuse img buffer if big enough.
        // im_img_reuse()?
        img = im_img_new((int)gif->Image.Width, (int)gif->Image.Height,1,FMT_COLOUR_INDEX, DT_U8);
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

        if (rdr->accumulator) {
            im_img_free(rdr->accumulator);
        }
        rdr->accumulator = img;
    }
    rdr->frame_num++;
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


static bool decode_trns( gif_reader* rdr, im_img* destimg, int destx, int desty, uint8_t trns )
{
    GifFileType* gif = rdr->gif;
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
                uint8_t *buf = rdr->linebuf;
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
            uint8_t *buf = rdr->linebuf;
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


static bool process_extension(gif_reader *rdr)
{
    GifFileType* gif = rdr->gif;
    GifByteType* buf;
    int ext_code;

    if (DGifGetExtension(gif, &ext_code, &buf) != GIF_OK) {
        return false;
    }

    if (ext_code==GRAPHICS_EXT_FUNC_CODE) {
        if(DGifExtensionToGCB(buf[0], buf+1, &rdr->gcb)!=GIF_OK) {
            return false;
        }
        rdr->gcb_valid = true;
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
    ImPalFmt palfmt;
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

    palfmt = (transparent_idx==NO_TRANSPARENT_COLOR) ? PALFMT_RGB:PALFMT_RGBA;
    if( !im_img_pal_set( img, palfmt, cm->ColorCount, NULL )) {
        return false;
    }
    return im_img_pal_write( img, 0, cm->ColorCount, PALFMT_RGBA, buf);
}


// copy an image onto another
static void blit( const im_img* src, im_img* dest, int destx, int desty, int w, int h)
{
    size_t rowbytes = w*im_img_bytesperpixel(src);

    assert(destx >=0 && desty >=0);
    assert( w <= src->w && w <= dest->w);
    assert( h <= src->h && h <= dest->h);
    assert( destx+w <= dest->w);
    assert( desty+h <= dest->h);

    assert( src->format == dest->format);
    assert( src->datatype == dest->datatype);

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

