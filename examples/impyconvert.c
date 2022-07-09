#include "impy.h"
#include <stdio.h>


int main( int argc, char* argv[])
{

    if (argc<3) {
        fprintf(stderr, "usage: %s infile outfile\n", argv[0] );
        return 1;
    }
#if 0
    ImErr err;
    im_bundle* bundle = NULL;
    // read
    bundle = im_bundle_load(argv[1], &err);
    if (bundle==NULL) {
        fprintf(stderr,"Load failed (ImErr %d)\n", err);
        return 1;
    }

    // write
    {
        im_out* out;
        im_writer* wr;
        const char* outfilename = argv[2];
        ImFileFmt outfmt = im_guess_file_format(outfilename);
        if(outfmt == IM_FILEFMT_UNKNOWN) {
            fprintf(stderr,"%s: unknown file format\n", outfilename);
            return 1;
        }

        out = im_open_file_writer(outfilename, &err);
        if (!out) {
            fprintf(stderr,"Couldn't open '%s' (ImErr %d)\n", outfilename, err);
            return 1;
        }

        wr = im_new_writer(outfmt, out, &err);
        if (!out) {
            fprintf(stderr,"error writing (ImErr %d)\n", err);
            return 1;
        }
        for (int i = 0; i < im_bundle_num_frames(bundle); ++i) {
            im_img* img = im_bundle_get_frame(bundle, i);

            im_begin_img(wr, img->w, img->h, img->format);
            if (img->pal_num_colours > 0) {
                im_set_palette(wr, img->pal_fmt, img->pal_num_colours, img->pal_data);
            }
            for (int y = 0; y < img->h; ++y) {
                im_write_rows(wr, 1, im_img_row(img, y));
            }
        }
        err = im_writer_finish(wr);

        if (err != ERR_NONE) {
            fprintf(stderr,"Save failed (ImErr %d)\n", err);
            return 1;
        }
    }

    im_bundle_free(bundle);
    return 0;
#endif
}


