#include "impy.h"
#include "private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <gif_lib.h>

// Imagemagick has some good details on gif animation:
// http://www.imagemagick.org/Usage/anim_basics/

// TODO: check GIFLIB_MAJOR version >= 5

static ImErr translate_err( int gif_err_code );

static bool is_gif(const uint8_t* buf, int nbytes);
static bool match_gif_ext(const char* file_ext);
static im_bundle* read_gif_bundle( im_reader* rdr, ImErr* err );
static bool write_gif_bundle(im_bundle* bundle, im_writer* out, ImErr* err);

struct handler handle_gif = {is_gif, NULL, read_gif_bundle, match_gif_ext, NULL, write_gif_bundle};

static ImErr translate_err( int gif_err_code )
{
    switch(gif_err_code) {
        case D_GIF_ERR_NOT_GIF_FILE: return ERR_MALFORMED;
        case D_GIF_ERR_NOT_ENOUGH_MEM: return ERR_NOMEM;
       //TODO:
        default: return ERR_MALFORMED; 
    }
}

/**************
 * GIF READING
 **************/

struct rect {
    int x,y,w,h;
};

struct readstate {
    GifFileType *gif;

    // the currently-active gcb data, if any
    bool gcb_valid;
    GraphicsControlBlock gcb;

    int loop_count; // from NETSCAPE block
    // TODO: stash any comment extension records we encounter

    //
    im_bundle *bundle;


    // buffer to hold one line (only used for transparency decode)
    uint8_t *linebuf;



    bool coalesce;
    im_img* accumulator;
    im_img* backup; // backup of accumulator, used for DISPOSE_BACKGROUND frames
    struct rect disposalrect;
    int disposal;
};

static bool readstate_cleanup( struct readstate* state, ImErr* err);
static bool process_image(struct readstate *state, ImErr* err);
static bool process_extension(struct readstate *state);
static bool apply_palette(GifFileType* gif, im_img* img,  int transparent_idx);
static bool decode( GifFileType* gif, im_img* destimg, int destx, int desty );
static bool decode_trns( struct readstate* state, im_img* destimg, int destx, int desty, uint8_t trns );
static void blit( const im_img* src, im_img* dest, int destx, int desty, int w, int h);
static void drawrect( im_img* img, int xo, int yo, int w, int h, uint8_t c);


static int input_fn(GifFileType *gif, GifByteType *buf, int size)
{
    im_reader* rdr = (im_reader*)gif->UserData;
    return (int)im_read(rdr, (void*)buf, (size_t)size);
}


// returns true if buf contains gif magic cookie ("GIF87a" or "GIF89a")
// (assumes buf contains at least 6 bytes)
static bool is_gif(const uint8_t* buf, int nbytes)
{
    //assert nbytes >= 6
    const char* p = (const char*)buf;
    return (
        p[0]=='G' &&
        p[1]=='I' &&
        p[2]=='F' &&
        p[3]=='8' &&
        (p[4]=='7' || p[4]=='9') &&
        p[5] == 'a');
}

// returns true if filename extenstion is ".gif" (case-insensitive)
static bool match_gif_ext(const char* file_ext)
{
    return (istricmp(file_ext,".gif")==0);
}


static im_bundle* read_gif_bundle( im_reader* rdr, ImErr* err )
{
    struct readstate state = {0};
    bool done=false;
    int giferr;
    im_bundle* out = NULL;

    state.coalesce = true;
    state.gif = DGifOpen( (void*)rdr, input_fn, &giferr);
    state.bundle = im_bundle_new();
    state.disposal = DISPOSAL_UNSPECIFIED;
    if (!state.gif) {
        *err = translate_err(giferr);
        goto bailout;
    }
    // we need a buffer big enough to decode a line
    state.linebuf = imalloc((size_t)state.gif->SWidth);
    if (!state.linebuf) {
        *err = ERR_NOMEM;
        goto bailout;
    }

    // if we're coalescing, then use an accumulator image big enough for the entire canvas
    if (state.coalesce) {
        int w = (int)state.gif->SWidth;
        int h = (int)state.gif->SHeight;
        state.accumulator = im_img_new(w,h,1, FMT_COLOUR_INDEX, DT_U8);
        if (!state.accumulator) {
            *err = ERR_NOMEM;
            goto bailout;
        }
        state.disposal = DISPOSAL_UNSPECIFIED;
        // clear it to the background colour
        drawrect( state.accumulator,0,0,w,h, (uint8_t)state.gif->SBackGroundColor);
    }


    while(!done) {
        GifRecordType rec_type;
        if (DGifGetRecordType(state.gif, &rec_type) != GIF_OK) {
            *err = ERR_MALFORMED;
            goto bailout;
        }
        switch(rec_type) {
            case UNDEFINED_RECORD_TYPE:
            case SCREEN_DESC_RECORD_TYPE:
                *err = ERR_MALFORMED;
                goto bailout;
            case IMAGE_DESC_RECORD_TYPE:
                if( !process_image(&state,err) ) {
                    goto bailout;
                }
                break;
            case EXTENSION_RECORD_TYPE:
                // TODO: set error code!
                if( !process_extension(&state) ) {
                    goto bailout;
                }
                break;
            case TERMINATE_RECORD_TYPE:
                done=true;
                break;
        }
    }

    // detach the bundle for returning
    out = state.bundle;
    state.bundle = NULL;
    if(!readstate_cleanup(&state, err)) {
        im_bundle_free(out);
        return NULL;
    }
    return out;

bailout:
    readstate_cleanup(&state, NULL);
    return NULL;
}


