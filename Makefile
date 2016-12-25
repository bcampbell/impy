a.out: im.c im_convert.c im_png.c main.c im.h
	gcc -ggdb im.c im_convert.c im_png.c main.c `pkg-config libpng sdl2 --libs --cflags`

