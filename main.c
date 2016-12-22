// gcc -ggdb im.c im_png.c main.c `pkg-config libpng --libs --cflags`

#include "im.h"
#include <stdio.h>


int main( int argc, char* argv[])
{
    if (argc>1) {
        im_Img *img = loadPng(argv[1]);
        if (img==NULL) {
            printf("Poop.\n");
            return 1;
        }


        printf("%dx%d\n", img->Width, img->Height);
    }

}
/*


struct im_readtarget {
    void* user;
    void* data;
    int pitch;
    int status;
};


struct im_inf {
    int index,frame,mipmap,face,layer;
    int width;
    int height;
    int depth;
    int fmt;
    int bytesperpixel;
    int type;
};


void begin_img(im_inf* inf, im_readtarg* out) {
    if inf.index!=0 {
        out->status = IM_SKIP;
    }

    surf = SDL_CreateRGBSurface(inf->w, inf->h);
    out->data = surf.Pixels;
    out->pitch = surf.Pitch;

    // TODO: check pixel formats here
    return IM_OK;
}

int set_palette(void* user, void* colours, int ncolours)
{
    // add palette to user struct
}



void free_img(void* user)
{
    //
}


*/





