# impy

A simple library for loading/saving images and animations, written in C.


## Supported formats

* PNG (load, save)
* GIF (load, save, including animation)
* IFF (ILBM, PBM, load only. ANIM5 anims only)
* PCX (load only, no support for 2 or 16-colour images yet)
* BMP (load only)

## Installation

Requires cmake, libpng, zlib, giflib (v5+).

Generic unix steps:

    $ git clone https://github.com/bcampbell/impy.git
    $ cd impy
    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ sudo make install

## API

Not yet. `impy.h` is the public interface, but it's subject to massive change.

## Example usage

    // load an image, output dimensions

    #include <stdio.h>
    #include <impy.h>

    int main(int argc, char* argv[])
    {
        if (argc<=1) {
            return 0;
        }

        im_img* img = im_img_load( argv[1], &err);
        if (!img) {
            fprintf(stderr,"load failed (err=%d)\n",err);
            return 1;
        }

        printf("%d x %d\n", im_img_w(img), im_img_h(img));

        im_img_free(img);
        return 0;
    }


