#include "impxy.h"
#include <stdio.h>


int main( int argc, char* argv[])
{
im_bundle* bundle = NULL;
    ImErr err;
    if (argc<=1) {
        return 0;
    }

    bundle = im_bundle_load(argv[1], &err);
    if (bundle==NULL) {
        fprintf(stderr,"load failed (err=%d)\n",err);
        return 1;
    }

    im_bundle_free(bundle);
    return 0;
}

