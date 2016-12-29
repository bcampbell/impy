LIBSRCS=im.c io.c convert.c png_read.c png_write.c

view: $(LIBSRCS) im.h examples/view.c
	gcc -ggdb $(LIBSRCS) examples/view.c -I. `pkg-config libpng sdl2 --libs --cflags` -o $@ 

cvt: $(LIBSRCS) im.h examples/cvt.c
	gcc -ggdb $(LIBSRCS) examples/cvt.c -I. `pkg-config libpng --libs --cflags` -o $@ 

