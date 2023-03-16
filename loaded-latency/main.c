
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

#include <sys/prctl.h>
#include <sys/time.h>

#ifdef __aarch64__
#include "cntvct.h"
#endif

#ifdef __x86_64__
#include "rdtsc.h"
#endif

#include "args.h"
#include "alloc.h"
#include "bandwidth.h"
#include "memlatency.h"

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)

// uncomment the next line for debugging latency loop in shared memory mode
// #define DUMP_MEM

#ifdef DUMP_MEM
static void dump_mem(void ** mem, size_t cacheline_count);
#endif

static void thread_set_affinity(int my_cpu_number) __attribute__((noinline));
static void * bw_thread_start(void *arg);
static void * lat_thread_start(void *arg);
static unsigned long max(unsigned long x, unsigned long y);
static unsigned long min(unsigned long x, unsigned long y);
static unsigned long estimate_hwclock_freq(long cpu_num);


// default parameters
args_t args = {
    .duration = 10,       // how long to run in seconds
    .show_per_thread_concurrency = 0, // default is hide the per-thread concurrency metrics
    .delay_seconds = 0.,
    .delay_seconds_valid = 0,   // delay_seconds is not valid
#ifdef __x86_64__
    .delay_ticks = 0x30000000,  // TSC ticks
#else
    .delay_ticks = 200000,      // HWCOUNTER ticks for thread setup and init
#endif
#define CYCLE_TIME_NS (1e9/2600e6)   // 2600 MHz as the default
    .cycle_time_ns = CYCLE_TIME_NS,
    .mhz = 1e3/CYCLE_TIME_NS,
    .hwclock_freq = 0,           // placeholder; if still 0, will use self-determined values
    .estimate_hwclock_freq_cpu = -1, // -1 means don't do the estimation
    .random_seedval = 0,
    .ssbs = 1,            // ssbs = 1 means go fast. specify -Q | --mitigate-spectre-v4 to make it not speculate

    .lat_secondary_delay = 0,
    .lat_cacheline_bytes = 64,   // cacheline size default is 64 bytes for latency
    .lat_cacheline_count = (1024*1024/64)+1, // default memory size is 1 MiB + 1 cache line for 64 byte cachelines
    .lat_iterations = 10000000,  // (10 x) 10 million iterations
    .lat_randomize = 0,
    .has_lat_offset = 0,         // set to 1 if lat_offset is a value specified on the command line.
    .lat_offset = 0,             // when multiple latency threads are specified, how many deploads to advance the secondary thread.
    .lat_cacheline_stride = 1,
    .lat_use_hugepages = HUGEPAGES_NONE,     // use hugepages for latency
    .lat_shared_memory = 0,      // latency: share memory
    .lat_shared_memory_init_cpu = -1,        // latency: cpu on which shared memory will be initialized; if not set will use lowest numbered CPU of latency threads
    .lat_clear_cache = 0,        // default do not clear cache on latency loop initialization

    .bw_buflen = 8192 * 1024,    // 8 MB
    .bw_inner_nops = 0,
    .bw_outer_nops = 0,
    .bw_iterations = 1000,
    .bw_cacheline_bytes = 64,    // cacheline size default is 64 bytes for bandwdith
    .bw_use_hugepages = HUGEPAGES_NONE,      // use hugepages for bandwidth
    .bw_write = 0,

};




