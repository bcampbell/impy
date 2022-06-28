#include "impy.h"
#include <stdio.h>


int main( int argc, char* argv[])
{
    im_bundle* bundle = NULL;
    ImErr err;
    int n;
    if (argc<=1) {
        return 0;
    }

    bundle = im_bundle_load(argv[1], &err);
    if (bundle==NULL) {
        fprintf(stderr,"load failed (err=%d)\n",err);
        return 1;
    }

    for (n=0; n<im_bundle_num_frames(bundle); ++n) {
        im_img* img = im_bundle_get_frame(bundle,n);
        printf("%d: ", n);
        if (!img) {
            printf("no image\n");
        } else {
            int w = img->w;
            int h = img->h;
            printf( "%dx%d\n", w,h );
        }
    }

    im_bundle_free(bundle);
    return 0;
}

