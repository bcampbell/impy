#include "im.h"
#include <stdio.h>


im_Img* img = NULL;

void cleanup();
void show();

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


    im_bundle* bundle = im_bundle_load(argv[1]);
    if (bundle==NULL) {
        fprintf(stderr,"Poop.\n");
        return 1;
    }


    if(!im_bundle_save(bundle,argv[2]) ) {
        fprintf(stderr,"save failed.\n");
        return 1;
    }


    im_bundle_free(bundle);
    return 0;
}


