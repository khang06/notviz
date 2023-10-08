#include <algorithm>
#include <stdint.h>
#include <intrin.h>
#include <smmintrin.h>
#include <avxintrin.h>
#include <avx2intrin.h>

// https://github.com/WojciechMula/toys/blob/master/simd-min-index/avx2.cpp
__attribute__((target("avx2"))) size_t min_index_avx2(int32_t* array, size_t size) {
    const __m256i increment = _mm256_set1_epi32(8);
    __m256i indices = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    __m256i minindices = indices;
    __m256i minvalues = _mm256_loadu_si256((__m256i*)array);

    for (size_t i = 8; i < size; i += 8) {

        indices = _mm256_add_epi32(indices, increment);

        const __m256i values = _mm256_loadu_si256((__m256i*)(array + i));
        const __m256i lt = _mm256_cmpgt_epi32(minvalues, values);
        minindices = _mm256_blendv_epi8(minindices, indices, lt);
        minvalues = _mm256_min_epi32(values, minvalues);
    }

    // find min index in vector result (in an extremely naive way)
    int32_t values_array[8];
    uint32_t indices_array[8];

    _mm256_storeu_si256((__m256i*)values_array, minvalues);
    _mm256_storeu_si256((__m256i*)indices_array, minindices);

    size_t  minindex = indices_array[0];
    int32_t minvalue = values_array[0];
    for (int i = 1; i < 8; i++) {
        if (values_array[i] < minvalue) {
            minvalue = values_array[i];
            minindex = indices_array[i];
        }
        else if (values_array[i] == minvalue) {
            minindex = std::min(minindex, size_t(indices_array[i]));
        }
    }

    return minindex;
}

// https://github.com/WojciechMula/toys/blob/master/simd-min-index/sse.cpp
__attribute__((target("sse4.2"))) size_t min_index_sse(int32_t* array, size_t size) {
    const __m128i increment = _mm_set1_epi32(4);
    __m128i indices = _mm_setr_epi32(0, 1, 2, 3);
    __m128i minindices = indices;
    __m128i minvalues = _mm_loadu_si128((__m128i*)array);

    for (size_t i = 4; i < size; i += 4) {

        indices = _mm_add_epi32(indices, increment);

        const __m128i values = _mm_loadu_si128((__m128i*)(array + i));
        const __m128i lt = _mm_cmplt_epi32(values, minvalues);
        minindices = _mm_blendv_epi8(minindices, indices, lt);
        minvalues = _mm_min_epi32(values, minvalues);
    }

    // find min index in vector result (in an extremely naive way)
    int32_t values_array[4];
    uint32_t indices_array[4];

    _mm_storeu_si128((__m128i*)values_array, minvalues);
    _mm_storeu_si128((__m128i*)indices_array, minindices);

    size_t  minindex = indices_array[0];
    int32_t minvalue = values_array[0];
    for (int i = 1; i < 4; i++) {
        if (values_array[i] < minvalue) {
            minvalue = values_array[i];
            minindex = indices_array[i];
        }
        else if (values_array[i] == minvalue) {
            minindex = std::min(minindex, size_t(indices_array[i]));
        }
    }

    return minindex;
}