#include "impy.h"
#include "private.h"

//#include "jinclude.h"
#include <jpeglib.h>
#include <jerror.h>
#include <setjmp.h>

#if 0
static bool is_jpeg(const uint8_t* buf, int nbytes);
static bool match_jpeg_ext(const char* file_ext);

static bool is_jpeg(const uint8_t* buf, int nbytes)
{
    // Start of Image (SOI) marker (FF D8)
    // followed by JFIF marker (FF E0) or EXIF marker(FF 01)
    // But wikipedia suggests ff d8 ff as magic number, which is nice and simple.
    return buf[0]==0xff && buf[1] == 0xd8 && buf[2] == 0xff;
}

static bool match_jpeg_ext(const char* file_ext)
{
    if (istricmp(file_ext,".jpeg")==0) {
        return true;
    }
    if (istricmp(file_ext,".jpg")==0) {
        return true;
    }
    return (istricmp(file_ext,".jpeg")==0);
}
#endif

// ---------------------------------------------
// custom jpeg_source_mgr to read from im_in
//
typedef struct {
    struct jpeg_source_mgr pub;
    im_in* rdr;
    uint8_t buf[4096];
    bool start_of_file;
} imreader_src;

static void init_source(j_decompress_ptr cinfo)
{
    imreader_src* mgr = (imreader_src*)cinfo->src;
    mgr->start_of_file = true;
}

static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
    imreader_src* mgr = (imreader_src*)cinfo->src;
    size_t nbytes;

    nbytes = im_read(mgr->rdr, mgr->buf, sizeof(mgr->buf));
    if (nbytes <= 0) {
        // no error/eof return from jpeg_source_mgr,
        // we either just throw a fatal error, or synthesize
        // a fake end-of-image marker:

        // treat empty file as fatal error
        if (mgr->start_of_file) {
            ERREXIT(cinfo, JERR_INPUT_EMPTY);
        }

        // insert fake EOI marker
        // TODO: should log a warning
        mgr->buf[0] = (JOCTET) 0xFF;
        mgr->buf[1] = (JOCTET) JPEG_EOI;
        nbytes = 2;
    }

    mgr->pub.next_input_byte = mgr->buf;
    mgr->pub.bytes_in_buffer = nbytes;
    mgr->start_of_file = false;

    return TRUE;
}

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    struct jpeg_source_mgr* src = (struct jpeg_source_mgr*) cinfo->src;

    // num_bytes might be bigger than buffer
    if (num_bytes > 0) {
        while (num_bytes > (long) src->bytes_in_buffer) {
            num_bytes -= (long) src->bytes_in_buffer;
            (void) (*src->fill_input_buffer) (cinfo);
        }
        src->next_input_byte += (size_t) num_bytes;
        src->bytes_in_buffer -= (size_t) num_bytes;
    }
}

static void term_source(j_decompress_ptr cinfo)
{
}

static imreader_src* init_im_in_src( j_decompress_ptr cinfo, im_in* rdr)
{
    imreader_src* mgr = imalloc(sizeof(imreader_src));

    mgr->pub.init_source = init_source;
    mgr->pub.fill_input_buffer = fill_input_buffer;
    mgr->pub.skip_input_data = skip_input_data;
    mgr->pub.resync_to_restart = jpeg_resync_to_restart;
    mgr->pub.term_source = term_source;
    mgr->pub.bytes_in_buffer = 0;       // force a fill upon first read
    mgr->pub.next_input_byte = NULL;

    mgr->rdr = rdr;

    cinfo->src = (struct jpeg_source_mgr*)mgr;
    return mgr;
}

// --------------------------------------------------------
//  error handler

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
} my_error_mgr;

void my_error_exit(j_common_ptr cinfo)
{
  my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
  (*cinfo->err->output_message) (cinfo);
  longjmp(myerr->setjmp_buffer, 1);
}


//------------------------------------------------------
//

im_img* iread_jpeg_image( im_in* rdr, ImErr* err )
{
    struct jpeg_decompress_struct cinfo;
    my_error_mgr jerr;
    imreader_src* src = 0;
    im_img* image = 0;

    *err = ERR_NONE;

    jpeg_create_decompress(&cinfo);

    src = init_im_in_src(&cinfo, rdr);

    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {

        *err = ERR_EXTLIB;
        goto cleanup;
    }

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int w = cinfo.output_width;
    int h = cinfo.output_height;
    int channels = cinfo.num_components;
    if( channels != 3) {
        // TODO: do we need to support other values? If so, how?
        *err = ERR_UNSUPPORTED;
        goto cleanup;
    }
    image = im_img_new( w, h, 1, IM_FMT_RGB);

    int y;
    for (y=0; y<h; ++y) {
        void* rowptr = im_img_row(image, y);
        jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&rowptr, 1);
    }

    jpeg_finish_decompress(&cinfo);   //finish decompressing

cleanup:
    jpeg_destroy_decompress(&cinfo);
    if (src) {
        ifree(src);
    }

    if (*err != ERR_NONE) {
        if (image) {
            im_img_free(image);
            image = 0;
        }
    }

    return image; 
}
