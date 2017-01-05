#include "im.h"
#include "private.h"

#include <stdio.h>
#include <limits.h>
#include <gif_lib.h>

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

struct readstate {
    GifFileType *gif;

    // the currently-active gcb data, if any
    bool gcb_valid;
    GraphicsControlBlock gcb;

    int loop_count; // from NETSCAPE block
    // TODO: stash any comment extension records we encounter

    //
    im_bundle *bundle;
};

static im_img* read_image(struct readstate *state);
static bool read_extension(struct readstate *state);
static im_pal* build_palette(ColorMapObject* cm, int transparent_idx);


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


static im_bundle* read_gif_bundle( im_reader* rdr, ImErr *err )
{
    struct readstate state;
    int giferr;
    bool done=false;
    SlotID frame = {0};

    state.gcb_valid = false;
    state.gif = DGifOpen( (void*)rdr, input_fn, &giferr);
    state.bundle = im_bundle_new();
    if (!state.gif) {
        *err = translate_err(giferr);
        goto bailout;
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
                {
                    im_img* img = read_image(&state);
                    if (!img) {
                        goto bailout;
                    }
                    if( !im_bundle_set(state.bundle, frame, img) ) {
                        goto bailout;
                    }
                    ++frame.frame;
                }
                break;
            case EXTENSION_RECORD_TYPE:
                if( !read_extension(&state) ) {
                    goto bailout;
                }
                break;
            case TERMINATE_RECORD_TYPE:
                done=true;
                break;
        }
    }

    if(DGifCloseFile(state.gif, &giferr)!=GIF_OK) {
        state.gif = NULL;
        *err = translate_err(giferr);
        goto bailout;
    }

    return state.bundle;

bailout:
    if (state.gif) {
        DGifCloseFile(state.gif, &giferr);
    }
    im_bundle_free(state.bundle);
    return NULL;
}


static im_img* read_image( struct readstate* state )
{
    GifFileType* gif = state->gif;
    int y;
    int w,h;
    im_img *img=NULL;

    if (DGifGetImageDesc(gif) != GIF_OK) {
        // TODO: set error?
        goto bailout;
    }

    w = gif->Image.Width;
    h = gif->Image.Height;

    img = im_img_new(w,h,1,FMT_COLOUR_INDEX, DT_U8);
    //printf("image (%dx%d)\n", gif->Image.Width, gif->Image.Height);

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
                uint8_t *dest = im_img_row(img,y);
                if (DGifGetLine(gif, dest, w) != GIF_OK) {
                    // TODO: set error?
                    goto bailout;
                }
            }
        }
    } else {
        for (y=0; y<h; ++y) { 
            uint8_t *dest = im_img_row(img,y);
            if (DGifGetLine(gif, dest, w) != GIF_OK) {
                // TODO: set error?
                goto bailout;
            }
        }
    }

    // TODO: set GCB stuff here - delay, disposition etc
    //
    if (gif->Image.ColorMap) {
        img->Palette = build_palette(gif->Image.ColorMap,-1);
    } else if (gif->SColorMap) {
        img->Palette = build_palette(gif->SColorMap,-1);
    } else {
        // TODO: it's valid (but bonkers) for gif files to have no palette
    }

    return img;

bailout:
    if (img) {
        im_img_free(img);
    }
    return NULL;
}

static bool read_extension( struct readstate* state )
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

static im_pal* build_palette(ColorMapObject* cm, int transparent_idx)
{
    im_pal* pal;
    uint8_t* dest;
    int i;
    if (transparent_idx==-1) {
        pal = im_pal_new( PALFMT_RGB, cm->ColorCount );
        if( !pal) {
            return NULL;
        }
        dest = pal->Data;
        for (i=0; i<cm->ColorCount; ++i) {
            GifColorType *src = &cm->Colors[i];
            *dest++ = src->Red;
            *dest++ = src->Green;
            *dest++ = src->Blue;
        }
    } else {
        pal = im_pal_new( PALFMT_RGBA, cm->ColorCount );
        if( !pal) {
            return NULL;
        }
        dest = pal->Data;
        for (i=0; i<cm->ColorCount; ++i) {
            GifColorType *src = &cm->Colors[i];
            *dest++ = src->Red;
            *dest++ = src->Green;
            *dest++ = src->Blue;
            *dest++ = (transparent_idx == i) ? 0 : 255;
        }
    }
    return pal;
}


/***************
 * GIF WRITING
 ***************/
static bool write_loops(GifFileType*gif, int loops);
static bool write_frame( GifFileType* gif, im_bundle* bundle, int frame, im_pal* first_palette, int global_trans, ImErr* err);

static int output_fn( GifFileType *gif, const GifByteType *buf, int size)
{
    im_writer* out = (im_writer*)gif->UserData;
    return (int)im_write(out, (void*)buf, (size_t)size);
}


static ColorMapObject *palette_to_cm( im_pal* pal, int *trans )
{

    ColorMapObject* cm;
    GifColorType* dest;
    const uint8_t* src;
    int i;

    // TODO:  GifMakeMapObject fails if colour count is not power-of-two.
    cm = GifMakeMapObject( pal->NumColours, NULL );
    if (!cm) {
        return NULL;
    }
    
    src = pal->Data;
    dest = cm->Colors;
    *trans = -1;
    switch (pal->Format) {
        case PALFMT_RGB:
            for (i=0; i<pal->NumColours; ++i) {
                dest->Red = *src++;
                dest->Green = *src++;
                dest->Blue = *src++;
                ++dest;
            }
            break;
        case PALFMT_RGBA:
            {
                uint8_t alpha;
                for (i=0; i<pal->NumColours; ++i) {
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
        img = im_bundle_get_frame(b,n);
        if (!img) {
            return false;
        }
        if( img->XOffset < *min_x) {
            *min_x = img->XOffset;
        } 
        if( img->YOffset < *min_y) {
            *min_y = img->YOffset;
        } 
        if( img->XOffset+ img->Width > *max_x) {
            *max_x = img->XOffset + img->Width;
        } 
        if( img->YOffset+ img->Height > *max_y) {
            *max_y = img->YOffset + img->Height;
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
    global_cm = palette_to_cm( first_img->Palette, &global_trans);
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
        if (!write_frame(gif, bundle, frame, first_img->Palette, global_trans, err ) ) {
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


static bool write_frame( GifFileType* gif, im_bundle* bundle, int frame, im_pal* first_palette, int global_trans, ImErr* err)
{
    im_img* img;
    int y;
    ColorMapObject* frame_cm = NULL;
    int trans = global_trans;

    img = im_bundle_get_frame(bundle,frame);
    if(!img) {
        *err = ERR_BADPARAM;
        return false;
    }

    // sanity checking
    if( img->Format != FMT_COLOUR_INDEX ) {
        *err = ERR_UNSUPPORTED;
        goto bailout;
    }
    if( !img->Palette ) {
        *err = ERR_BADPARAM;
        goto bailout;
    }


    // decide if frame requires its own palette
    if( !im_pal_equal(first_palette, img->Palette)) {
        frame_cm = palette_to_cm(img->Palette, &trans);
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
        img->XOffset, img->YOffset,
        img->Width, img->Height,
        false,  // interlace
        frame_cm))
    {
        if( frame_cm) {
            GifFreeMapObject(frame_cm);
        }
        *err = translate_err(gif->Error);
        goto bailout;
    }

    for (y=0; y<img->Height; ++y) {
        if( GIF_OK !=EGifPutLine( gif, (GifPixelType*)im_img_row(img,y), img->Width)) {
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