int main(int argc, char *argv[]) {
    struct bw_thread_info *bw_tinfo;
    struct lat_thread_info *lat_tinfo;
    int s;

    CPU_ZERO(&args.lat_cpuset);
    CPU_ZERO(&args.lat_warmup_cpuset);
    CPU_ZERO(&args.bw_cpuset);

    pthread_attr_t attr;
    void *res;

    int i;

    handle_args(argc, argv, &args);

    if (args.estimate_hwclock_freq_cpu != -1) {
        unsigned long freq = estimate_hwclock_freq(args.estimate_hwclock_freq_cpu);
        printf("the estimated hwclock frequency on CPU %ld in Hz is %lu\n", args.estimate_hwclock_freq_cpu, freq);
        return 0;
    }


    int num_bw_threads = 0;
    int num_lat_threads = 0;

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &args.bw_cpuset)) {
            printf("Bandwidth thread %d on CPU%d (-B %d)\n", num_bw_threads, i, i);
            num_bw_threads++;
        }
    }

    printf("Total of %d bandwidth threads requested\n", num_bw_threads);

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &args.lat_cpuset)) {
            printf("Latency thread %d on CPU%d   (-l %d)\n", num_lat_threads, i, i);

            if (CPU_ISSET(i, &args.lat_warmup_cpuset)) {
                printf("Latency thread %d will warmup on CPU%d   (-w %d)\n", num_lat_threads, i, i);
            }

            num_lat_threads++;
        } else if (CPU_ISSET(i, &args.lat_warmup_cpuset)) {
            printf("ERROR:  CPU %d was to run warmup latency (-w %d), but it is not to run a latency thread (because -l %d was not specified).\n", i, i, i);
            exit(-1);
        }
    }

    printf("Total of %d latency threads requested\n", num_lat_threads);

    if (args.hwclock_freq == 0) {
        args.hwclock_freq = get_default_cntfreq();
    }

    // recompute delay_ticks if delay_seconds_valid

    if (args.delay_seconds_valid) {
        args.delay_ticks = args.delay_seconds * read_cntfreq();
    } else {
        args.delay_seconds = args.delay_ticks / (double) read_cntfreq();
    }

    // random seed based on time
    struct timeval t;
    gettimeofday(&t, NULL);

    if (args.random_seedval == 0) {
        args.random_seedval = t.tv_sec * 1000000 + t.tv_usec;
    }

    srand48(args.random_seedval);


    // compute lat_offset

    if (! args.has_lat_offset && num_lat_threads > 1) {
        args.lat_offset = args.lat_cacheline_count / num_lat_threads;
    }

    // SSBS = speculative store bypass (is) safe

    // if -Q flag is NOT set, spec store bypass is safe, enable speculation feature
    // if -Q flag is set, spec store bypass is not safe, disable speculation feature

    int ssbs_set_retval = prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS,
            args.ssbs ? PR_SPEC_ENABLE : PR_SPEC_DISABLE, 0, 0);
    int ssbs_get_retval = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, 0, 0, 0);

    printf("main program pid         = %d\n", getpid());
    printf("duration            (-D) = %f seconds\n", args.duration);
    printf("delay_seconds       (-d) = %f seconds (%zu " HWCOUNTER " ticks at cntfreq=%lu)\n",
            args.delay_seconds, args.delay_ticks, read_cntfreq());
    printf("random_seedval      (-S) = %ld\n", args.random_seedval);
    /* XXX: frequency is NOT auto-detected by this program */
    printf("cycle_time_ns       (-t) = %.6f ((-f) %.3f MHz)\n", args.cycle_time_ns, args.mhz);
    printf("ssbs                (-Q) = speculation feature: "
            "requested %s (retval = 0x%x) "
            "status is %s (retval = 0x%x)\n",
            args.ssbs ? "enabled" : "disabled", ssbs_set_retval,
            ssbs_get_retval & PR_SPEC_ENABLE ? "enabled" : "disabled", ssbs_get_retval);

    printf("\n");
    printf("bandwidth settings:\n");
    printf("bw_iterations       (-I) = %zu\n", args.bw_iterations);
    printf("bw_buflen           (-L) = %zu (%.3f (1e6) megabytes)\n", args.bw_buflen, args.bw_buflen / 1000000.);
    printf("fine loop delay     (-F) = %zu\n", args.bw_inner_nops);
    printf("coarse loop delay   (-C) = %zu\n", args.bw_outer_nops);
    printf("bw_cacheline_bytes  (-Z) = %zu\n", args.bw_cacheline_bytes);
    printf("bw_use_hugepages    (-H) = %d (hugepages = %s)\n", args.bw_use_hugepages, hugepage_map(args.bw_use_hugepages));
    printf("bw_write            (-W) = %d\n", args.bw_write);

    printf("\n");
    printf("latency settings:\n");
    printf("lat_cacheline_count (-n) = %zu (%.3f (1e6) megabytes)\n", args.lat_cacheline_count, args.lat_cacheline_count * args.lat_cacheline_bytes / 1000000.);
    printf("lat_iterations      (-i) = %zu\n", args.lat_iterations);
    printf("lat_offset          (-o) = %zu\n", args.lat_offset);
    printf("lat_secondary_delay (-e) = %zu\n", args.lat_secondary_delay);
    printf("lat_randomize       (-r) = %d\n", args.lat_randomize);
    printf("lat_use_hugepages   (-h) = %d (hugepages = %s)\n", args.lat_use_hugepages, hugepage_map(args.lat_use_hugepages));
    printf("lat_shared_memory   (-s) = %d\n", args.lat_shared_memory);
    printf("lat_shared_memory_init_cpu(-u) = %d\n", args.lat_shared_memory_init_cpu);
    printf("lat_clear_cache     (-c) = %d\n", args.lat_clear_cache);
    printf("lat_cacheline_stride(-j) = %zu\n", args.lat_cacheline_stride);
    /* XXX: no machine with other than a 64 byte CL is easily available to test it */
    printf("lat_cacheline_bytes (-z) = %zu\n", args.lat_cacheline_bytes);
    printf("\n");


    /* Initialize thread creation attributes */

    s = pthread_attr_init(&attr);
    if (s != 0)
        handle_error_en(s, "pthread_attr_init");


    unsigned long hwcounter_now = read_hwcounter();
    unsigned long hwcounter_start = hwcounter_now + args.delay_ticks;
    unsigned long hwcounter_stop = hwcounter_start + read_cntfreq() * args.duration;


    /* set up bandwidth threads */

    bw_tinfo = calloc(num_bw_threads, sizeof(struct bw_thread_info));
    if (bw_tinfo == NULL)
        handle_error("calloc");

    size_t bw_thread_num = 0;

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &args.bw_cpuset)) {
            bw_tinfo[bw_thread_num].hwcounter_start = hwcounter_start;
            bw_tinfo[bw_thread_num].hwcounter_stop = hwcounter_stop;
            bw_tinfo[bw_thread_num].thread_num = bw_thread_num;
            bw_tinfo[bw_thread_num].cpu = i;
            bw_tinfo[bw_thread_num].bw_buflen = args.bw_buflen;
            bw_tinfo[bw_thread_num].inner_nops = args.bw_inner_nops;
            bw_tinfo[bw_thread_num].outer_nops = args.bw_outer_nops;
            bw_tinfo[bw_thread_num].iterations = args.bw_iterations;
            bw_tinfo[bw_thread_num].bw_cacheline_bytes = args.bw_cacheline_bytes;
            bw_tinfo[bw_thread_num].bw_use_hugepages = args.bw_use_hugepages;
            bw_tinfo[bw_thread_num].bw_write = args.bw_write;
            sprintf(bw_tinfo[bw_thread_num].threadname, "bw_thread_%zu", bw_thread_num);
            bw_thread_num++;
        }
    }


    /* set up latency threads */

    if (num_lat_threads == 0) {
        printf("No latency threads requested!\n");
    }

    lat_tinfo = calloc(num_lat_threads, sizeof(struct lat_thread_info));
    if (lat_tinfo == NULL)
        handle_error("calloc");

    if (args.lat_cacheline_stride > args.lat_cacheline_count) {
        printf("ERROR: lat_cacheline_stride > lat_cacheline_count\n");
        exit(-1);
    }

    if (args.lat_cacheline_stride == 0) {
        printf("ERROR: lat_cacheline_stride == 0; if you really want jump to self, use lat_cacheline_stride == num_cache_lines\n");
        exit(-1);
    }

    if (args.lat_cacheline_stride == args.lat_cacheline_count && args.lat_randomize) {
        printf("ERROR: lat_cacheline_stride == cacheline_count but with randomize = 1, can't do this\n");
        exit(-1);
    }

    void ** mem = NULL;

    if (num_lat_threads > 0 && args.lat_shared_memory) {

        int latency_thread_to_setup_memory = args.lat_shared_memory_init_cpu;

        // if -u is not specified to select the CPU on which to init the
        // shared memory loop, find the lowest CPU of a latency thread.

        if (latency_thread_to_setup_memory == -1) {
            for (i = 0; i < CPU_SETSIZE; i++) {
                if (CPU_ISSET(i, &args.lat_cpuset)) {
                    latency_thread_to_setup_memory = i;
                    break;
                }
            }
        }

        printf("latency_thread_to_setup_memory = %d\n", latency_thread_to_setup_memory);

        // set affinity for the threads that set up the shared memory latency loop

        cpu_set_t main_thread_cpu_mask;
        cpu_set_t a_latency_thread_cpu_mask;

        CPU_ZERO(&a_latency_thread_cpu_mask);
        CPU_SET(latency_thread_to_setup_memory, &a_latency_thread_cpu_mask);

        // save affinity of main thread
        if (0 != sched_getaffinity(0, sizeof(cpu_set_t), &main_thread_cpu_mask)) {
            handle_error("sched_getaffinity");
        }

        // set up shared memory latency loop on a CPU that will run the loop
        if (0 != sched_setaffinity(0, sizeof(cpu_set_t), &a_latency_thread_cpu_mask)) {
            handle_error("sched_setaffinity");
        }

        mem = lat_initialize(args.lat_cacheline_bytes, args.lat_cacheline_count, args.lat_randomize,
                args.lat_clear_cache, args.lat_cacheline_stride, args.lat_use_hugepages);

        // restore affinity of main thread
        if (0 != sched_setaffinity(0, sizeof(cpu_set_t), &main_thread_cpu_mask)) {
            handle_error("sched_setaffinity");
        }

#ifdef DUMP_MEM
        printf("the map before:\n");
        dump_mem(mem, args.lat_cacheline_count);
#endif
    }


    size_t lat_thread_num = 0;

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &args.lat_cpuset)) {
            lat_tinfo[lat_thread_num].hwcounter_start = hwcounter_start + (lat_thread_num > 0 ? args.lat_secondary_delay : 0);
            lat_tinfo[lat_thread_num].hwcounter_stop = hwcounter_stop + (lat_thread_num > 0 ? args.lat_secondary_delay : 0);
            lat_tinfo[lat_thread_num].thread_num = lat_thread_num;
            lat_tinfo[lat_thread_num].cpu = i;
            if (CPU_ISSET(i, &args.lat_warmup_cpuset)) {
                lat_tinfo[lat_thread_num].warmup = 1;
            } else {
                lat_tinfo[lat_thread_num].warmup = 0;
            }
            lat_tinfo[lat_thread_num].cacheline_stride = args.lat_cacheline_stride;
            lat_tinfo[lat_thread_num].randomize = args.lat_randomize;
            lat_tinfo[lat_thread_num].use_hugepages = args.lat_use_hugepages;
            lat_tinfo[lat_thread_num].lat_cacheline_bytes = args.lat_cacheline_bytes;
            lat_tinfo[lat_thread_num].cacheline_count = args.lat_cacheline_count;
            lat_tinfo[lat_thread_num].iterations = args.lat_iterations;
            lat_tinfo[lat_thread_num].cycle_time_ns = args.cycle_time_ns;
            lat_tinfo[lat_thread_num].mem = mem;
            lat_tinfo[lat_thread_num].lat_clear_cache = args.lat_clear_cache;
            if (lat_thread_num > 0) {
                lat_tinfo[lat_thread_num].lat_offset = args.lat_offset;
            } else {
                lat_tinfo[lat_thread_num].lat_offset = 0;
            }
            sprintf(lat_tinfo[lat_thread_num].threadname, "lat_thread_%zu", lat_thread_num);
            lat_thread_num++;
        }
    }

    /* start all threads */

    // start latency threads first because initialization can take a while
    for (i = 0; i < num_lat_threads; i++) {
        s = pthread_create(&lat_tinfo[i].thread_id, &attr,
                &lat_thread_start, &lat_tinfo[i]);

        pthread_setname_np(lat_tinfo[i].thread_id, &(lat_tinfo[i].threadname[0]));

        if (s != 0)
            handle_error_en(s, "pthread_create");
    }

    for (i = 0; i < num_bw_threads; i++) {
        s = pthread_create(&bw_tinfo[i].thread_id, &attr,
                &bw_thread_start, &bw_tinfo[i]);

        pthread_setname_np(bw_tinfo[i].thread_id, &(bw_tinfo[i].threadname[0]));

        if (s != 0)
            handle_error_en(s, "pthread_create");
    }


    /* stop all threads */

    double total_bandwidth = 0.0;
    for (i = 0; i < num_bw_threads; i++) {
        s = pthread_join(bw_tinfo[i].thread_id, &res);
        if (s != 0)
            handle_error_en(s, "pthread_join");

        total_bandwidth += bw_tinfo[i].avg_bw;

        printf("Joined BWTHREAD%d, avg_bw = %f MB/sec\n", bw_tinfo[i].thread_num, bw_tinfo[i].avg_bw / 1e6);
    }

    double average_latency = 0.0;
    unsigned long latency_count = 0;

    for (i = 0; i < num_lat_threads; i++) {
        s = pthread_join(lat_tinfo[i].thread_id, &res);
        if (s != 0)
            handle_error_en(s, "pthread_join");

        printf("Joined LATTHREAD%d, avg_latency = %f ns\n", lat_tinfo[i].thread_num, lat_tinfo[i].avg_latency);
        average_latency += lat_tinfo[i].avg_latency;
        latency_count++;
    }

    average_latency /= latency_count;

    printf("\n");
    printf("Total Bandwidth = %.6f MB/sec\n", total_bandwidth / 1e6);
    printf("Average Latency = %.6f ns\n\n", average_latency);


    /* compute overhang between latency and bandwidth threads */

    /*
     * It is desirable to have the bandwidth and latency threads to start
     * and finish at very close to the same time in order to provide a
     * measurement of concurrent operation.  For example, if the bandwidth
     * threads finish before the latency thread does, the latency thread
     * may be able to execute unencumbered and will report a lower-than-
     * typical latency.
     *
     * The parent thread does not coordinate the child threads for
     * concurrent operation using barriers because barriers do not
     * guarantee concurrent operation.
     *
     * The -d / --delay flag adjusts when the threads are to begin their
     * operation.  It should be specified long enough for all of the
     * threads to have been created and initialized.
     *
     * The -D / --duration flag adjusts how long the threads should run.
     * Typically, a longer the duration reduces the percentage error of
     * the overhang.
     *
     * -i / --lat-iterations and -I / --bw-iterations specify the number of
     * iterations of work in between when a thread decides it has complete its
     * work and then join with the parent.
     *
     * The latency threads will run -i iterations before checking whether
     * it is time to join with the parent.  The smaller the -i value, the
     * more frequently the checks are made.
     *
     * The bandwidth threads will run -I iterations before checking whether it
     * is time to join with the parent.  The smaller the -I value, the more
     * frequently the checks are made.
     */


    printf("concurrency coverage metrics:\n\n");

    // concurrency coverage for bandwidth

    unsigned long bw_hwcounter_start_min = -1;
    unsigned long bw_hwcounter_start_max = 0;

    unsigned long bw_hwcounter_stop_min = -1;
    unsigned long bw_hwcounter_stop_max = 0;

    for (i = 0; i < num_bw_threads; i++) {

        long bw_start_diff = bw_tinfo[i].actual_hwcounter_start - bw_tinfo[i].hwcounter_start;
        long bw_stop_diff  = bw_tinfo[i].actual_hwcounter_stop  - bw_tinfo[i].hwcounter_stop ;

        double bw_start_diff_seconds = bw_start_diff / (double) read_cntfreq();
        double bw_stop_diff_seconds  = bw_stop_diff  / (double) read_cntfreq();

        long bw_actual_stop_start_diff = bw_tinfo[i].actual_hwcounter_stop - bw_tinfo[i].actual_hwcounter_start;
        double bw_actual_stop_start_diff_seconds = bw_actual_stop_start_diff / (double) read_cntfreq();

        bw_hwcounter_start_min = min(bw_tinfo[i].actual_hwcounter_start, bw_hwcounter_start_min);
        bw_hwcounter_start_max = max(bw_tinfo[i].actual_hwcounter_start, bw_hwcounter_start_max);

        bw_hwcounter_stop_min  = min(bw_tinfo[i].actual_hwcounter_stop , bw_hwcounter_stop_min );
        bw_hwcounter_stop_max  = max(bw_tinfo[i].actual_hwcounter_stop , bw_hwcounter_stop_max );

        if (! args.show_per_thread_concurrency) {
            continue;
        }

        printf("bw_tinfo[%d].hwcounter_start        = 0x%lx\n", i, bw_tinfo[i].hwcounter_start);
        printf("bw_tinfo[%d].actual_hwcounter_start = 0x%-16lx  diff = %ld (%f seconds)\n",
                i, bw_tinfo[i].actual_hwcounter_start, bw_start_diff, bw_start_diff_seconds);
        printf("bw_tinfo[%d].hwcounter_stop         = 0x%lx\n", i, bw_tinfo[i].hwcounter_stop);
        printf("bw_tinfo[%d].actual_hwcounter_stop  = 0x%-16lx  diff = %ld (%f seconds), duration = %ld (%f seconds)\n\n",
                i, bw_tinfo[i].actual_hwcounter_stop, bw_stop_diff, bw_stop_diff_seconds,
                bw_actual_stop_start_diff, bw_actual_stop_start_diff_seconds);

    }


    // concurrency coverage for latency

    unsigned long lat_hwcounter_start_min = -1;
    unsigned long lat_hwcounter_start_max = 0;

    unsigned long lat_hwcounter_stop_min = -1;
    unsigned long lat_hwcounter_stop_max = 0;

    for (i = 0; i < num_lat_threads; i++) {

        long lat_start_diff = lat_tinfo[i].actual_hwcounter_start - lat_tinfo[i].hwcounter_start;
        long lat_stop_diff  = lat_tinfo[i].actual_hwcounter_stop  - lat_tinfo[i].hwcounter_stop ;

        double lat_start_diff_seconds = lat_start_diff / (double) read_cntfreq();
        double lat_stop_diff_seconds  = lat_stop_diff  / (double) read_cntfreq();

        long lat_actual_stop_start_diff = lat_tinfo[i].actual_hwcounter_stop - lat_tinfo[i].actual_hwcounter_start;
        double lat_actual_stop_start_diff_seconds  = lat_actual_stop_start_diff  / (double) read_cntfreq();

        lat_hwcounter_start_min = min(lat_tinfo[i].actual_hwcounter_start, lat_hwcounter_start_min);
        lat_hwcounter_start_max = max(lat_tinfo[i].actual_hwcounter_start, lat_hwcounter_start_max);

        lat_hwcounter_stop_min  = min(lat_tinfo[i].actual_hwcounter_stop , lat_hwcounter_stop_min );
        lat_hwcounter_stop_max  = max(lat_tinfo[i].actual_hwcounter_stop , lat_hwcounter_stop_max );

        if (! args.show_per_thread_concurrency) {
            continue;
        }

        printf("lat_tinfo[%d].hwcounter_start        = 0x%lx\n", i, lat_tinfo[i].hwcounter_start);
        printf("lat_tinfo[%d].actual_hwcounter_start = 0x%-16lx  diff = %ld (%f seconds)\n",
                i, lat_tinfo[i].actual_hwcounter_start, lat_start_diff, lat_start_diff_seconds);
        printf("lat_tinfo[%d].hwcounter_stop         = 0x%-16lx\n", i, lat_tinfo[i].hwcounter_stop);
        printf("lat_tinfo[%d].actual_hwcounter_stop  = 0x%-16lx  diff = %ld (%f seconds), duration = %ld (%f seconds)\n\n",
                i, lat_tinfo[i].actual_hwcounter_stop, lat_stop_diff, lat_stop_diff_seconds,
                lat_actual_stop_start_diff, lat_actual_stop_start_diff_seconds);

    }

    // compute latency and bandwidth start/stop spreads

    // min = oldest counter value, max = youngest counter value

    long bw_start_spread_ticks = bw_hwcounter_start_max - bw_hwcounter_start_min;
    long bw_stop_spread_ticks = bw_hwcounter_stop_max - bw_hwcounter_stop_min;

    double bw_start_spread_ticks_seconds = bw_start_spread_ticks / (double) read_cntfreq();
    double bw_stop_spread_ticks_seconds = bw_stop_spread_ticks / (double) read_cntfreq();

    long lat_start_spread_ticks = lat_hwcounter_start_max - lat_hwcounter_start_min;
    long lat_stop_spread_ticks = lat_hwcounter_stop_max - lat_hwcounter_stop_min;

    double lat_start_spread_ticks_seconds = lat_start_spread_ticks / (double) read_cntfreq();
    double lat_stop_spread_ticks_seconds = lat_stop_spread_ticks / (double) read_cntfreq();

    printf("bw_hwcounter_start_max  = 0x%lx\n", bw_hwcounter_start_max);
    if (bw_hwcounter_start_max == 0) {
        printf("bw_hwcounter_start_min  = 0x%-16lx  max-min diff = n/a\n", bw_hwcounter_start_min);
    } else {
        printf("bw_hwcounter_start_min  = 0x%-16lx  max-min diff = %lu (%f seconds)\n", bw_hwcounter_start_min, bw_start_spread_ticks, bw_start_spread_ticks_seconds);
    }

    printf("lat_hwcounter_start_max = 0x%lx\n", lat_hwcounter_start_max);
    if (lat_hwcounter_start_max == 0) {
        printf("lat_hwcounter_start_min = 0x%-16lx  max-min diff = n/a\n\n", lat_hwcounter_start_min);
    } else {
        printf("lat_hwcounter_start_min = 0x%-16lx  max-min diff = %lu (%f seconds)\n\n", lat_hwcounter_start_min, lat_start_spread_ticks, lat_start_spread_ticks_seconds);
    }

    printf("bw_hwcounter_stop_max   = 0x%lx\n", bw_hwcounter_stop_max);
    if (bw_hwcounter_stop_max == 0) {
        printf("bw_hwcounter_stop_min   = 0x%-16lx  max-min diff = n/a\n", bw_hwcounter_stop_min);
    } else {
        printf("bw_hwcounter_stop_min   = 0x%-16lx  max-min diff = %ld (%f seconds)\n", bw_hwcounter_stop_min, bw_stop_spread_ticks, bw_stop_spread_ticks_seconds);
    }

    printf("lat_hwcounter_stop_max  = 0x%lx\n", lat_hwcounter_stop_max);
    if (lat_hwcounter_stop_max == 0) {
        printf("lat_hwcounter_stop_min  = 0x%-16lx  max-min diff = n/a\n\n", lat_hwcounter_stop_min);
    } else {
        printf("lat_hwcounter_stop_min  = 0x%-16lx  max-min diff = %ld (%f seconds)\n\n", lat_hwcounter_stop_min, lat_stop_spread_ticks, lat_stop_spread_ticks_seconds);
    }

    // compute the spread of bandwidth and latency start ticks and stop ticks

    long bw_lat_start_spread_ticks = bw_hwcounter_start_min - lat_hwcounter_start_min;
    long bw_lat_stop_spread_ticks = bw_hwcounter_stop_min - lat_hwcounter_stop_min;

    double bw_lat_start_spread_seconds = bw_lat_start_spread_ticks / (double) read_cntfreq();
    double bw_lat_stop_spread_seconds = bw_lat_stop_spread_ticks / (double) read_cntfreq();

    if (bw_hwcounter_start_min == -1 || lat_hwcounter_start_min == -1) {
        printf("bw_lat_start_spread_ticks = n/a\n");
    } else {
        printf("bw_lat_start_spread_ticks = %ld (%f seconds)\n", bw_lat_start_spread_ticks, bw_lat_start_spread_seconds);
    }

    if (bw_hwcounter_stop_min == -1 || lat_hwcounter_stop_min == -1) {
        printf("bw_lat_stop_spread_ticks  = n/a\n\n");
    } else {
        printf("bw_lat_stop_spread_ticks  = %ld (%f seconds)\n\n", bw_lat_stop_spread_ticks, bw_lat_stop_spread_seconds);
    }

    s = pthread_attr_destroy(&attr);
    if (s != 0)
        handle_error_en(s, "pthread_attr_destroy");

