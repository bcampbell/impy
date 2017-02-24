#include "impy.h"
#include <stdio.h>

#include <SDL.h>
#include <SDL_surface.h>

SDL_Window *win = NULL;
SDL_Renderer *ren = NULL;
im_bundle* bundle = NULL;
SlotID cur = {0};
SDL_Texture *tex = NULL;
int scale = 1;

void cleanup();
void show();
bool set_frame(int n);
void prev_frame();
void next_frame();


int main( int argc, char* argv[])
{
    int quit = 0;
    ImErr err;
    if (argc<=1) {
        return 0;
    }

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) != 0 ) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return 1;
    }

    win = SDL_CreateWindow("Hello World!", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
	    return 1;
    }

    ren = SDL_CreateRenderer(win, -1, 0); //SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (ren == 0){
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
    	return 1;
    }

    bundle = im_bundle_load(argv[1], &err);
    if (bundle==NULL) {
        fprintf(stderr,"load failed (err=%d)\n",err);
        cleanup();
        return 1;
    }


    if(!set_frame(0) ) {
        fprintf(stderr,"set_frame failed.\n");
        cleanup();
        return 1;
    }

    show();
    while (!quit) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) {
            fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
            cleanup();
            return 1;
        }
        switch (ev.type) {
            case SDL_QUIT:
            {
                quit = 1;
                break;
            }
            case SDL_KEYDOWN:
            {
                switch (ev.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE: quit = 1; break;
                    case SDL_SCANCODE_1: scale = 1; break;
                    case SDL_SCANCODE_2: scale = 2; break;
                    case SDL_SCANCODE_3: scale = 4; break;
                    case SDL_SCANCODE_4: scale = 8; break;
                    case SDL_SCANCODE_5: scale = 16; break;
                    case SDL_SCANCODE_LEFT: prev_frame(); break;
                    case SDL_SCANCODE_RIGHT: next_frame(); break;
                }
                show();
                break;
            }
        }
    }

    cleanup();
    return 0;
}


static SDL_Texture* to_texture(im_img* img)
{
    SDL_Surface* surf;
    SDL_Texture *t;
    im_img* img2;

    img2 = im_img_convert(img, FMT_RGB, DT_U8);
    if (img2==NULL) {
        fprintf(stderr,"Poop.\n");
        return NULL;
    }

    // bmp shares the pixel data (it doesn't take it's own copy)
    // little-endian assumption...
    surf = SDL_CreateRGBSurfaceFrom(im_img_row(img2,0),
            im_img_w(img2), im_img_h(img2), 24, im_img_pitch(img2), 0x0000ff,0x00ff00,0xff0000,0);
    if (!surf) {
        im_img_free(img2);
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return NULL;
    }

    t = SDL_CreateTextureFromSurface(ren, surf);
    im_img_free(img2);
    SDL_FreeSurface(surf);

    return t;
}


bool set_frame(int n)
{
    SlotID id = {0};
    SDL_Texture* t;
    im_img* img;

    id.frame = n;

    img = im_bundle_get(bundle, id);
    if( img == NULL ) {
        return false;
    }

    t = to_texture(img);
    if( t== NULL) {
        return false;
    }

    tex = t;
    cur = id;
    return true;
}


void prev_frame()
{
    int nframes = im_bundle_num_frames(bundle);
    int n = cur.frame - 1;
    if(n < 0 ) {
        n = nframes-1;
    }
    set_frame(n);
    show();
}

void next_frame()
{
    int nframes = im_bundle_num_frames(bundle);
    int n = cur.frame + 1;
    if(n >= nframes) {
        n = 0;
    }
    set_frame(n);
    show();
}



void show()
{
    int winw, winh;
    int texw, texh;
    SDL_Rect src, dest;

    SDL_GetWindowSize(win, &winw, &winh);

    if (SDL_QueryTexture(tex,NULL,NULL,&texw, &texh) != 0 ) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return;
    }

	SDL_RenderClear(ren);
    dest.w = texw*scale;
    dest.h = texh*scale;
    dest.x = (winw-dest.w)/2;
    dest.y = (winh-dest.h)/2;
	SDL_RenderCopy(ren, tex, NULL, &dest);
	SDL_RenderPresent(ren);
    
}




void cleanup()
{
    if (tex) {
        SDL_DestroyTexture(tex);
    }
    if (bundle) {
        im_bundle_free(bundle);
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





