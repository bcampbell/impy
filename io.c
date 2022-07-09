#include "impy.h"
#include "private.h"
#include <stdio.h>


struct file_in {
    im_in base;
    FILE* fp;
};

struct file_out {
    im_out base;
    FILE* fp;
};



static size_t file_in_read(im_in* r, void* buf, size_t nbytes)
{
    struct file_in *frdr = (struct file_in*)r;
    int ret = fread(buf,1,nbytes,frdr->fp);
    if (ret<0) {
        // TODO: translate errno
        //im_err(ERR_FILE);
    }
    return ret;
}


static int file_in_seek(im_in* r, long pos, int whence)
{
    struct file_in *fr = (struct file_in*)r;
    int w;
    int ret;
    switch(whence) {
        case IM_SEEK_SET: w = SEEK_SET; break;
        case IM_SEEK_CUR: w = SEEK_CUR; break;
        case IM_SEEK_END: w = SEEK_END; break;
        default:
            //im_err(ERR_BADPARAM);
            return -1;
    }
    ret = fseek(fr->fp,pos,w);
    if (ret<0) {
        // TODO: translate errno
        // im_err(ERR_FILE);
    }
    return ret;
}

static int file_in_tell(im_in* r)
{
    struct file_in *fr = (struct file_in*)r;
    return ftell(fr->fp);
}


static int file_in_eof(im_in* r)
{
    struct file_in *fr = (struct file_in*)r;
    return feof(fr->fp);
}

static int file_in_error(im_in* r)
{
    struct file_in *fr = (struct file_in*)r;
    return ferror(fr->fp);
}


static int file_in_close(im_in* r)
{
    struct file_in *fr = (struct file_in*)r;
    if( fclose(fr->fp) == 0 ) {
        return 0;
    } else {
        // TODO: translate errno?
        // im_err(ERR_FILE);
        return -1;
    }
}


im_in* im_in_open_file(const char *filename, ImErr *err)
{
    struct file_in *rdr = NULL;

    rdr = imalloc(sizeof(struct file_in));
    if (!rdr) {
        *err = ERR_NOMEM;
        return NULL;
    }

    rdr->fp = fopen(filename,"rb");
    if (!rdr->fp) {
        ifree(rdr);
        *err = ERR_FILE;    // TODO: translate errno
        return NULL;
    }

    rdr->base.read = file_in_read;
    rdr->base.seek = file_in_seek;
    rdr->base.tell = file_in_tell;
    rdr->base.eof = file_in_eof;
    rdr->base.error = file_in_error;
    rdr->base.close = file_in_close;
    return (im_in*)rdr;
}

int im_in_close(im_in* in) {
    int ret = in->close(in);
    ifree(in);
    return ret;
}


static size_t file_out_write(im_out* w, const void* buf, size_t nbytes)
{
    struct file_out *fw = (struct file_out*)w;
    int ret = fwrite(buf,1,nbytes,fw->fp);
    if (ret<0) {
        //*err = ERR_FILE;    // TODO: translate errno
    }
    return ret;
}

static int file_out_close(im_out* w)
{
    struct file_out *fw = (struct file_out*)w;
    if( fclose(fw->fp) == 0 ) {
        return 0;
    } else {
        return -1;
    }
}

im_out* im_out_open_file(const char *filename, ImErr *err)
{
    struct file_out *w = NULL;

    w = imalloc(sizeof(struct file_out));
    if (!w) {
        *err = ERR_NOMEM;
        return NULL;
    }

    w->fp = fopen(filename,"wb");
    if (!w->fp) {
        ifree(w);
        *err = ERR_FILE;    // TODO: translate errno
        return NULL;
    }

    w->base.write = file_out_write;
    w->base.close = file_out_close;
    return (im_out*)w;
}

int im_out_close(im_out* w)
{
    int ret = w->close(w);
    ifree(w);
    return ret;
}

