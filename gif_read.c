#include "im.h"

#include <stdio.h>
#include <gif_lib.h>

// TODO: check GIFLIB_MAJOH version >= 5
//
//
static bool read_image( GifFileType* gif );
static bool read_extension( GifFileType* gif );

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
    GifFileType* gif;
    int err;
    bool done=false;

    gif = DGifOpen( (void*)rdr, input_fn, &err);
    if (!gif) {
        im_err(translate_err(err));
        return NULL;
    }

    while(!done) {
        GifRecordType rec_type;
        if (DGifGetRecordType(gif, &rec_type) == GIF_OK) {
            switch(rec_type) {
                case UNDEFINED_RECORD_TYPE:
                case SCREEN_DESC_RECORD_TYPE:
                    printf("fook\n");
                    im_err(ERR_MALFORMED);
                    done=true;
                    break;
                case IMAGE_DESC_RECORD_TYPE:
                    if(!read_image(gif)) {
                        done=true;
                    }
                    break;
                case EXTENSION_RECORD_TYPE:
                    read_extension(gif);
                    break;
                case TERMINATE_RECORD_TYPE:
                    printf("TERMINATE_RECORD_TYPE\n");
                    done=true;
                    break;
            }
        } else {
            im_err(ERR_MALFORMED);
            done=true;
        }
    }

    if(DGifCloseFile(gif, &err)!=GIF_OK) {
        // TODO: clean up!
        im_err(translate_err(err));
    }
    return NULL;
}


static bool read_image( GifFileType* gif )
{
    int y;

    if (DGifGetImageDesc(gif) != GIF_OK) {
        return false;
    }

    printf("image (%dx%d)\n", gif->Image.Width, gif->Image.Height);

    if (gif->Image.Interlace) {
        uint8_t buf[1024];

        // GIF interlacing stores the lines in the order:
        // 0, 8, 16, ...(8n)
        // 4, 12, ...(8n+4)
        // 2, 6, 10, 14, ...(4n+2)
        // 1, 3, 5, 7, 9, ...(2n+1).
        int offsets[4] = {0,4,2,1};
        int jumps[4] = {8,8,4,2};
        int pass;
        for (pass=0; pass<4; ++pass) {
            for(y=offsets[pass]; y<gif->Image.Height; y+= jumps[pass]) {
                if (DGifGetLine(gif, buf, gif->Image.Width) != GIF_OK) {
                    return false;
                }
            }
        }
    } else {
        for (y=0; y<gif->Image.Height; ++y) { 
            uint8_t buf[1024];
            //, PixelType *GifLine, int GifLineLen)
            if (DGifGetLine(gif, buf,gif->Image.Width) != GIF_OK) {
                return false;
            }
        }
    }
    return true;
}

static bool read_extension( GifFileType* gif )
{
    GifByteType* buf;
    int ext_code;

    if (DGifGetExtension(gif, &ext_code, &buf) != GIF_OK) {
        return false;
    }

    if (ext_code==GRAPHICS_EXT_FUNC_CODE) {
        GraphicsControlBlock gcb;
        if(DGifExtensionToGCB(buf[0], buf+1, &gcb)!=GIF_OK) {
            return false;
        }
        printf("ext (GCB) disposal=%d delay=%d trans=%d\n",
                gcb.DisposalMode, gcb.DelayTime, gcb.TransparentColor );
    } else if (ext_code==APPLICATION_EXT_FUNC_CODE) {
        printf("ext (0xff - application) %d bytes: '%c%c%c%c%c%c%c%c'\n",
             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
    } else {
        printf("ext (0x%02x)\n",ext_code);
    }

    while (1) {
        if (DGifGetExtensionNext(gif,&buf) != GIF_OK) {
            return false;
        }
        if (buf==NULL) {
            printf("  endext\n");
            break;
        }
        // TODO: collect comment blocks here
        printf("  extnext (%d bytes)\n", buf[0]);
    }
    return true;
}

