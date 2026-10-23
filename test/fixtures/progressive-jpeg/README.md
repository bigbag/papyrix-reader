# Progressive JPEG fixtures

`generate.py` creates all source images from local pixel patterns. No image comes from Witchhunt.

The generator uses `cjpeg` and `djpeg` from libjpeg-turbo 3.2.0 (build 20260701). The `.y.pgm` files contain independent grayscale reference pixels. The maximum-size and scan-limit images use constant level 128. Black and white images use levels 0 and 255.

Run `python3 test/fixtures/progressive-jpeg/generate.py` to regenerate the files. Normal tests use the saved files and do not require libjpeg tools.
