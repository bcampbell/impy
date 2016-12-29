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


    im_Img* img = loadPng(argv[1]);
    if (img==NULL) {
        fprintf(stderr,"Poop.\n");
        return 1;
    }


    im_writer* out = im_open_file_writer(argv[2]);
    if (out==NULL) {
        im_img_free(img);
        fprintf(stderr,"ERROR opening '%s'\n", argv[2]);
        return 1;
    }

    if(!writePng(out,img)) {
        im_img_free(img);
        fprintf(stderr,"ERROR writePng() failed\n");
        return 1;
    }

    im_img_free(img);
    return 0;
}


