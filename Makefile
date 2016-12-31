LIBSRCS=im.c io.c bundle.c convert.c gif_read.c png_read.c png_write.c

view: $(LIBSRCS) im.h examples/view.c
	gcc -ggdb $(LIBSRCS) examples/view.c -I. -lgif `pkg-config libpng sdl2 --libs --cflags` -o $@ 

cvt: $(LIBSRCS) im.h examples/cvt.c
	gcc -ggdb $(LIBSRCS) examples/cvt.c -I. -lgif `pkg-config libpng --libs --cflags` -o $@ 