static bool readstate_cleanup( struct readstate* state, ImErr* err)
{
    if (state->backup) {
        im_img_free(state->backup);
    }
    if (state->accumulator) {
        im_img_free(state->accumulator);
    }

    if (state->linebuf) {
        ifree(state->linebuf);
    }
    if (state->bundle) {
        im_bundle_free(state->bundle);
    }
    if (state->gif) {
        int giferr;
        if(DGifCloseFile(state->gif, &giferr)!=GIF_OK) {
            *err = translate_err(giferr);
            return false;
        }
    }
    return true;
}





static bool process_image( struct readstate* state, ImErr* err )
{
    im_img* img = NULL;
    GifFileType* gif = state->gif;
    int trns = NO_TRANSPARENT_COLOR;
    if (state->gcb_valid) {
        trns = state->gcb.TransparentColor;
    }

    if (DGifGetImageDesc(gif) != GIF_OK) {
        *err = translate_err(gif->Error);
        return false;
    }

    if (state->coalesce) {
        int w = (int)gif->SWidth;
        int h = (int)gif->SHeight;
        int disposal = DISPOSAL_UNSPECIFIED;
        if (state->gcb_valid) {
            disposal = state->gcb.DisposalMode;
        }


        // apply frame disposal...
        if (im_bundle_num_frames(state->bundle) > 0) {
            switch (state->disposal) {
                case DISPOSAL_UNSPECIFIED:
                case DISPOSE_DO_NOT:
                    break;
                case DISPOSE_BACKGROUND:
                    {
                        struct rect *r = &state->disposalrect;
                        drawrect( state->accumulator,r->x, r->y, r->w, r->h, (uint8_t)gif->SBackGroundColor);
                    }
                    break;
                case DISPOSE_PREVIOUS:
                    // restore from backup
                    blit(state->backup, state->accumulator,0,0,(int)gif->SWidth,(int)gif->SHeight);
                    break;
            }
        }

        // back up the current accumulator if we'll need to roll back
        if (disposal == DISPOSE_PREVIOUS) {
            if (state->backup) {
                im_img_free(state->backup);
            }
            state->backup = im_img_clone(state->accumulator);
            if (!state->backup) {
                *err = ERR_NOMEM;
                return false;
            }
        }

        // decode the new frame into the accumulator
        if (trns == NO_TRANSPARENT_COLOR) {
            if (!decode(gif, state->accumulator, gif->Image.Left, gif->Image.Top)) {
                *err= translate_err(gif->Error);
                return false;
            }
        } else {
            if (!decode_trns(state, state->accumulator, gif->Image.Left, gif->Image.Top, (uint8_t)trns)) {
                *err= translate_err(gif->Error);
                return false;
            }
        }

        // update the palette
        if (!apply_palette(gif,state->accumulator, trns)) {
            *err= ERR_NOMEM;
            return false;
        }

        // remember what we need to do next time
        state->disposal = disposal;
        state->disposalrect.x = (int)gif->Image.Left;
        state->disposalrect.y = (int)gif->Image.Top;
        state->disposalrect.w = (int)gif->Image.Width;
        state->disposalrect.h = (int)gif->Image.Height;

        // take a copy of the accumulator for adding to bundle
        img = im_img_clone(state->accumulator);
        if (!img) {
            *err = ERR_NOMEM;
            return false;
        }

    } else {
        // load each frame as separate image
        img = im_img_new((int)gif->Image.Width, (int)gif->Image.Height,1,FMT_COLOUR_INDEX, DT_U8);
        if (!img) {
            *err = ERR_NOMEM;
            return false;
        }
        if (!decode(gif,img,0,0)) {
            *err = translate_err(gif->Error);
            im_img_free(img);
            return false;
        }

        if (!apply_palette(gif,img,trns)) {
            *err = ERR_NOMEM;
            im_img_free(img);
            return false;
        }

        img->x_offset = (int)gif->Image.Left;
        img->y_offset = (int)gif->Image.Top;
        // TODO: record disposal details here
    }

    // add image to the bundle
    {
        SlotID id = {0};
        id.frame = im_bundle_num_frames(state->bundle);
        im_bundle_set( state->bundle, id, img);
    }
    
    return true;
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


static bool decode_trns( struct readstate* state, im_img* destimg, int destx, int desty, uint8_t trns )
{
    GifFileType* gif = state->gif;
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
        for (pass=0; pass<4; ++pass) {
            for(y=offsets[pass]; y<h; y+= jumps[pass]) {
                uint8_t *buf = state->linebuf;
                uint8_t *dest = im_img_pos(destimg, destx+0, desty+y);
                if (DGifGetLine(gif, buf, w) != GIF_OK) {
                    return false;
                }
                for( x=0; x<w; ++x) {
                    uint8_t c = *buf++;
                    if (c!=trns) {
                        *dest = c;
                    }
                    ++dest;
                }
            }
        }
    } else {
        for (y=0; y<h; ++y) { 
            uint8_t *buf = state->linebuf;
            uint8_t *dest = im_img_pos(destimg, destx+0, desty+y);
            if (DGifGetLine(gif, buf, w) != GIF_OK) {
                return false;
            }
            for( x=0; x<w; ++x) {
                uint8_t c = *buf++;
                if (c!=trns) {
                    *dest = c;
                }
                ++dest;
            }
        }
    }
    return true;
}


