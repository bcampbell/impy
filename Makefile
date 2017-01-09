LIBSRCS=im.c img.c img_default.c io.c util.c bundle.c convert.c iff.c gif.c png_read.c png_write.c

view: $(LIBSRCS) im.h examples/view.c
	gcc -ggdb $(LIBSRCS) examples/view.c -I. -lgif `pkg-config libpng sdl2 --libs --cflags` -o $@ 

cvt: $(LIBSRCS) im.h examples/cvt.c
	gcc -ggdb $(LIBSRCS) examples/cvt.c -I. -lgif `pkg-config libpng --libs --cflags` -o $@ 

