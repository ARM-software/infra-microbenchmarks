
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MEMLATENCY_H
#define MEMLATENCY_H

struct lat_thread_info {
    pthread_t     thread_id;
    unsigned long hwcounter_start;
    unsigned long hwcounter_stop;
    unsigned long actual_hwcounter_start;   // output
    unsigned long actual_hwcounter_stop;    // output
    int           thread_num;
    int           cpu;              // cpu on which this thread is run
    int           randomize;
    int           warmup;
    size_t        cacheline_stride;
    int           use_hugepages;
    int           lat_clear_cache;
    size_t        lat_cacheline_bytes;
    size_t        cacheline_count;
    size_t        iterations;
    size_t        lat_offset;
    double        cycle_time_ns;
    double        avg_latency;              // output
    void **       mem;
    size_t        lat_cacheline_size;
    char          threadname[32];
};

void ** lat_initialize(size_t cacheline_bytes,
        size_t cacheline_count, int randomize, int clear_cache, size_t cachline_stride, int use_hugepages);

void latency_thread (struct lat_thread_info * lat_tinfo);

#endif
