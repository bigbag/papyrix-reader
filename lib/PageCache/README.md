# PageCache

PageCache stores rendered pages on the SD card and uses a lookup table for random access. It can create a partial cache and extend it with more pages.

`loadPage()` returns an owned page or `nullptr` on error. Create, extend, load, and clear report failure to the caller. See the [public header](src/PageCache.h) for the API and the [rendering pipeline](../../docs/rendering-pipeline.md) for its use.
