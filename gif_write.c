#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <gif_lib.h>




typedef struct gif_writer {
    im_write base;

    // gif-specific stuff
    GifFileType *gif;
    ColorMapObject* global_cm;
    ColorMapObject* local_cm;
} gif_writer;


static void pre_img(im_write* wr);
static void emit_header(im_write* wr);
static void emit_rows(im_write *wr, unsigned int num_rows, const void *data, int stride);
static void finish(im_write* wr);


static int calc_colour_res(int ncolours);
static bool colormaps_equal(ColorMapObject const* a, ColorMapObject const* b);
static ColorMapObject* cvt_palette(unsigned int num_colours, const uint8_t *src, int* trans);
static ImErr translate_err(int gif_err_code);
static int output_fn(GifFileType *gif, const GifByteType *buf, int size);

static bool write_loops(GifFileType*gif, int loops);


static struct write_handler gif_write_handler = {
    IM_FILETYPE_GIF,
    pre_img,
    emit_header,
    emit_rows,
    NULL,   // post_img()
    finish
};

im_write* igif_new_writer(im_out* out, ImErr* err)
{
    gif_writer* gw = imalloc(sizeof(gif_writer));
    if (!gw) {
        *err = IM_ERR_NOMEM;
        return NULL;
    }
    i_write_init(&gw->base);

    gw->base.handler = &gif_write_handler;
    gw->base.out = out;

    gw->gif = NULL;
    gw->global_cm = NULL;
    gw->local_cm = NULL;
    return (im_write*)gw;
}

// Optional hook, called at the last stage of im_begin_img(), but before
// im_set_palette() et al...
static void pre_img(im_write* writer)
{
    // Only accept indexed data. We won't do quantisation on-the-fly, so this
    // will fail if the user is planning to send us RGB or whatever.
    i_write_set_internal_fmt(writer, IM_FMT_INDEX8);
    if (writer->err != IM_ERR_NONE) {
        return;
    }
}


// Write out everything up to the start of the row data itself.
static void emit_header(im_write* wr)
{
    gif_writer* gw = (gif_writer*)wr;

    // First frame?
    if (wr->num_frames==0) {
        int giferr;
        assert(!gw->gif);
        gw->gif = EGifOpen((void*)wr->out, output_fn, &giferr);
        if (!gw->gif) {
            wr->err = translate_err(giferr);
            return;
        }

        // Set up the global colormap (and transparent index)
        int global_trans = NO_TRANSPARENT_COLOR;
        gw->global_cm = cvt_palette(wr->pal_num_colours, wr->pal_data, &global_trans);
        if( gw->global_cm == NULL) {
            wr->err = IM_ERR_NO_PALETTE;
            return;
        }

        // Emit preamble and screen description

        // GIF89 - TODO: only if needed...
        EGifSetGifVersion(gw->gif, true);

        if (GIF_OK != EGifPutScreenDesc(gw->gif,
            wr->w,  // TODO: option to specify screen size in advance?
            wr->h,
            calc_colour_res(gw->global_cm->ColorCount),
            0,  // Background colour.
            gw->global_cm))
        {
            // failed
            wr->err = translate_err(gw->gif->Error);
            return;
        }

        // Assume endlessly-looping gif.
        write_loops(gw->gif, 0);
    }

    // Now, write out the frame!

    // Has the palette changed?
    if (gw->local_cm) {
        GifFreeMapObject(gw->local_cm);
    }
    int trans = NO_TRANSPARENT_COLOR;
    gw->local_cm = cvt_palette(wr->pal_num_colours, wr->pal_data, &trans);
    if( gw->local_cm == NULL) {
        wr->err = IM_ERR_NO_PALETTE;
        return;
    }
    if (colormaps_equal(gw->global_cm, gw->local_cm)) {
        // No change. throw away the local cm.
        GifFreeMapObject(gw->local_cm);
        gw->local_cm = NULL;
    }

    // output a GCB block
    {
        uint8_t gcb_buf[4];
        GraphicsControlBlock gcb = {0};
        gcb.DisposalMode = DISPOSAL_UNSPECIFIED;
        gcb.UserInputFlag = false;
        gcb.DelayTime = 10; // in 0.01sec units
        gcb.TransparentColor = NO_TRANSPARENT_COLOR;    //trans;

        EGifGCBToExtension(&gcb, (GifByteType*)gcb_buf);

        if( EGifPutExtension(gw->gif, GRAPHICS_EXT_FUNC_CODE, 4, gcb_buf) != GIF_OK) {
            wr->err = translate_err(gw->gif->Error);
            return;
        }
    }

    if (GIF_OK != EGifPutImageDesc(
        gw->gif,
        wr->x_offset, wr->y_offset,
        wr->w, wr->h,
        false,  // interlace
        gw->local_cm))        // can be null.
    {
        wr->err = translate_err(gw->gif->Error);
        return;
    }
}


