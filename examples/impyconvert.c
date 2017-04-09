#include "impy.h"
#include <stdio.h>


int main( int argc, char* argv[])
{
    int quit = 0;
    if (argc<=1) {
        return 0;
    }

    if (argc<3) {
        fprintf(stderr, "usage: %s infile outfile\n", argv[0] );
        return 1;
    }

    ImErr err;
    im_bundle* bundle = im_bundle_load(argv[1], &err);
    if (bundle==NULL) {
        fprintf(stderr,"Load failed (ImErr %d)\n", err);
        return 1;
    }


    if (!im_bundle_save(bundle,argv[2], &err) ) {
        fprintf(stderr,"Save failed (ImErr %d)\n", err);
        return 1;
    }


    im_bundle_free(bundle);
    return 0;
}