static bool process_extension( struct readstate* state )
{
    GifFileType* gif = state->gif;
    GifByteType* buf;
    int ext_code;

    if (DGifGetExtension(gif, &ext_code, &buf) != GIF_OK) {
        return false;
    }

    if (ext_code==GRAPHICS_EXT_FUNC_CODE) {
        if(DGifExtensionToGCB(buf[0], buf+1, &state->gcb)!=GIF_OK) {
            return false;
        }
        state->gcb_valid = true;
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
    int x,y;
    size_t rowbytes = w*im_img_bytesperpixel(src);

    assert(destx >=0 && desty >=0);
    assert( w <= src->w && w <= dest->w);
    assert( h <= src->h && h <= dest->h);
    assert( destx+w <= dest->w);
    assert( desty+h <= dest->h);

    assert( src->format == dest->format);
    assert( src->datatype == dest->datatype);

    for (y=0; y<h; ++y) {
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

/***************
 * GIF WRITING
 ***************/
static bool write_loops(GifFileType*gif, int loops);
static bool write_frame( GifFileType* gif, im_bundle* bundle, int frame, im_img* first_frame, int global_trans, ImErr* err);

static int output_fn( GifFileType *gif, const GifByteType *buf, int size)
{
    im_writer* out = (im_writer*)gif->UserData;
    return (int)im_write(out, (void*)buf, (size_t)size);
}


static ColorMapObject *palette_to_cm( im_img* img, int *trans )
{

    ColorMapObject* cm;
    GifColorType* dest;
    const uint8_t* src;
    int i;
    int num_colours = img->pal_num_colours;

    // TODO:  GifMakeMapObject fails if colour count is not power-of-two.
    cm = GifMakeMapObject( num_colours, NULL );
    if (!cm) {
        return NULL;
    }
    
    src = img->pal_data;
    dest = cm->Colors;
    *trans = -1;
    switch (img->pal_fmt) {
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
                    if( alpha==0 && i>*trans) {
                        *trans = i;
                    }
                }
            }
            break;
        default:
            GifFreeMapObject(cm);
            return NULL;
    }
    return cm; 
}


// calc extent of all frames in a bundle
static bool calc_extents( im_bundle* b, int* min_x, int* min_y, int* max_x, int* max_y  ) {
    im_img* img;
    int n;
    int num_frames = im_bundle_num_frames(b);
    *min_x = INT_MAX;
    *min_y = INT_MAX;
    *max_x = INT_MIN;
    *max_y = INT_MIN;

    for (n=0; n<num_frames; ++n) {
        int x,y,w,h;
        img = im_bundle_get_frame(b,n);
        if (!img) {
            return false;
        }

        x = img->x_offset;
        y = img->y_offset;
        w = img->w;
        h = img->h;

        if (x < *min_x) {
            *min_x = x;
        } 
        if (y < *min_y) {
            *min_y = y;
        } 
        if ((x+w) > *max_x) {
            *max_x = x+w;
        } 
        if ((y+h) > *max_y) {
            *max_y = y+h;
        } 

    }

    return true;
}


static int calc_colour_res( int ncolours) {
    int i;
    for( i=1; i<=8; ++i) {
        if( ncolours <= (1<<i)) {
            return i;
        }
    }
    return 8;
}


static bool write_gif_bundle(im_bundle* bundle, im_writer* out, ImErr *err)
{
    GifFileType *gif = NULL;
    int giferr;
    im_img* first_img;
    int num_frames = im_bundle_num_frames(bundle);
    int min_x,min_y, max_x, max_y;
    ColorMapObject* global_cm = NULL;
    int global_trans = -1;
    int frame;

    // TODO: ensure all frames are indexed

    if (!num_frames) {
        *err = ERR_BADPARAM;
        return false;
    }

    if( !calc_extents( bundle, &min_x, &min_y, &max_x, &max_y) ) {
        return false;
    }

    // Use first image to provide global colormap.
    // Could be a more optimal palette, but this seems a reasonable way to go.
    first_img = im_bundle_get_frame(bundle, 0);
    if (!first_img) {
        *err = ERR_BADPARAM;
        return false;
    }
    global_cm = palette_to_cm( first_img, &global_trans);
    if (!global_cm) {
        *err = ERR_NOMEM;
        return false;
    }


    gif = EGifOpen((void*)out, output_fn, &giferr);
    if (!gif) {
        *err = translate_err(giferr);
        goto bailout;
    }

    // GIF89 - TODO: only if needed...
    EGifSetGifVersion(gif, true);

    if (GIF_OK != EGifPutScreenDesc(gif,
        max_x-min_x,
        max_y-min_y,
        calc_colour_res(global_cm->ColorCount),
        0,  // (bgcolor)
        global_cm))
    {
        *err = translate_err(gif->Error);
        goto bailout;
    }

    // output NETSCAPE application block, if needed for looping
    if (num_frames>1) {
        // TODO: allow non-infinite looping?
        if(!write_loops(gif,0)) {
            *err = translate_err(gif->Error);
            goto bailout;
        }
    }

    for( frame=0; frame<num_frames; ++frame) {
        if (!write_frame(gif, bundle, frame, first_img, global_trans, err ) ) {
            goto bailout;
        }
    }

    GifFreeMapObject(global_cm);
    if( GIF_OK != EGifCloseFile(gif, &giferr) ) {
        *err = translate_err(giferr);
        gif = NULL;
        goto bailout;
    }
    return true;

bailout:
    if (global_cm) {
        GifFreeMapObject(global_cm);
    }
    if (gif) {
        EGifCloseFile(gif, &giferr);
    }
    return false;
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


static bool write_frame( GifFileType* gif, im_bundle* bundle, int frame, im_img* first_frame, int global_trans, ImErr* err)
{
    im_img* img;
    int y;
    ColorMapObject* frame_cm = NULL;
    int trans = global_trans;
    int width,height;

    img = im_bundle_get_frame(bundle,frame);
    if(!img) {
        *err = ERR_BADPARAM;
        return false;
    }

    width = img->w;
    height = img->h;

    // sanity checking
    if( img->format != FMT_COLOUR_INDEX ) {
        *err = ERR_UNSUPPORTED;
        goto bailout;
    }

    // decide if frame requires its own palette
    if( !im_img_pal_equal(first_frame, img)) {
        frame_cm = palette_to_cm(img, &trans);
        if( !frame_cm) {
            *err = ERR_NOMEM;
            return false;
        }
    }

    // output a GCB block
    {
        uint8_t gcb_buf[4];
        GraphicsControlBlock gcb = {0};
        gcb.DisposalMode = DISPOSAL_UNSPECIFIED;
        gcb.UserInputFlag = false;
        gcb.DelayTime = 10; // in 0.01sec units
        gcb.TransparentColor = trans;

        EGifGCBToExtension(&gcb, (GifByteType*)gcb_buf);

        if( EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, gcb_buf) != GIF_OK) {
            *err = translate_err(gif->Error);
            return false;
        }
    }


    if (GIF_OK != EGifPutImageDesc(
        gif,
        img->x_offset, img->y_offset,
        width, height,
        false,  // interlace
        frame_cm))
    {
        if( frame_cm) {
            GifFreeMapObject(frame_cm);
        }
        *err = translate_err(gif->Error);
        goto bailout;
    }

    for (y=0; y<height; ++y) {
        if( GIF_OK !=EGifPutLine( gif, (GifPixelType*)im_img_row(img,y), width)) {
            *err = translate_err(gif->Error);
            goto bailout;
        }
    }
    if(frame_cm) {
        GifFreeMapObject(frame_cm);
    }
    return true;

bailout:
    if(frame_cm) {
        GifFreeMapObject(frame_cm);
    }
    return false;

}


