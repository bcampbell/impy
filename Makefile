LIBSRCS=im.c img.c img_default.c io.c util.c bundle.c convert.c iff.c gif.c png_read.c png_write.c

imgview: $(LIBSRCS) im.h examples/imgview.c
	gcc -ggdb $(LIBSRCS) examples/imgview.c -I. -lgif `pkg-config libpng sdl2 --libs --cflags` -o $@ 

imginfo: $(LIBSRCS) im.h examples/imginfo.c
	gcc -ggdb $(LIBSRCS) examples/imginfo.c -I. -lgif `pkg-config libpng --libs --cflags` -o $@ 


cvt: $(LIBSRCS) im.h examples/cvt.c
	gcc -ggdb $(LIBSRCS) examples/cvt.c -I. -lgif `pkg-config libpng --libs --cflags` -o $@ 

