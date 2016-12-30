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

void readGif( im_reader* rdr )
{
    GifFileType* gif;
    int err;
    bool done=false;

    gif = DGifOpen( (void*)rdr, input_fn, &err);
    if (!gif) {
        im_err(translate_err(err));
        return;
    }

    while(done) {
        bool done=false;
        GifRecordType rec_type;
        if (DGifGetRecordType(gif, &rec_type) == GIF_OK) {
            switch(rec_type) {
                case UNDEFINED_RECORD_TYPE:
                case SCREEN_DESC_RECORD_TYPE:
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
                    done=true;
                    break;
            }
        } else {
            im_err(ERR_MALFORMED);
            done=true;
        }
    }
}


static bool read_image( GifFileType* gif )
{
    int y;

    if (DGifGetImageDesc(gif) != GIF_OK) {
        return false;
    }

    printf("image (%dx%d)\n", gif->Image.Width, gif->Image.Height);

    for (y=0; y<gif->Image.Height; ++y) { 
        uint8_t buf[1024];
        //, PixelType *GifLine, int GifLineLen)
        if (DGifGetLine(gif, buf,sizeof(buf)) != GIF_OK) {
            return false;
        }
    }
    return true;
}

static bool read_extension( GifFileType* gif )
{
    GifByteType* buf;
    int code;

    if (DGifGetExtension(gif, &code, &buf) != GIF_OK) {
        return false;
    }
    printf("ext (%d)\n",code);

    while (1) {
        if (DGifGetExtensionNext(gif,&buf) != GIF_OK) {
            return false;
        }
        if (buf==NULL) {
            break;
        }
        printf("  extnext\n");
    }
    return true;
}

