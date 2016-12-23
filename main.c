// gcc -ggdb im.c im_png.c main.c `pkg-config libpng sdl2 --libs --cflags`

#include "im.h"
#include <stdio.h>

#include <SDL.h>
#include <SDL_surface.h>

SDL_Window *win = NULL;
SDL_Renderer *ren = NULL;
im_Img* img = NULL;
SDL_Surface *bmp = NULL;
SDL_Texture *tex = NULL;

void cleanup();

int main( int argc, char* argv[])
{
    if (argc<=1) {
        return 0;
    }

    if(SDL_Init(SDL_INIT_VIDEO) != 0 ) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Hello World!", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
	    return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (ren == 0){
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
    	return 1;
    }

    im_Img *img = loadPng(argv[1]);
    if (img==NULL) {
        fprintf(stderr,"Poop.\n");
        cleanup();
        return 1;
    }

/*    SDL_Surface *bmp = SDL_LoadBMP(imagePath.c_str());
    if (bmp == nullptr){
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
        return 1;
    }
*/

    // bmp shares the pixel data (it doesn't take it's own copy)
    bmp = SDL_CreateRGBSurfaceFrom(img->Data,
            img->Width, img->Height, 24, img->Pitch, 0xff0000,0x00ff00,0x0000ff,0);
    if (!bmp) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
        return 1;
    }
    

    printf("%dx%d\n", img->Width, img->Height);

    tex = SDL_CreateTextureFromSurface(ren, bmp);
    if (tex == 0 ){
        cleanup();
    	return 1;
    }
    SDL_FreeSurface(bmp);
    bmp=NULL;


for (int i = 0; i < 3; ++i){
	//First clear the renderer
	SDL_RenderClear(ren);
	//Draw the texture
	SDL_RenderCopy(ren, tex, NULL, NULL);
	//Update the screen
	SDL_RenderPresent(ren);
	//Take a quick break after all that hard work
	SDL_Delay(1000);
}
    cleanup();
    return 0;
}

void cleanup()
{
    if (tex) {
        SDL_DestroyTexture(tex);
    }
    if (bmp) {
        SDL_FreeSurface(bmp);
    }
    if (img) {
        im_img_free(img);
    }
    if (ren) {
        SDL_DestroyRenderer(ren);
    }
    if (win) {
        SDL_DestroyWindow(win);
    }
    SDL_Quit();
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





