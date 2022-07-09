#include "impy.h"
#include <stdio.h>


int main( int argc, char* argv[])
{
    ImErr err;
    if (argc<=1) {
        return 0;
    }

    im_reader* rdr = im_reader_open_file(argv[1], &err);
    if (!rdr) {
        fprintf(stderr,"open failed (err=%d)\n",err);
        return 1;
    }

    im_imginfo img;
    while (im_get_img(rdr, &img)) {
        printf("%dx%d\n", img.w, img.h);
    }

    err = im_reader_finish(rdr);
    if (err!= ERR_NONE) {
        fprintf(stderr,"cleanup failed (err=%d)\n",err);
    }
    return 0;
}

