#pragma once
#include <stdint.h>

size_t min_index_avx2(int32_t* array, size_t size);
size_t min_index_sse(int32_t* array, size_t size);
