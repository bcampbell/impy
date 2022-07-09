#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <gif_lib.h>




typedef struct gif_writer {
    // embedded im_writer
    write_handler* handler;
    ImErr err;
    im_out* out;

    // gif-specific stuff
    enum {READY, HEADER, BODY} state;
    GifFileType *gif;
    unsigned int num_frames;
    ColorMapObject* global_cm;
    int global_trans;
    // Details about current frame
    int x_offset;
    int y_offset;
    unsigned int w;
    unsigned int h;
    unsigned int rows_written;
    ColorMapObject* local_cm;
    int local_trans;
} gif_writer;


static ImErr translate_err(int gif_err_code);
static int output_fn(GifFileType *gif, const GifByteType *buf, int size);
static void emit_file_header(gif_writer* writer);
static void emit_frame_header(gif_writer* writer);
static int calc_colour_res(int ncolours);
//static bool is_power_of_two(uint32_t n) { return (n & (n - 1)) == 0; } 

static void gif_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt);
static void gif_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours);
static void gif_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t* data);
static ImErr gif_finish(im_writer* writer);

static struct write_handler gif_write_handler = {
    IM_FILEFMT_GIF,
    gif_begin_img,
    gif_write_rows,
    gif_set_palette,
    gif_finish
};

im_writer* igif_new_writer(im_out* out, ImErr* err)
{
    int giferr;
    gif_writer* wr = imalloc(sizeof(gif_writer));
    if (!wr) {
        *err = ERR_NOMEM;
        return NULL;
    }
    wr->handler = &gif_write_handler;
    wr->out = out;
    wr->err = ERR_NONE;
    wr->state = READY;
    wr->gif = NULL;
    wr->num_frames = 0;
    wr->global_cm = NULL;
    wr->global_trans = -1;

    wr->x_offset = 0;
    wr->y_offset = 0;
    wr->w = 0;
    wr->h = 0;
    wr->rows_written = 0;
    wr->local_cm = NULL;
    wr->local_trans = -1;

    wr->gif = EGifOpen((void*)wr->out, output_fn, &giferr);
    if (!wr->gif) {
        ifree(wr);
        *err = translate_err(giferr);
        return NULL;
    }

    return (im_writer*)wr;
}

static void gif_begin_img(im_writer* writer, unsigned int w, unsigned int h, ImFmt fmt)
{
    gif_writer* wr = (gif_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }

    // Ready for new frame?
    if (wr->state != READY) {
        // The current image is unfinished
        wr->err = ERR_UNFINISHED_IMG;
        return;
    }

    if (fmt != FMT_COLOUR_INDEX) {
        wr->err = ERR_UNSUPPORTED_FMT;
        return;
    }

    wr->w = w;
    wr->h = h;
    wr->state = HEADER;
    // now ready for rows, palette whatever
}



static void gif_write_rows(im_writer* writer, unsigned int num_rows, const uint8_t* data)
{
    gif_writer* wr = (gif_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }
    if (wr->state == READY) {
        wr->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }

    if (wr->state == HEADER) {
        if (wr->num_frames == 0) {
            emit_file_header(wr);
            if (wr->err != ERR_NONE) {
                return;
            }
        }

        emit_frame_header(wr);
        if (wr->err != ERR_NONE) {
            return;
        }

        wr->state = BODY;
        wr->rows_written = 0;
    }


    // 
    {
        int i;
        uint8_t const* src = data;
        for (i = 0; i < num_rows; ++i) {
            if (wr->rows_written >= wr->h) {
                // Trying to write more rows than the image has!
                wr->err = ERR_TOO_MANY_ROWS;
                return;
            }
            if( GIF_OK !=EGifPutLine(wr->gif, (GifPixelType *)src, wr->w)) {
                wr->err = translate_err(wr->gif->Error);
                return;
            }
            wr->rows_written++;
            src += wr->w;
        }
        // Finished the frame?
        if (wr->rows_written >= wr->h) {
            wr->state = READY;
            wr->num_frames++;
        }
    }
}


static void emit_file_header(gif_writer* wr)
{
    // Check we've got all the frame details we need...
    if( wr->global_cm == NULL) {
        // No palette!
        wr->err = ERR_NO_PALETTE;
        return;
    }

    // emit file header blocks

    // GIF89 - TODO: only if needed...
    EGifSetGifVersion(wr->gif, true);

    if (GIF_OK != EGifPutScreenDesc(wr->gif,
        wr->w,  // TODO: option to specify screen size in advance?
        wr->h,
        calc_colour_res(wr->global_cm->ColorCount),
        0,  // (bgcolor)
        wr->global_cm))
    {
        // failed
        wr->err = translate_err(wr->gif->Error);
        return;
    }
}

// Return true if a and b hold the same colour values.
static bool colormaps_equal(ColorMapObject const* a, ColorMapObject const* b) {
    int n = a->ColorCount;
    if (n != b->ColorCount) {
        return false;
    }
    return (0 == memcmp(a->Colors, b->Colors, n * sizeof(GifColorType)));
}


