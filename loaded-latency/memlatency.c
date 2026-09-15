
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
#include <stddef.h>
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

#include <stdint.h>

/* lat_initialize can be called from main.c for shared memory */

/*
 * mode-2: n-pass pointer chain per cacheline
 *
 * LAT_CHAIN_PASSES - number of independent random passes through the
 * buffer concatenated head-to-tail to form the mode-2 pointer chain.
 *
 * Algorithm (make_multipass_chain):
 *   Each pass j produces one Hamiltonian cycle through all cachelines
 *   using slot j of the per-cacheline pointer array. The cacheline
 *   visitation order within each pass is an independent Fisher-Yates
 *   shuffle. Passes are concatenated so the last pointer of pass j
 *   links to the first pointer of pass j+1, and the last pointer of
 *   pass (PASSES-1) wraps to the first pointer of pass 0. Total
 *   chain length = cacheline_count * LAT_CHAIN_PASSES.
 *
 * LAT_CHAIN_PASSES is passed in as the randomize parameter value.
 *
 */

typedef struct ptr_s {
    struct ptr_s * next;
} ptr_t;

/*
 * mode-1: 1 pointer per cache line
 *
 * Header layout of the mode-1 node_t (first field only).
 * node_t itself is declared inside lat_initialize() because its trailing
 * pad depends on the runtime cacheline_bytes. node_hdr_t lets helper
 * functions access ->next without seeing the local typedef.
 */
typedef struct {
    void * next;
} node_hdr_t;

#define NODE_HDR(base, i, cbytes) \
    ((node_hdr_t *)((char *)(base) + (i) * (cbytes)))

/*
 * Generate a random permutation of [0, count) using Fisher-Yates.
 * Matches mode 1 style: size_t indices, lrand48().
 */
static void make_random_order(size_t * index, size_t count) {
    size_t * selection = malloc(count * sizeof(size_t));
    if (selection == NULL) {
        printf("make_random_order: malloc failed for count=%zu\n", count);
        exit(-1);
    }

    for (size_t i = 0; i < count; i++) {
        selection[i] = i;
    }

    size_t remaining = count;
    for (size_t i = 0; i < count; i++) {
        size_t n = (size_t) lrand48() % remaining;
        index[i] = selection[n];
        remaining--;
        selection[n] = selection[remaining];
    }

    free(selection);
}


/*
 * Mode 1: pair-swap shuffle.
 * Build a permutation of stride-aligned cacheline offsets in an
 * external array (mirroring make_multipass_chain), then wire
 * node->next pointers by walking it. The nodes carry only ->next.
 */
static void ** make_pairswap_chain(void * ptr,
    size_t cacheline_bytes, size_t cacheline_count, size_t cacheline_stride, int randomize) {

    size_t count = cacheline_count / cacheline_stride;
    if (count < 2) {
        printf("make_pairswap_chain: need >=2 stride slots, got %zu\n", count);
        exit(-1);
    }

    size_t * order = malloc(count * sizeof(size_t));
    if (order == NULL) {
        printf("make_pairswap_chain: malloc failed for count=%zu\n", count);
        exit(-1);
    }

    /* Sequential permutation: order[k] = k-th stride-aligned offset */
    for (size_t k = 0; k < count; k++) {
        order[k] = k * cacheline_stride;
    }

    /* Optional shuffle: 10 rounds of random pair swaps on the array */
    if (randomize) {
        for (int rounds = 0; rounds < 10; rounds++) {
            for (size_t k = 0; k < count; k++) {
                size_t a, b, x;
                do {
                    a = (size_t) lrand48() % count;
                    b = (size_t) lrand48() % count;
                } while (a == b);
                x = order[a];
                order[a] = order[b];
                order[b] = x;
            }
        }
    }

    /* Wire pointer loop following the permutation */
    for (size_t k = 0; k < count - 1; k++) {
        size_t cur  = order[k];
        size_t next = order[k + 1];
        NODE_HDR(ptr, cur, cacheline_bytes)->next =
            &NODE_HDR(ptr, next, cacheline_bytes)->next;
    }
    /* Close the cycle */
    {
        size_t cur   = order[count - 1];
        size_t first = order[0];
        NODE_HDR(ptr, cur, cacheline_bytes)->next =
            &NODE_HDR(ptr, first, cacheline_bytes)->next;
    }

    free(order);
    return (void **) ptr;
}

