#include "impy.h"
#include "private.h"

//#include "jinclude.h"
#include <jpeglib.h>
#include <jerror.h>

static bool is_jpeg(const uint8_t* buf, int nbytes);
static bool match_jpeg_ext(const char* file_ext);
static im_img* read_jpeg_image( im_reader* rdr, ImErr* err );
//static bool write_jpeg_image(im_img* img, im_writer* out, ImErr* err);

struct handler handle_jpeg = {is_jpeg, read_jpeg_image, NULL, match_jpeg_ext, NULL, NULL};

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


// ---------------------------------------------
// custom jpeg_source_mgr to read from im_reader
//
typedef struct {
    struct jpeg_source_mgr pub;
    im_reader* rdr;
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

static imreader_src* init_im_reader_src( j_decompress_ptr cinfo, im_reader* rdr)
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

// end custom reader
// --------------------------------------------------------


static im_img* read_jpeg_image( im_reader* rdr, ImErr* err )
{
    struct jpeg_decompress_struct cinfo;

    struct jpeg_error_mgr errmgr;

    jpeg_create_decompress(&cinfo);

    // TODO: proper error handling
    cinfo.err = jpeg_std_error(&errmgr);
    imreader_src* src = init_im_reader_src(&cinfo, rdr);
//    FILE * infile = fopen("test.jpeg","rb");
//    jpeg_stdio_src(&cinfo, infile);

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);


    int w = cinfo.output_width;
    int h = cinfo.output_height;
    int channels = cinfo.num_components;
    // TODO!!!
  //type = GL_RGB;
  //if(channels == 4) type = GL_RGBA;
    im_img* image = im_img_new( w, h, 1, FMT_RGB, DT_U8);

    // TODO: alloc a rowptr array and read all scanlines at once
    int row=0;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        void* rowptr = im_img_row(image, row);
        jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&rowptr, 1);
        ++row;
    }

    jpeg_finish_decompress(&cinfo);   //finish decompressing

    ifree(src);
    return image; 
}
