
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARGS_H
#define ARGS_H

typedef struct {
    cpu_set_t lat_cpuset;
    cpu_set_t lat_warmup_cpuset;
    cpu_set_t bw_cpuset;
    double    duration;            // how long to run in seconds
    int show_per_thread_concurrency; // show the per-thread concurrency metrics
    int       delay_seconds_valid; // 1 if delay_seconds is valid instead of delay_ticks
    size_t    delay_ticks;         // HWCOUNTER ticks for thread setup and init
    double    delay_seconds;       // delay in seconds
    double    cycle_time_ns;       // 2600 MHz
    double    mhz;
    unsigned long hwclock_freq;    // frequency in Hz of the hwclock counter
    long estimate_hwclock_freq_cpu;  // cpu on which to estimate hwclock frequency
    long      int random_seedval;
    int       ssbs;   // ssbs = 1 means to go fast. specify -Q to make it not speculate

    size_t    lat_secondary_delay;
    size_t    lat_cacheline_bytes; // cacheline size default is 64 bytes for latency
    size_t    lat_cacheline_count; // default memory size is 1 MiB + 1 cache line
    size_t    lat_iterations;      // (10 x) 10 million iterations
    int       lat_randomize;
    int       has_lat_offset;      // set to 1 if lat_offset is a value specified on the command line.
    size_t    lat_offset;          // when multiple latency threads are specified, how many deploads to advance the secondary thread.
    size_t    lat_cacheline_stride;
    int       lat_use_hugepages;   // use hugepages for latency
    int       lat_shared_memory;   // latency: share memory
    int       lat_shared_memory_init_cpu; // if not set will use lowest numbered CPU of latency threads
    int       lat_clear_cache;     // default do not clear cache on latency loop initialization

    size_t    bw_buflen;
    size_t    bw_inner_nops;
    size_t    bw_outer_nops;
    size_t    bw_iterations;
    size_t    bw_cacheline_bytes;  // cacheline size default is 64 bytes for bandwdith
    int       bw_use_hugepages;    // use hugepages for bandwidth
    int       bw_write;   // bw_write = 1 means to do writes for mem bandwidth instead of reads

} args_t;

void handle_args(int argc, char ** argv, args_t * pargs);

const char * hugepage_map (int enum_param_value);

#endif
