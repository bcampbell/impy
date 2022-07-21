#include "impy.h"
#include <stdio.h>

#include <SDL.h>
#include <SDL_surface.h>

typedef struct bundle {
    int len;
    SDL_Texture** frames; 
} bundle;


SDL_Window *win = NULL;
SDL_Renderer *ren = NULL;
bundle *doc = NULL;    // The loaded frame(s).
SDL_Texture *bg = NULL; // background texture

void show(SDL_Texture* tex);
void cleanup();
bundle* bundle_load(SDL_Renderer *renderer, const char *filename);
void bundle_free(bundle *b);
SDL_Texture* checkerboard(SDL_Renderer* renderer);

int main( int argc, char* argv[])
{
    int quit = 0;
    int frameidx=0;
    if (argc<=1) {
        return 0;
    }

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) != 0 ) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return 1;
    }

    win = SDL_CreateWindow("ImpyView", 100, 100, 640, 480, SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
	    return 1;
    }

    ren = SDL_CreateRenderer(win, -1, 0); //SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren){
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        cleanup();
    	return 1;
    }

    // This applies at texture creation time... so we'd have to reload textures
    // to switch on the fly.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");  // "linear", "best"

    bg = checkerboard(ren);

    doc = bundle_load(ren, argv[1]);
    if (doc==NULL || doc->len==0) {
        return 1;
    }

    show(doc->frames[frameidx]);
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
            case SDL_WINDOWEVENT:
            {
                if( ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    show(doc->frames[0]);
                }
                break;
            }
            case SDL_KEYDOWN:
            {
                switch (ev.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE: quit = 1; break;
                    //case SDL_SCANCODE_1: scale = 1; break;
                    //case SDL_SCANCODE_2: scale = 2; break;
                    //case SDL_SCANCODE_3: scale = 4; break;
                    //case SDL_SCANCODE_4: scale = 8; break;
                    //case SDL_SCANCODE_5: scale = 16; break;
                    case SDL_SCANCODE_LEFT:
                        if (frameidx>0) {
                            --frameidx;
                        } else {
                            frameidx = doc->len-1;
                        }
                        break;
                    case SDL_SCANCODE_RIGHT:
                        if (frameidx<doc->len-1) {
                            ++frameidx;
                        } else {
                            frameidx = 0;
                        }
                        break;
                    default: break;
                }
                show(doc->frames[frameidx]);
                break;
            }
        }
    }

    cleanup();
    return 0;
}



// Redraw the window.
void show(SDL_Texture* tex)
{
    int texw, texh;
    int bgw, bgh;
    if (SDL_QueryTexture(tex,NULL,NULL,&texw, &texh) != 0 ) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return;
    }
    if (SDL_QueryTexture(bg,NULL,NULL,&bgw, &bgh) != 0 ) {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError() );
        return;
    }
    SDL_SetRenderDrawColor(ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderClear(ren);

    // draw background (scale fixed)
    SDL_Rect vp;
    SDL_RenderSetViewport(ren, NULL);
    SDL_RenderSetScale(ren, 1.0f, 1.0f);
    SDL_RenderGetViewport(ren, &vp);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    int x, y;
    for (y = vp.y; y < vp.y + vp.h; y += bgh) {
        for (x = vp.x; x < vp.x + vp.w; x += bgw) {
            SDL_Rect dest = {x, y, bgw, bgh};
	        SDL_RenderCopy(ren, bg, NULL, &dest);
        }
    }

    // blend main image on top (scaled to fill window)
    SDL_RenderSetLogicalSize(ren, texw, texh);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
	SDL_RenderCopy(ren, tex, NULL, NULL);
	SDL_RenderPresent(ren);
}

void cleanup()
{
    if (doc) {
        bundle_free(doc);
    }
    if (bg) {
        SDL_DestroyTexture(bg);
    }
    if (ren) {
        SDL_DestroyRenderer(ren);
    }
    if (win) {
        SDL_DestroyWindow(win);
    }
    SDL_Quit();
}


// Generate a checkerboard texture for use as a background
SDL_Texture* checkerboard(SDL_Renderer* renderer)
{
    const int w = 256;
    const int h = 256;
    const int s = 16;   // Size of each square.
    int x,y;
    SDL_Texture* tx = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);

    SDL_SetRenderTarget(renderer, tx);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    for (y = 0; y < h/s; ++y) {
        for (x = 0; x < w/s; ++x) {
            SDL_Rect r = { x*s, y*s, s, s};
            SDL_RenderFillRect(renderer, &r);
            if ((x&1) ^ (y&1)) {
                SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
            } else {
                SDL_SetRenderDrawColor(renderer, 0xCC, 0xCC, 0xCC, 0xFF);
            }
            SDL_RenderFillRect(renderer, &r);
        }
    }
    SDL_SetRenderTarget(renderer, NULL);
    return tx;
}



bundle* bundle_load(SDL_Renderer *renderer, const char *filename)
{
    ImErr err;
    bundle* doc = malloc(sizeof(bundle));
    SDL_Surface* surf = NULL;
    doc->len = 0;
    doc->frames = realloc(NULL, sizeof(SDL_Texture*) * 8);

    im_reader *rdr;
    rdr = im_reader_open_file(filename, &err);
    if (!rdr) {
        goto bailout;
    }
    im_imginfo info;
    while (im_get_img(rdr, &info)) {
        int d = 0;
        Uint32 f = 0;
        // let impy sort out pixelformat and palettes and just give us RGBA
        im_reader_set_fmt(rdr, IM_FMT_RGBA);
        d = 32;
        f = SDL_PIXELFORMAT_RGBA32;
        surf = SDL_CreateRGBSurfaceWithFormat(0, info.w, info.h, d, f);
        if (!surf) {
            goto bailout;
        }
#if 0
        // Load in the palette, if any.
        if (info.pal_num_colours>0 && surf->format->palette) {
            SDL_Color tmp[256];
            uint8_t buf[256*4];
            int ncol = info.pal_num_colours;
            if (ncol>256) {
                ncol=256;
            }
            im_read_palette(rdr, IM_FMT_RGBA, buf);
            uint8_t *p = buf;
            for (int i=0; i<ncol; ++i) {
                tmp[i].r = *p++;
                tmp[i].g = *p++;
                tmp[i].b = *p++;
                tmp[i].a = *p++;
            }
            SDL_SetPaletteColors(surf->format->palette, tmp, 0, ncol);
        }
#endif

        //printf("w=%d d=%d surf->pitch=%d\n", info.w, d, surf->pitch);
        im_read_rows(rdr, info.h, surf->pixels);

        SDL_Texture* t = SDL_CreateTextureFromSurface(ren, surf);
        if (!t) {
            goto bailout;
        }
        doc->frames = realloc(doc->frames, (doc->len+1) * sizeof(SDL_Texture*));
        if (!doc->frames) {
            goto bailout;
        }
        doc->frames[doc->len] = t;
        doc->len++;
        SDL_FreeSurface(surf);
    }
    err = im_reader_finish(rdr);
    rdr = NULL;

    if (err!= ERR_NONE) {
        goto bailout;
    }
    return doc;

bailout:
    if (rdr) {
        im_reader_finish(rdr);
    }
    if (surf) {
        SDL_FreeSurface(surf);
    }
    if (doc) {
        bundle_free(doc);
    }
    return NULL;
}

void bundle_free(bundle *b)
{
    int i;
    for (i = 0; i < b->len; ++i) {
        SDL_DestroyTexture(b->frames[i]);
    }
    free(b->frames);
    free(b);
}


