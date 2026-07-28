// clap_perf_test.c
//
// Minimal, deterministic benchmark harness used to independently verify the
// "Replace hot-path bucket memcpy with inlined word-copy helper" optimization
// commit on hashmap.c. It exercises hashmap_set_with_hash / resize0 (via an
// uncapped map that must grow) and hashmap_delete_with_hash, which are the
// three call sites touched by that commit, using a fixed seed so the same
// sequence of operations runs identically before and after the change.
//
// Build: cc -O3 -DNDEBUG clap_perf_test.c hashmap.c -o clap_perf_test
// Run:   ./clap_perf_test
// Output: a single line "PERF_TIME_SEC=<seconds>" measuring only the
// set+delete loops (map creation, array setup, and shuffling are excluded).

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "hashmap.h"

#define SEED 42
#define N 3000000

static void shuffle(void *array, size_t numels, size_t elsize) {
    char tmp[elsize];
    char *arr = array;
    for (size_t i = 0; i < numels - 1; i++) {
        size_t j = i + rand() / (RAND_MAX / (numels - i) + 1);
        memcpy(tmp, arr + j * elsize, elsize);
        memcpy(arr + j * elsize, arr + i * elsize, elsize);
        memcpy(arr + i * elsize, tmp, elsize);
    }
}

static int compare_ints_udata(const void *a, const void *b, void *udata) {
    (void)udata;
    return *(const int*)a - *(const int*)b;
}

static uint64_t hash_int(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_xxhash3(item, sizeof(int), seed0, seed1);
}

static double now_secs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    srand(SEED);

    int *vals = malloc(N * sizeof(int));
    int *vals2 = malloc(N * sizeof(int));
    assert(vals && vals2);
    for (int i = 0; i < N; i++) {
        vals[i] = i;
    }
    shuffle(vals, N, sizeof(int));
    memcpy(vals2, vals, N * sizeof(int));
    shuffle(vals2, N, sizeof(int));

    // Uncapped map: forces repeated resize0() calls (robin-hood rehash),
    // in addition to the bucket displacement in hashmap_set_with_hash.
    struct hashmap *map = hashmap_new(sizeof(int), 0, SEED, SEED, hash_int,
                                      compare_ints_udata, NULL, NULL);
    assert(map);

    // Only the set/resize and delete loops (the paths touched by the
    // bucket_copy optimization) are timed; array setup and shuffling
    // (identical before/after) happen outside this window.
    double t0 = now_secs();
    for (int i = 0; i < N; i++) {
        const int *v = hashmap_set(map, &vals[i]);
        assert(!v);
        (void)v;
    }
    for (int i = 0; i < N; i++) {
        const int *v = hashmap_delete(map, &vals2[i]);
        assert(v && *v == vals2[i]);
        (void)v;
    }
    double t1 = now_secs();

    hashmap_free(map);
    free(vals);
    free(vals2);

    printf("PERF_TIME_SEC=%.6f\n", t1 - t0);
    return 0;
}
