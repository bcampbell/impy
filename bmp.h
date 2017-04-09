#ifndef BMP_H_INCLUDED
#define BMP_H_INCLUDED

#include "impy.h"


#define BMP_FILE_HEADER_SIZE 14

#define DIB_BITMAPCOREHEADER_SIZE 12
#define DIB_BITMAPINFOHEADER_SIZE 40
#define DIB_BITMAPV2INFOHEADER_SIZE 52
#define DIB_BITMAPV3INFOHEADER_SIZE 56
#define DIB_BITMAPV4HEADER_SIZE 104
#define DIB_BITMAPV5HEADER 124
#define DIB_MAX_HEADER_SIZE DIB_BITMAPV5HEADER

// compression types (we'll only support the first few)
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3
#define BI_JPEG 4
#define BI PNG 5
#define BI_ALPHABITFIELDS 6
#define BI_CMYK 11
#define BI_CMYKRLE8 12
#define BI_CMYKRLE4 13

bool im_is_bmp(const uint8_t* buf, int nbytes);
bool im_ext_match_bmp(const char* file_ext);
im_img* im_img_read_bmp( im_reader* rdr, ImErr* err );
bool im_img_write_bmp(im_img* img, im_writer* out, ImErr* err);

#endif // BMP_H_INCLUDED