#ifdef DUMP_MEM
    if (args.lat_shared_memory) {
        printf("the map after:\n");
        dump_mem(mem, args.lat_cacheline_count);
    }
#endif

    return 0;
}

// -------------------------------------------

#ifdef DUMP_MEM
static void dump_mem(void ** mem, size_t cacheline_count) {
    for (size_t i = 0; i < cacheline_count; i++) {
        printf("%zu, %p @ %p, index, %zu\n", i, mem[i * 8], &(mem[i*8]), (size_t) mem[i * 8 + 2]);
    }
}
#endif

static void thread_set_affinity(int my_cpu_number) {
    int s, j;
    cpu_set_t cpuset;
    pthread_t thread;

    thread = pthread_self();

    /* Set affinity mask to be self */

    CPU_ZERO(&cpuset);

    CPU_SET(my_cpu_number, &cpuset);

    s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (s != 0)
        handle_error_en(s, "pthread_setaffinity_np");

    /* Check the actual affinity mask assigned to the thread */

    s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (s != 0)
        handle_error_en(s, "pthread_getaffinity_np");

    size_t num_bits_set = 0;

    for (j = 0; j < CPU_SETSIZE; j++) {
        if (CPU_ISSET(j, &cpuset)) {
            // printf("    CPU %d\n", j);
            num_bits_set++;
            if (j != my_cpu_number) {
                printf(" wanted CPU%d but got CPU%d\n", my_cpu_number, j);
            }
        }
    }

    if (num_bits_set > 1) {
        printf("WARNING: %zu bits were set for CPU %d\n", num_bits_set, my_cpu_number);
    }

    if (num_bits_set == 0) {
        printf("ERROR: 0 bits were set for CPU %d! EXITING!\n", my_cpu_number);
        exit(-1);
    }
}

