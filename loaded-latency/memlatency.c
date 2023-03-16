
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

#include <sys/time.h>
#include <sys/types.h>

#include <sys/mman.h>
#include <linux/mman.h>

#ifdef __aarch64__
#include "cntvct.h"
#endif

#ifdef __x86_64__
#include "rdtsc.h"
#endif

#include "alloc.h"
#include "memlatency.h"

/* lat_initialize can be called from main.c for shared memory */

void ** lat_initialize(size_t cacheline_bytes,
    size_t cacheline_count, int randomize, int clear_cache, size_t cacheline_stride, int use_hugepages) {

    size_t i;

    typedef struct {
        void * next;
        size_t order;
        size_t index;
        char buf[cacheline_bytes - sizeof(void *) - sizeof(size_t) - sizeof(size_t)];
    } node_t;

    // check that sizeof(node_t) == cacheline_bytes // XXX: might not be on 32-bit
    if (sizeof(node_t) != cacheline_bytes) {
        printf("in lat_setup, sizeof(node_t) = %zu, does not equal cacheline_bytes = %zu\n",
            sizeof(node_t), cacheline_bytes);
        exit(-1);
    }

    if (cacheline_bytes % sizeof(void*)) {
        printf("cacheline_bytes = %zu, is not an exact multiple of sizeof(void*) = %zu\n", cacheline_bytes, sizeof(void*));
        exit(-1);
    }

    node_t * p = do_alloc(cacheline_bytes * cacheline_count, use_hugepages, cacheline_bytes);

    // order is the sequence of node_t elements to traverse.  Initialize for sequential order.

    for (i = 0; i < cacheline_count; i++) {
        p[i].order = i;
    }

    // if randomize is used, randomly swap the order values

    if (randomize) {
        for (int rounds = 0; rounds < 10; rounds++) {
            for (i = 0; i < cacheline_count; i+= cacheline_stride) {
                size_t offset_a, offset_b, x;

                do {
                    offset_a = (lrand48() % (cacheline_count/cacheline_stride)) * cacheline_stride;
                    offset_b = (lrand48() % (cacheline_count/cacheline_stride)) * cacheline_stride;
                } while (offset_a == offset_b);

                x = p[offset_a].order;
                p[offset_a].order = p[offset_b].order;
                p[offset_b].order = x;
            }
        }
    }

    // create the pointer loop using the ordering table

    for (i = 0; i < cacheline_count - cacheline_stride; i += cacheline_stride) {
        p[p[i].order].next = &(p[p[i + cacheline_stride].order].next);
        p[p[i].order].index = i;
    }

    p[p[i].order].next = &(p[p[0].order].next);
    p[p[i].order].index = i;

#if 0
    // print out latency loop pointers for debug
    printf("by pointer:\n");
    node_t * pp = (node_t *) ppvoid;
    for (i = 0; i < cacheline_count; i++) {
        printf("%zu\tpp=%p pp->next=%p delta=%ld bytes\n", i, pp, pp->next, (long) pp->next - (long) pp );
        pp = pp->next;
    }

    printf("by entry:\n");
    pp = (node_t *) ppvoid;
    for (i = 0; i < cacheline_count; i++) {
        printf("pp[%zu]\t= %p, .next=%p\n", i, &(pp[i]), pp[i].next);
    }
#endif

    if (clear_cache) {
        __builtin___clear_cache(p, p+cacheline_count);
    }

    return (void **) p;
}


static void ** run(void ** p, size_t iterations) __attribute__((noinline));
static void ** run(void ** p, size_t iterations) {

#ifdef DO_DUMMY1
    size_t dummy1;
#define DUMMY1 asm volatile ("ldr %0, [%1, #8]" : "=r" (dummy1) : "r" (p));
#else
#define DUMMY1
#endif

#ifdef DO_DUMMY2
    size_t dummy2;
#define DUMMY2 asm volatile ("ldr %0, [%1, #16]" : "=r" (dummy2) : "r" (p));
#else
#define DUMMY2
#endif

    for (size_t i = 0; i < iterations; i++) {

        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;

        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
        p = (void **) (*p); DUMMY1;  DUMMY2;
    }

    return p;
}