static void emit_rows(im_write *wr, unsigned int num_rows, const void *data, int stride)
{
    gif_writer* gw = (gif_writer*)wr;
    unsigned int i;
    uint8_t const* src = data;

    assert(wr->internal_fmt == IM_FMT_INDEX8);
    for (i = 0; i < num_rows; ++i) {
        if( GIF_OK !=EGifPutLine(gw->gif, (GifPixelType *)src, wr->w)) {
            wr->err = translate_err(gw->gif->Error);
            return;
        }
        src += stride;
    }
}


// Return true if a and b hold the same colour values.
static bool colormaps_equal(ColorMapObject const* a, ColorMapObject const* b)
{
    int n = a->ColorCount;
    if (n != b->ColorCount) {
        return false;
    }
    return (0 == memcmp(a->Colors, b->Colors, n * sizeof(GifColorType)));
}




static int output_fn( GifFileType *gif, const GifByteType *buf, int size)
{
    im_out* out = (im_out*)gif->UserData;
    return (int)im_out_write(out, (void*)buf, (size_t)size);
}



// convert raw RGB to gif colormap
static ColorMapObject* cvt_palette(unsigned int num_colours, const uint8_t *src, int* trans)
{
    ColorMapObject* cm;
    GifColorType* dest;
    int i;

    if (num_colours == 0) {
        return NULL;
    }
    cm = GifMakeMapObject(num_colours, NULL);
    if (!cm) {
        return NULL;
    }
    *trans = NO_TRANSPARENT_COLOR;  // -1

    dest = cm->Colors;
    for (i=0; i<num_colours; ++i) {
        uint8_t alpha;
        dest->Red = *src++;
        dest->Green = *src++;
        dest->Blue = *src++;
        ++dest;
        alpha = *src++;
        if (*trans == NO_TRANSPARENT_COLOR && alpha == 0) {
            *trans = i;
        }
    }
    return cm;
}

static ImErr translate_err(int gif_err_code)
{
    switch(gif_err_code) {
        case D_GIF_ERR_NOT_GIF_FILE: return IM_ERR_MALFORMED;
        case D_GIF_ERR_NOT_ENOUGH_MEM: return IM_ERR_NOMEM;
       //TODO:
        default: return IM_ERR_MALFORMED; 
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

static void finish(im_write* wr)
{
    gif_writer* gw = (gif_writer*)wr;

    if (gw->global_cm) {
        GifFreeMapObject(gw->global_cm);
        gw->global_cm = NULL;
    }
    if (gw->local_cm) {
        GifFreeMapObject(gw->local_cm);
        gw->local_cm = NULL;
    }

    if (gw->gif) {
        int giferr;
        if( GIF_OK != EGifCloseFile(gw->gif, &giferr) ) {
            if (wr->err == IM_ERR_NONE) {
                wr->err = translate_err(giferr);
            }
        }
        gw->gif = NULL;
    }
}


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

