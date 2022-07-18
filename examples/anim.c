#include "impy.h"
#include <stdio.h>


uint8_t pal[] = { 0,0,0, 255,0,0, 0,255,0, 0,0,255 };
uint8_t frame1[] = {
    0,0,0,0,
    0,1,1,0,
    0,1,1,0,
    0,0,0,0,
};
uint8_t frame2[] = {
    2,1,2,1,
    1,0,0,2,
    2,0,0,1,
    1,2,1,2,
};

ImErr test_anim(ImFileFmt file_fmt, const char *filename);
ImErr test_indexed_img(ImFileFmt file_fmt, const char *filename);

int main( int argc, char* argv[])
{
    ImErr err;
    err = test_indexed_img(IM_FILEFMT_PNG, "/tmp/test.png");
    if (err != ERR_NONE) {
        fprintf(stderr, "test.png failed: ImErr: %d\n", err);
    }
    err = test_indexed_img(IM_FILEFMT_BMP, "/tmp/test.bmp");
    if (err != ERR_NONE) {
        fprintf(stderr, "test.bmp failed: ImErr: %d\n", err);
    }

    test_anim(IM_FILEFMT_GIF, "/tmp/anim.gif");
    if (err != ERR_NONE) {
        fprintf(stderr, "anim.gif failed: ImErr: %d\n", err);
    }
}

ImErr test_anim(ImFileFmt file_fmt, const char *filename)
{
    ImErr err;
    im_writer* wr = im_writer_open_file(filename, &err);
    if (!wr) {
        return err;
    }

    im_begin_img(wr, 4, 4, IM_FMT_INDEX8);

    im_set_palette(wr, IM_FMT_RGB, 4, pal);
    im_write_rows(wr, 4, frame1);

    im_begin_img(wr, 4, 4, IM_FMT_INDEX8);
    im_write_rows(wr, 4, frame2);

    err = im_writer_finish(wr);
    return err;
}

ImErr test_indexed_img(ImFileFmt file_fmt, const char *filename)
{
    ImErr err;
    im_writer* wr = im_writer_open_file(filename, &err);
    if (!wr) {
        return err;
    }

    im_begin_img(wr, 4, 4, IM_FMT_INDEX8);

    im_set_palette(wr, IM_FMT_RGB, 4, pal);
    im_write_rows(wr, 4, frame1);

    err = im_writer_finish(wr);
    return err;
}

