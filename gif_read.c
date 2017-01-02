#include "im.h"

#include <stdio.h>
#include <gif_lib.h>

// TODO: check GIFLIB_MAJOH version >= 5


struct readstate {
    GifFileType *gif;

    // the currently-active gcb data
    bool gcb_valid;
    GraphicsControlBlock gcb;

    int loop_count; // from NETSCAPE block
    // TODO: stash any comment extension records we encounter

    //
    im_bundle *bundle;
};



static im_Img* read_image(struct readstate *state);
static bool read_extension(struct readstate *state);
static im_Pal* build_palette(ColorMapObject* cm, int transparent_idx);

static int input_fn(GifFileType *gif, GifByteType *buf, int size)
{
    im_reader* rdr = (im_reader*)gif->UserData;
    return (int)im_read(rdr, (void*)buf, (size_t)size);
}


static ImErr translate_err( int gif_err_code )
{
    switch(gif_err_code) {
        case D_GIF_ERR_NOT_GIF_FILE: return ERR_MALFORMED;
        case D_GIF_ERR_NOT_ENOUGH_MEM: return ERR_NOMEM;
       //TODO:
        default: return ERR_MALFORMED; 
    }
}

bool isGif(const uint8_t* buf, int nbytes)
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

im_Img* readGif( im_reader* rdr )
{
    im_Img* img;
    im_bundle* b = multiReadGif(rdr);
    SlotID id = {0};
    if (!b) {
        return NULL;
    }
    img = im_bundle_get(b,id);
    // TODO: detach image and delete bundle!
    return img;
}

im_bundle* multiReadGif( im_reader* rdr )
{
    struct readstate state;
    int err;
    bool done=false;
    SlotID frame = {0};

    state.gcb_valid = false;
    state.gif = DGifOpen( (void*)rdr, input_fn, &err);
    state.bundle = im_bundle_new();
    if (!state.gif) {
        im_err(translate_err(err));
        goto bailout;
    }

    while(!done) {
        GifRecordType rec_type;
        if (DGifGetRecordType(state.gif, &rec_type) != GIF_OK) {
            im_err(ERR_MALFORMED);
            goto bailout;
        }
        switch(rec_type) {
            case UNDEFINED_RECORD_TYPE:
            case SCREEN_DESC_RECORD_TYPE:
                im_err(ERR_MALFORMED);
                goto bailout;
            case IMAGE_DESC_RECORD_TYPE:
                {
                    im_Img* img = read_image(&state);
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

    if(DGifCloseFile(state.gif, &err)!=GIF_OK) {
        state.gif = NULL;
        im_err(translate_err(err));
        goto bailout;
    }

    return state.bundle;

bailout:
    if (state.gif) {
        DGifCloseFile(state.gif, &err);
    }
    im_bundle_free(state.bundle);
    return NULL;
}


static im_Img* read_image( struct readstate* state )
{
    GifFileType* gif = state->gif;
    int y;
    int w,h;
    im_Img *img=NULL;

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

static im_Pal* build_palette(ColorMapObject* cm, int transparent_idx)
{
    im_Pal* pal;
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