static void * bw_thread_start(void *arg) {
    struct bw_thread_info *bw_tinfo = arg;

    thread_set_affinity(bw_tinfo->cpu);

    bandwidth_thread(bw_tinfo);

    return bw_tinfo;
}

static void * lat_thread_start(void *arg) {
    struct lat_thread_info *lat_tinfo = arg;

    thread_set_affinity(lat_tinfo->cpu);

    latency_thread(lat_tinfo);

    return lat_tinfo;
}

static unsigned long max(unsigned long x, unsigned long y) {
    if (x > y) {
        return x;
    }
    return y;
}

static unsigned long min(unsigned long x, unsigned long y) {
    if (x < y) {
        return x;
    }
    return y;
}

static unsigned long estimate_hwclock_freq(long cpu_num) {

    unsigned long n = 10;
    unsigned long hwcounter_start, hwcounter_stop, hwcounter_diff;
    unsigned long hwcounter_average = 0;

    printf("estimating hwclock frequency on cpu %ld for %lu iterations\n", cpu_num, n);

    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET(cpu_num, &cpu_mask);

    if (0 != sched_setaffinity(0, sizeof(cpu_mask), &cpu_mask)) {
        handle_error("sched_setaffinity");
    }

    for (unsigned long i = 0; i < n; i++) {

        struct timeval ts_a, ts_b, ts_diff;

        hwcounter_start = read_hwcounter();
        gettimeofday(&ts_a, NULL);

        do {
            gettimeofday(&ts_b, NULL);
            timersub(&ts_b, &ts_a, &ts_diff);
        } while (ts_diff.tv_sec < 1);

        hwcounter_stop = read_hwcounter();

        hwcounter_diff = hwcounter_stop - hwcounter_start;

        printf("hwcounter_diff = %lu\n", hwcounter_diff);

        hwcounter_average += hwcounter_diff;
    }

    hwcounter_average /= (double) n;

    // printf("hwcounter_average = %lu\n", hwcounter_average);

    return hwcounter_average;
}