void latency_thread (struct lat_thread_info * lat_tinfo) {
    size_t cacheline_bytes                    = lat_tinfo->lat_cacheline_bytes;
    size_t cacheline_count                    = lat_tinfo->cacheline_count;
    size_t iterations                         = lat_tinfo->iterations;
    double cycle_time_ns                      = lat_tinfo->cycle_time_ns;
    int thread_num                            = lat_tinfo->thread_num;
    int cpu                                   = lat_tinfo->cpu;
    unsigned long hwcounter_start             = lat_tinfo->hwcounter_start;
    unsigned long hwcounter_stop              = lat_tinfo->hwcounter_stop;
    int randomize                             = lat_tinfo->randomize;
    int use_hugepages                         = lat_tinfo->use_hugepages;
    void ** mem                               = lat_tinfo->mem;
    size_t lat_offset                         = lat_tinfo->lat_offset;
    int lat_clear_cache                       = lat_tinfo->lat_clear_cache;
    int warmup                                = lat_tinfo->warmup;
    size_t cacheline_stride                   = lat_tinfo->cacheline_stride;

    double avg_latency = 0.0;
    double min_latency = INFINITY;
    unsigned long latency_samples = 0;
    unsigned long start_tick, stop_tick;

    struct timeval t0, t1, tdiff;

    // if mem is not NULL, then it has been preinitalized.

    if (mem == NULL) {
        mem = lat_initialize(cacheline_bytes, cacheline_count, randomize, lat_clear_cache, cacheline_stride, use_hugepages);
    }

    void ** p = mem;

    // warm-up read

    if (warmup) {
        p = run(p, cacheline_count); // this will do 10 full-reads because there are 10 deploads per iteration in run()
        printf("CPU%d LATTHREAD%d: warmed up\n", cpu, thread_num);
    }
    p = run(p, lat_offset / 10);    // advance p to start offset. / 10 because there are 10 deploads per iteration in run()

    printf("CPU%d LATTHREAD%d: cacheline_count = %zu, iterations = %zu, mem = %p, randomize = %d, use_hugepages = %d, hwcounter_start = 0x%zx, lat_offset = %zu, tid = %d\n",
           cpu, thread_num, cacheline_count, iterations, mem, randomize,
           use_hugepages, hwcounter_start, lat_offset, gettid());

    // wait until hwcounter reaches the expected value
    while ((start_tick = read_hwcounter()) < hwcounter_start) {
        ;
    }

    lat_tinfo->actual_hwcounter_start = start_tick;

    // XXX: this printf has overhead
    printf("CPU%d LATTHREAD%d: started at " HWCOUNTER " = 0x%zx\n", cpu, thread_num, start_tick);

    size_t last_hwcounter = start_tick;

    stop_tick = read_hwcounter();

    if (stop_tick < hwcounter_stop) {
        do {
            gettimeofday(&t0, NULL);

            p = run(p, iterations);

            gettimeofday(&t1, NULL);

            timersub(&t1, &t0, &tdiff);

            double x = tdiff.tv_sec;    // x is elapsed time for loop. Here it is in seconds.
            x += tdiff.tv_usec / 1e6;

            double x_per_iter = x;
            x_per_iter *= 1e9;
            x_per_iter /= iterations * 10;  // latency for this iteration

            size_t this_hwcounter = read_hwcounter();

#if 0
            typedef struct {
                void * next;
                size_t order;
                size_t index;
            } partial_node_t;

            size_t current_index = ((partial_node_t *) p)->index;

            printf("CPU%d LATTHREAD%d: %.6f ns, %.6f cycles, cntvct=0x%08lx cntvct_diff=%lu p=%p index=%zu latency_samples=%zu\n",
                    cpu, thread_num, x_per_iter, x_per_iter/cycle_time_ns, this_hwcounter,
                    this_hwcounter - last_hwcounter, p, current_index, latency_samples);
#else
            printf("CPU%d LATTHREAD%d: %.6f ns, %.6f cycles\n", cpu, thread_num, x_per_iter, x_per_iter/cycle_time_ns);
#endif

            last_hwcounter = this_hwcounter;

            if (x_per_iter < min_latency) {
                min_latency = x_per_iter;
            }

            avg_latency += x_per_iter;
            latency_samples++;
        } while (last_hwcounter < hwcounter_stop);
        stop_tick = last_hwcounter;
    } else {
        unsigned long tick_deficit = stop_tick - hwcounter_stop;
        double tick_deficit_seconds = tick_deficit / (double) read_cntfreq();

        printf("CPU%d LATTHREAD%d: the hwclock has passed the expected "
        "stop time without any measurements.  Use --delay-seconds to "
        "increase delay time for threads to do their setup.  A "
        "suggested value to add to the current value is %f\n",
        cpu, thread_num, tick_deficit_seconds);
    }

    asm volatile ("" : : "r" (p));  // force p to be "used"

    lat_tinfo->actual_hwcounter_stop = stop_tick;

    // drop lowest latency if there is more than 1 sample
    // because it may be an unencumbered trailing iteration

    if (latency_samples > 1) {
        avg_latency -= min_latency;
        latency_samples--;
    }

    avg_latency /= latency_samples;

    lat_tinfo->avg_latency = avg_latency;
}
