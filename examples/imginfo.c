#include "impy.h"
#include <stdio.h>
#include <stdlib.h>

int main( int argc, char* argv[])
{
    ImErr err;
    if (argc<=1) {
        return 0;
    }

    im_read* rdr = im_read_open_file(argv[1], &err);
    if (!rdr) {
        fprintf(stderr,"open failed (err=%d)\n",err);
        return 1;
    }

    im_imginfo img;
    while (im_read_img(rdr, &img)) {
        printf("%dx%d\n", img.w, img.h);
        if (img.pal_num_colours > 0) {
            printf("Palette - %d colours\n", img.pal_num_colours);
        }
   
        // Discard the image data.
        {
            size_t bytes_per_row = img.w * im_fmt_bytesperpixel(img.fmt);
            uint8_t *buf = malloc(bytes_per_row);
            unsigned int y;
            for (y = 0; y < img.h; ++y) {
                im_read_rows(rdr, 1, buf, bytes_per_row);
            }
            free(buf);
        }

        const im_kv *kv;
        for (kv = im_read_kv(rdr); kv->key; ++kv) {
            printf("%s: \"%s\"\n", kv->key, kv->value);
        }
    }

    err = im_read_finish(rdr);
    if (err!= IM_ERR_NONE) {
        fprintf(stderr,"failed (err=%d)\n",err);
    }
    return 0;
}

