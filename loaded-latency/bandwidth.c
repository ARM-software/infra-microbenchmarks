
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

#include <sys/time.h>
#include <sys/types.h>

#ifdef USE_HUGEPAGES
#include <sys/mman.h>
#endif

#ifdef __aarch64__
#include "cntvct.h"
#endif

#ifdef __x86_64__
#include "rdtsc.h"
#endif

#include "alloc.h"
#include "bandwidth.h"


/* my_read() provides a variable read bandwidth.
   Increasing inner_nops lowers the read bandwidth. */

static void my_read(void * p, size_t bytes, size_t inner_nops, size_t bw_cacheline_bytes)
    __attribute__((noinline));
static void my_read(void * p, size_t bytes, size_t inner_nops, size_t bw_cacheline_bytes) {
    size_t i, j, dummy;

    // this just reads one 64-bit dword from each cache lnie
    for (i = 0; i < bytes; i += bw_cacheline_bytes) {
#ifdef __aarch64__
        asm volatile ("ldr %0, [%1, %2]" : "=r" (dummy): "r" (p), "r" (i));
#endif
#ifdef __x86_64__
        asm volatile ("movq   (%1,%2,1), %0" : "=r" (dummy) : "r" (p), "r" (i));
#endif
        for (j = 0; j < inner_nops; j++) {
            asm volatile ("");
        }
    }
}


/* my_write() provides a variable write bandwidth.
   Increasing inner_nops lowers the write bandwidth. */

static void my_write(void * p, size_t bytes, size_t inner_nops, size_t bw_cacheline_bytes)
    __attribute__((noinline));
static void my_write(void * p, size_t bytes, size_t inner_nops, size_t bw_cacheline_bytes) {

// uncomment the next line to use DC ZVA instructions to do writes
//#define USE_DCZVA

#if defined(__aarch64__) && defined(USE_DCZVA)
    // this just writes one 64-bit dword from each cache line, but is slower.
    for (char * i = p; i < ((char *)p)+bytes; i += bw_cacheline_bytes) {
        asm volatile ("dc zva, %0" : : "r" (i));
#elif defined(__aarch64__) && !defined(USE_DCZVA)
    size_t dummy=0;
    for (size_t i = 0; i < bytes; i += bw_cacheline_bytes) {
        asm volatile ("str %0, [%1, %2]" : : "r" (dummy), "r" (p), "r" (i));
#elif defined(__x86_64__)
    size_t dummy=0;
    for (size_t i = 0; i < bytes; i += bw_cacheline_bytes) {
        // warning untested
        asm volatile ("movq   %0, (%1,%2,1)" : : "r" (dummy), "r" (p), "r" (i));
#endif
        for (size_t j = 0; j < inner_nops; j++) {
            asm volatile ("");
        }
    }
}


void bandwidth_thread (struct bw_thread_info * bw_tinfo) {
    size_t buflen           = bw_tinfo->bw_buflen;
    size_t inner_nops       = bw_tinfo->inner_nops;
    size_t outer_nops       = bw_tinfo->outer_nops;

    size_t iterations       = bw_tinfo->iterations;
    int thread_num          = bw_tinfo->thread_num;
    int cpu                 = bw_tinfo->cpu;                /* cpu on which this thread is to run */
    int bw_write            = bw_tinfo->bw_write;

    unsigned long hwcounter_start = bw_tinfo->hwcounter_start;
    unsigned long hwcounter_stop  = bw_tinfo->hwcounter_stop;

    size_t bw_cacheline_bytes     = bw_tinfo->bw_cacheline_bytes;

    int bw_use_hugepages    = bw_tinfo->bw_use_hugepages;

    unsigned long start_tick, stop_tick, tickdiff;
    double avg_bw = 0.0;
    double cntfreq = (double) read_cntfreq();
    unsigned long bw_samples = 0;

    printf("CPU%d BWTHREAD%d: buflen = %zu, iterations = %zu, inner_nops = %zu, outer_nops = %zu, hwcounter_start = 0x%zx, bw_cacheline_bytes = %zu, bw_use_hugepages = %d, tid = %d\n",
           cpu, thread_num, buflen, iterations, inner_nops, outer_nops, hwcounter_start, bw_cacheline_bytes, bw_use_hugepages, gettid());

    void * mem = do_alloc(buflen, bw_use_hugepages, sysconf(_SC_PAGESIZE));

    // synchronize thread start at the specified HW timer value
    while ((start_tick = read_hwcounter()) < hwcounter_start) {
        ;
    }

    bw_tinfo->actual_hwcounter_start = start_tick;

    printf("CPU%d BWTHREAD%d: started at " HWCOUNTER " = 0x%zx\n", cpu, thread_num, start_tick);

    while ((start_tick = stop_tick = read_hwcounter()) < hwcounter_stop) {

        for (size_t i = 0; i < iterations; i++) {
            if (bw_write) {
                my_write((void *) (((char *) mem)), buflen, inner_nops, bw_cacheline_bytes);
            } else {
                my_read((void *) (((char *) mem)), buflen, inner_nops, bw_cacheline_bytes);
            }
            for (size_t j = 0; j < outer_nops; j++) {
                asm volatile ("");
            }
        }

        stop_tick = read_hwcounter();
        tickdiff = stop_tick - start_tick;

        double bw = iterations * buflen / (tickdiff / cntfreq);

        avg_bw += bw;
        bw_samples++;

        bw /= 1e6;  // MB, not MiB

        printf("CPU%d BWTHREAD%d: %f MB/sec\n", cpu, thread_num, bw);

    }

    bw_tinfo->actual_hwcounter_stop = stop_tick;

    avg_bw /= bw_samples;

    bw_tinfo->avg_bw = avg_bw;
}