/*
 * Mode 2: build LAT_CHAIN_PASSES independent Hamiltonian cycles,
 * each using slot j of every cacheline. Cycles are concatenated
 * head-to-tail to form one closed loop of length
 * cacheline_count * LAT_CHAIN_PASSES.
 */
static void ** make_multipass_chain(void * ptr,
    size_t cacheline_count, size_t cacheline_bytes, size_t chains) {

    size_t * index = malloc(cacheline_count * sizeof(size_t));
    if (index == NULL) {
        printf("make_multipass_chain: malloc failed for count=%zu\n", cacheline_count);
        exit(-1);
    }

    char  * base    = (char *) ptr;
    ptr_t * first   = NULL;
    ptr_t * current = NULL;

    for (size_t j = 0; j < chains; j++) {
        make_random_order(index, cacheline_count);

        for (size_t i = 0; i < cacheline_count; i++) {
            ptr_t * next = (ptr_t *)(base + index[i] * cacheline_bytes + j * sizeof(ptr_t));
            if (current == NULL) {
                first = next;
            } else {
                current->next = next;
            }
            current = next;
        }
    }

    /* close the loop */
    current->next = first;

    free(index);
    return (void **) first;
}


// lat_initialize: returns the entry into the loop and lowest address in *pmem_base
void ** lat_initialize(size_t cacheline_bytes, size_t cacheline_count,
        size_t randomize, int clear_cache, size_t cacheline_stride,
        int use_hugepages, void ** pmem_base) {

    typedef struct {
        union {
            void * next;             /* mode 1: single chain */
            ptr_t ptrs[randomize];   /* mode 2: per-pass chain slots */
        };
        char buf[cacheline_bytes
                 - (sizeof(ptr_t) * randomize > sizeof(void *)
                    ? sizeof(ptr_t) * randomize
                    : sizeof(void *))];
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
    *pmem_base = p;

    void ** head;
    if (randomize >= 2) {
        head = make_multipass_chain(p, cacheline_count, cacheline_bytes, randomize);
    } else {
        // also for randomize == 0
        head = make_pairswap_chain(p, cacheline_bytes, cacheline_count, cacheline_stride, randomize);
    }

//#define LAT_INITIALIZE_POINTER_DEBUG
#ifdef LAT_INITIALIZE_POINTER_DEBUG
    // examine pointer chain
    {
        long diff_sum = 0;
        size_t num_pointers = cacheline_count / cacheline_stride;

        if (cacheline_count % cacheline_stride) {
            num_pointers++;
        }

        // randomize contains the number of chains
        if (randomize) { num_pointers *= randomize; }

        // head points to a node_t that is the entry into the loop of pointers.

        printf("head = %p, num_pointers = %zu\n", head, num_pointers);

#if 0
        printf("*head = head->next = head->ptrs[0].next = %p %p %p\n",
             *head, ((node_t *) head)->next, ((node_t *) head)->ptrs[0].next);
        // *head:  abuse the fact that the .next is at the beginning of node_t
        // ((node_t *) head)->next: cast head to a node_t and expect next to be at the beginning of it
        // ((node_t *) head)->ptrs[0].next: explicitly get the multichain ptrs[0].next
        // all of the above should give the same address of the next node_t.
#endif

        ptr_t * ppp = (ptr_t *) head;

        for (size_t i = 0; i < num_pointers; i++) {
            ssize_t delta = (ppp->next - ppp) * sizeof(void *); // signed bytes
            diff_sum += labs(delta);
            printf("%zu:\t%p\tnext = %p\t%zd\n", i, ppp, ppp->next, delta);
            ppp = ppp->next;
            if (ppp == (ptr_t *) head && i < num_pointers - 1) {
                printf("ERROR: i = %zu, num_pointers = %zu, but already ppp == head. Unexpectedly walked a cycle early?\n", i, num_pointers);
            }
        }

        double avg_diff = (double) diff_sum / num_pointers;
        printf("diff_sum = %ld, num_pointers = %zu, avg_diff = %f\n", diff_sum, num_pointers, avg_diff);

        if (ppp != (ptr_t *) head) {
            // expect that num_pointers walks through a cycle
            printf("ERROR: ppp does not match head!\n");
            printf("ppp = %p, head = %p\n", ppp, head);
            exit(-1);
        }

        exit(0);
    }
#endif

    if (clear_cache) {
        __builtin___clear_cache((char *) p, (char *) p + cacheline_bytes * cacheline_count);
    }

    return head;
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
    size_t randomize                          = lat_tinfo->randomize;
    int use_hugepages                         = lat_tinfo->use_hugepages;
    void ** mem                               = lat_tinfo->mem;
    void * mem_base                           = lat_tinfo->mem_base;
    size_t lat_offset                         = lat_tinfo->lat_offset;
    int lat_clear_cache                       = lat_tinfo->lat_clear_cache;
    int warmup                                = lat_tinfo->warmup;
    size_t cacheline_stride                   = lat_tinfo->cacheline_stride;

    double avg_latency = 0.0;
    double min_latency = INFINITY;
    unsigned long latency_samples = 0;
    unsigned long start_tick, stop_tick;

    struct timeval t0, t1, tdiff;

    // NOTE: if randomize >= 2, it is the number of chain passes.

    // if mem is not NULL, then it has been preinitalized.

    if (mem_base == NULL) {
        if (mem != NULL) { printf("mem_base == null but mem != null, unpexpected\n"); exit(-1); }
        mem = lat_initialize(cacheline_bytes, cacheline_count, randomize, lat_clear_cache, cacheline_stride, use_hugepages, &mem_base);
    }

    void ** p = mem;

    // warm-up read

    if (warmup) {
        p = run(p, cacheline_count); // this will do 10 full-reads because there are 10 deploads per iteration in run()
        printf("CPU%d LATTHREAD%d: warmed up\n", cpu, thread_num);
    }
    p = run(p, lat_offset / 10);    // advance p to start offset. / 10 because there are 10 deploads per iteration in run()

    printf("CPU%d LATTHREAD%d: cacheline_count = %zu, iterations = %zu, mem = %p, randomize = %zu, use_hugepages = %d, hwcounter_start = 0x%zx, lat_offset = %zu, tid = %d\n",
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

//#define LAT_DEBUG_CHAIN
#ifdef LAT_DEBUG_CHAIN
            /*
             * Chain-walk debug. `p` points at a node's ->next field.
             * With the new layout there is no stored index/order, so
             * derive the cacheline index from the buffer base:
             *   line_base = p & ~(cacheline_bytes - 1)
             *   idx       = (line_base - mem_base) / cacheline_bytes
             *   slot      = (p - line_base) / sizeof(void *)
             * slot is 0..randomize-1
             */
            uintptr_t pu        = (uintptr_t) p;
            uintptr_t line_base = pu & ~((uintptr_t) cacheline_bytes - 1);
            size_t    slot      = (size_t)((pu - line_base) / sizeof(void *));
            size_t    current_index = (mem_base != NULL)
                ? (size_t)(((char *) line_base - (char *) mem_base) / cacheline_bytes)
                : (size_t) -1;

            printf("CPU%d LATTHREAD%d: %.6f ns, %.6f cycles, "
                   HWCOUNTER "=0x%08lx " HWCOUNTER "_diff=%lu "
                   "p=%p line=0x%lx idx=%zu slot=%zu samples=%zu\n",
                   cpu, thread_num, x_per_iter, x_per_iter / cycle_time_ns,
                   this_hwcounter, this_hwcounter - last_hwcounter,
                   (void *) p, (unsigned long) line_base,
                   current_index, slot, latency_samples);
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
