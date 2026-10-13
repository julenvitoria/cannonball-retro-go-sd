// Game & Watch port: placement of the engine objects that do not fit the default heap order.
#pragma once
#include <stddef.h>
#include <new>

// DTCM (zero-wait, ~100 KB) if it fits, else the normal heap. Zeroed, never freed.
void* gw_fast_alloc(size_t size);