static void emit_frame_header(gif_writer* wr)
{
    // Need to pick palette and transparent index
    ColorMapObject* cm = wr->global_cm;
    int trans = wr->global_trans;

    // If we've got a local palette and it's different to the global one,
    // use it instead.
    if (wr->local_cm) {
        if (!colormaps_equal(cm, wr->local_cm)) {
            cm = wr->local_cm;
            trans = wr->local_trans;
        }
    }

    // output a GCB block
    {
        uint8_t gcb_buf[4];
        GraphicsControlBlock gcb = {0};
        gcb.DisposalMode = DISPOSAL_UNSPECIFIED;
        gcb.UserInputFlag = false;
        gcb.DelayTime = 10; // in 0.01sec units
        gcb.TransparentColor = trans;   // TODO: local or global

        EGifGCBToExtension(&gcb, (GifByteType*)gcb_buf);

        if( EGifPutExtension(wr->gif, GRAPHICS_EXT_FUNC_CODE, 4, gcb_buf) != GIF_OK) {
            wr->err = translate_err(wr->gif->Error);
            return;
        }
    }

    if (GIF_OK != EGifPutImageDesc(
        wr->gif,
        wr->x_offset, wr->y_offset,
        wr->w, wr->h,
        false,  // interlace
        cm))
    {
        wr->err = translate_err(wr->gif->Error);
        return;
    }
}



static int output_fn( GifFileType *gif, const GifByteType *buf, int size)
{
    im_out* out = (im_out*)gif->UserData;
    return (int)im_write(out, (void*)buf, (size_t)size);
}


// Set palette for the current frame.
// If not called, assumes that the palette for the previous frame is in effect.
static void gif_set_palette(im_writer* writer, ImPalFmt pal_fmt, unsigned int num_colours, const uint8_t *colours)
{
    ColorMapObject* cm;
    GifColorType* dest;
    const uint8_t* src;
    int trans;
    int i;

    gif_writer* wr = (gif_writer*)writer;
    if (wr->err != ERR_NONE) {
        return;
    }

    if (wr->state == READY) {
        wr->err = ERR_NOT_IN_IMG;   // begin_img wasn't called first.
        return;
    }

    // TODO:  GifMakeMapObject fails if colour count is not power-of-two.
    cm = GifMakeMapObject( num_colours, NULL );
    if (!cm) {
        wr->err = ERR_EXTLIB;
        return;
    }
    
    src = colours;
    dest = cm->Colors;
    trans = -1;
    switch (pal_fmt) {
        case PALFMT_RGB:
            for (i=0; i<num_colours; ++i) {
                dest->Red = *src++;
                dest->Green = *src++;
                dest->Blue = *src++;
                ++dest;
            }
            break;
        case PALFMT_RGBA:
            {
                uint8_t alpha;
                for (i=0; i<num_colours; ++i) {
                    dest->Red = *src++;
                    dest->Green = *src++;
                    dest->Blue = *src++;
                    ++dest;
                    alpha = *src++;
                    if (trans == -1 && alpha == 0) {
                        trans = i;
                    }
                }
            }
            break;
        default:
            GifFreeMapObject(cm);
            wr->err = ERR_UNSUPPORTED;  // unknown palette fmt
            return;
    }

    if (wr->num_frames == 0) {
        // First frame: set the global_cm.
        if (wr->global_cm) {
            GifFreeMapObject(wr->global_cm);
        }
        wr->global_cm = cm;
        wr->global_trans = trans;
    } else {
        // Subsequent frames: set local_cm. The local_cm carries over to next
        // frame, but we'll only output a local_cm for any given frame if
        // it's different to the global_cm.
        if (wr->local_cm) {
            GifFreeMapObject(wr->local_cm);
        }
        wr->local_cm = cm;
        wr->local_trans = trans;
    }
    return; 
}

static ImErr translate_err(int gif_err_code)
{
    switch(gif_err_code) {
        case D_GIF_ERR_NOT_GIF_FILE: return ERR_MALFORMED;
        case D_GIF_ERR_NOT_ENOUGH_MEM: return ERR_NOMEM;
       //TODO:
        default: return ERR_MALFORMED; 
    }
}

static int calc_colour_res(int ncolours)
{
    int i;
    for( i=1; i<=8; ++i) {
        if( ncolours <= (1<<i)) {
            return i;
        }
    }
    return 8;
}

ImErr gif_finish(im_writer* writer)
{
    ImErr err;

    gif_writer* wr = (gif_writer*)writer;
    if (wr->global_cm) {
        GifFreeMapObject(wr->global_cm);
        wr->global_cm = NULL;
    }
    if (wr->local_cm) {
        GifFreeMapObject(wr->local_cm);
        wr->local_cm = NULL;
    }

    //GifFreeMapObject(global_cm);
    if (wr->gif) {
        int giferr;
        if( GIF_OK != EGifCloseFile(wr->gif, &giferr) ) {
            if (wr->err == ERR_NONE) {
                wr->err = translate_err(giferr);
            }
        }
        wr->gif = NULL;
    }

    if (wr->out) {
        if (im_out_close(wr->out) < 0 ) {
            if (wr->err == ERR_NONE) {
                wr->err = ERR_FILE;
            }
        }
        wr->out = NULL;
    }

    err = wr->err;
    ifree(wr);
    return err;
}

// TODO: support gif looping!
#if 0
static bool write_loops(GifFileType*gif, int loops)
{
    uint8_t blk1[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
    uint8_t blk2[3] = { 0x01, (loops&0xff), ((loops>>8) & 0xff)};
    if(EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) != GIF_OK) {
        return false;
    }
    if(EGifPutExtensionBlock(gif, sizeof(blk1), blk1) != GIF_OK) {
        return false;
    }
    if(EGifPutExtensionBlock(gif, sizeof(blk2), blk2) != GIF_OK) {
        return false;
    }
    if(EGifPutExtensionTrailer(gif) != GIF_OK) {
        return false;
    }
    return true;
}
#endif

