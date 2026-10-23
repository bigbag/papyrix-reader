Decoder source: Witchhunt revision `e05065f6c3f16a12dad5bc1313046ae3f4f203f1`.

# Acknowledgements

The progressive decoding procedures (DC/AC first and refinement scans, EOB runs, restart
handling) follow ITU-T T.81 Annex G. The AC refinement routine mirrors the structure of
`decode_mcu_AC_refine` in the Independent JPEG Group's libjpeg (`jdphuff.c`), whose
correction-bit bookkeeping is the reference behaviour every encoder is tested against.

The reduced-size inverse DCT (keeping the top-left n x n coefficients and inverting with an
n-point IDCT) is the approach of libjpeg's scaled decoding.

This is an independent implementation; it contains no libjpeg, libjpeg-turbo, JPEGDEC or
TJpgDec source code.
