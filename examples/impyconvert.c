#include "impy.h"
#include <stdio.h>
#include <stdlib.h>

int main( int argc, char* argv[])
{
    ImErr err;
    im_reader* reader;
    im_writer* writer;
    im_imginfo inf;
    uint8_t *buf = NULL;
    unsigned int y;

    if (argc<3) {
        fprintf(stderr, "usage: %s infile outfile\n", argv[0] );
        return 1;
    }

    reader = im_reader_open_file(argv[1], &err);
    if (!reader) {
        printf("im_reader_open_file() failed (ImErr=%d)\n", err);
        return 1;
    }
    writer = im_writer_open_file(argv[2], &err);
    if (!writer) {
        printf("im_writer_open_file() failed (ImErr=%d)\n", err);
        return 1;
    }

    while(im_get_img(reader, &inf)) {
        im_begin_img(writer, inf.w, inf.h, inf.fmt);
        // TODO: PALETTE!!!
        printf("img: %dx%d fmt=%d num_colours=%d\n", inf.w, inf.h, inf.fmt, inf.pal_num_colours);
        if (inf.pal_num_colours > 0) {
            buf = realloc(buf, inf.pal_num_colours * im_fmt_bytesperpixel(IM_FMT_RGBA));
            im_read_palette(reader, IM_FMT_RGBA, buf);
            im_set_palette(writer, IM_FMT_RGBA, inf.pal_num_colours, buf);
        }

        buf = realloc(buf, inf.w * im_fmt_bytesperpixel(inf.fmt));
        for (y=0; y < inf.h; ++y) {
            im_read_rows(reader, 1, buf);
            im_write_rows(writer, 1, buf);
        }
    }
    free(buf);

    err = im_reader_finish(reader);
    if (err != ERR_NONE) {
        printf("Reader failed: ImErr=%d\n", err);
        return 1;
    }
    err = im_writer_finish(writer);
    if (err != ERR_NONE) {
        printf("Writer failed: ImErr=%d\n", err);
        return 1;
    }

    return 0;
}


