# impy

A simple library for loading/saving images and animations, written in C.

Git repo: https://github.com/bcampbell/impy

License: GPL3

## Supported formats

* PNG (load, save)
* GIF (load, save, including animation)
* PCX (load only, no support for 2 or 16-colour images yet)
* BMP (load, save)
* JPEG (load only)
* Targa (load only)

### currently work-in-progress

* IFF (ILBM, PBM, load only. ANIM5 anims only)

## Installation

### Prerequisites

Requires meson, libpng, zlib, giflib (v5+), libjpeg.

On Debian/Ubuntu:

```
$ sudo apt install meson libjpeg-dev libpng-dev libgif-dev
```

### Compile & Install

Generic unix steps:

    $ git clone https://github.com/bcampbell/impy.git
    $ cd impy
    $ meson setup build
    $ meson compile -C build

To then install:

    $ meson install

To specify a release build in the meson setup step:

    $ meson setup --buildtype release build

## API

Not yet. `impy.h` is the public interface, but it's subject to massive change.

