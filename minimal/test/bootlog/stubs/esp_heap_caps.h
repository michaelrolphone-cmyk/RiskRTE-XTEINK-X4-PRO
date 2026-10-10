#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline bool heap_caps_test_fail=false;
inline void* heap_caps_malloc(size_t n,unsigned){return heap_caps_test_fail?nullptr:std::malloc(n);}
