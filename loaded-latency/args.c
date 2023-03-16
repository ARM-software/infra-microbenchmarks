
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sched.h>
#include <stdio.h>
#include <strings.h>

#include "args.h"
#include "alloc.h"

static const struct {
    const char * size_string;
    const int enum_param_value;
} hugepage_mapping[] = {
    { "none", HUGEPAGES_NONE },
    { "0", HUGEPAGES_NONE },
    { "default", HUGEPAGES_DEFAULT },
    { "1", HUGEPAGES_DEFAULT },
    { "64K", HUGEPAGES_64K },
    { "64KB", HUGEPAGES_64K },
    { "2M", HUGEPAGES_2M },
    { "2MB", HUGEPAGES_2M },
    { "32M", HUGEPAGES_32M },
    { "32MB", HUGEPAGES_32M },
    { "512M", HUGEPAGES_512M },
    { "512MB", HUGEPAGES_512M },
    { "1G", HUGEPAGES_1G },
    { "1GB", HUGEPAGES_1G },
    { "16G", HUGEPAGES_16G },
    { "16GB", HUGEPAGES_16G },
};

const size_t num_hugepage_mappings = sizeof(hugepage_mapping) / sizeof(hugepage_mapping[0]);

const char * hugepage_map (int enum_param_value) {
    for (size_t i = 0; i < num_hugepage_mappings; i++) {
        if (hugepage_mapping[i].enum_param_value == enum_param_value) {
            return hugepage_mapping[i].size_string;
        }
    }
    return NULL;
}

static int parse_hugepage_parameter(const char opt, const char * optarg) {
    int use_hugepages = HUGEPAGES_NONE;
    size_t i;

    if (0 == strcasecmp(optarg, "help")) {
        printf("hugepage sizes:\n");
        goto PRINT_HUGEPAGES;
    }

    // search for one of the strings in hugepage_mapping[]
    for (i = 0; i < num_hugepage_mappings; i++) {
        if (0 == strcasecmp(optarg, hugepage_mapping[i].size_string)) {
            use_hugepages = hugepage_mapping[i].enum_param_value;
            break;
        }
    }

    // a string size_string was not found, parse for the enum value
    if (i == num_hugepage_mappings) {
        use_hugepages = strtoul(optarg, NULL, 0);
    }

    if (use_hugepages >= HUGEPAGES_MAX_ENUM || use_hugepages < HUGEPAGES_NONE) {
        printf("Error: unknown -%c hugepage parameter %s\n", opt, optarg);
        printf("param\t  hugepage size\n");
PRINT_HUGEPAGES:
        for (i = 0; i < num_hugepage_mappings; i++) {
            printf("%d\t= %s\n", hugepage_mapping[i].enum_param_value, hugepage_mapping[i].size_string);
        }
        exit(-1);
    }

    return use_hugepages;
}

static void print_help(void) {
    printf(
"./loaded-latency [args]\n"
"\n"
" -D | --duration              seconds     how many seconds to run\n"
" -S | --random-seed           seedval     set random seed to seedval\n"
" -d | --delay-seconds         seconds     how many seconds to wait for threads to start together\n"
"      --delay-ticks           ticks       how many HWCOUNTER ticks to wait for threads to start together\n"
"      --show-per-thread-concurrency       show per-thread concurrency metrics\n"
" -Q | --mitigate-spectre-v4               enable mitigation for Spectre v4 (SSBD=1 or SSBS=0) using prctl()\n"
" -q | --hwclock-freq          freq_hz     frequency in Hz of the hwclock counter\n"
"      --estimate-hwclock-freq cpu_num     measure and estimate the hardware clock frequency in Hz on CPU cpu_num\n"
" -f | --cpu-freq-mhz          frequency   CPU frequency in MHz for calculating cycles\n"
" -t | --cpu-cycle-time-ns     nanoseconds CPU cycle time in nanoseconds for calculating cycles\n"
"             CPU frequency and cycle time are NOT auto-detected!\n"
"             The last -f or -t overrides earlier parameters for computing cycles.\n"
"\n"
"latency flags:\n"
" -l | --lat-cpu               cpu_num  CPU on which to run a latency thread.  Repeat for additional CPUs.\n"
" -n | --lat-cacheline-count   count    number of sequential cachelines of memory to use for latency measurement\n"
" -e | --lat-secondary-delay   ticks    how many additional ticks for secondary latency threads to start\n"
" -i | --lat-iterations        iters    number of iterations between latency measurement interim reports\n"
" -z | --lat-cacheline-bytes   bytes    cacheline length for latency measurement\n"
" -j | --lat-cacheline-stride  count    number of cachelines to skip between loads for latency measurement\n"
" -o | --lat-offset            count    number of deploads to advance secondary latency threads\n"
" -c | --lat-clear-cache                clear caches before latency run\n"
" -r | --lat-randomize                  randomize ordering of dependent loads\n"
" -h | --lat-use-hugepages     size     hugepage size to use for latency. Use \"-h help\" to show known sizes\n"
" -w | --lat-warmup-cpu        cpu_num  on which CPU to warm up latency loop (repeat for additional CPUs)\n"
" -s | --lat-shared-memory              use the same memory for all latency threads\n"
" -u | --lat-shared-memory-init-cpu cpu_num   on which CPU to initialize the latency shared memory\n"
"\n"
"bandwidth flags:\n"
" -B | --bw-cpu                cpu_num  CPU on which to run a bandwidth thread.  Repeat for additional CPUs.\n"
" -L | --bw-buflen             bytes    memory buffer size for bandwidth loop\n"
" -I | --bw-iterations         iters    iterations of the bandwidth loop to run between interim reports\n"
" -F | --bw-fine-delay         count    bandwidth fine delay (inner loop nops).  Increase to slow bandwidth.\n"
" -C | --bw-coarse-delay       count    bandwidth coarse delay (coarse loop nops).  Increase to slow bandwidth.\n"
" -H | --bw-use-hugepages      size     hugepage size to use for bandwidth. Use \"-H help\" to show known sizes.\n"
" -Z | --bw-cacheline-bytes    bytes    cacheline length for bandwidth memory region size\n"
" -W | --bw-write                       instead of reads, use writes for memory bandwidth traffic\n"
"\n"
" --help                                this screen\n"
"\n"
"Example using bash arithmetic for 64MB latency loop and 96MB bandwidth buffer:\n"
"\n"
"./loaded-latency --lat-cpu 0 --lat-cacheline-count $((64*1024*1024/64)) --lat-iterations 100000 --lat-randomize \\\n"
"   --bw-cpu 1 --bw-buflen $((96*1024*1024)) --bw-fine-delay 100 --bw-iterations 30\n"
"\n"
    );

    exit(-1);
}

void handle_args(int argc, char ** argv, args_t * pargs) {

    enum {
        help_val = 1,
        estimate_hwclock_freq_val = 2,
        delay_ticks_val = 3,
        show_per_thread_concurrency_val = 4
    };

    static struct option long_options[] = {
        // *name                has_arg         *flag       val (returned or stored thru *flag)
        {"duration",            required_argument,  0,      'D'},
        {"random-seed",         required_argument,  0,      'S'},
        {"delay-seconds",       required_argument,  0,      'd'},
        {"delay-ticks",         required_argument,  0,      delay_ticks_val},
        {"show-per-thread-concurrency",no_argument, 0,      show_per_thread_concurrency_val},
        {"mitigate-spectre-v4", no_argument,        0,      'Q'},
        {"hwclock-freq",        required_argument,  0,      'q'},
        {"estimate-hwclock-freq",required_argument, 0,      estimate_hwclock_freq_val},
        {"cpu-freq-mhz",        required_argument,  0,      'f'},
        {"cpu-cycle-time-ns",   required_argument,  0,      't'},

        // latency flags
        {"lat-cpu",             required_argument,  0,      'l'},
        {"lat-cacheline-count", required_argument,  0,      'n'},
        {"lat-secondary-delay", required_argument,  0,      'e'},
        {"lat-iterations",      required_argument,  0,      'i'},
        {"lat-cacheline-bytes", required_argument,  0,      'z'},
        {"lat-cacheline-stride",required_argument,  0,      'j'},
        {"lat-offset",          required_argument,  0,      'o'},
        {"lat-clear-cache",     no_argument,        0,      'c'},
        {"lat-randomize",       no_argument,        0,      'r'},
        {"lat-use-hugepages",   required_argument,  0,      'h'},
        {"lat-warmup-cpu",      required_argument,  0,      'w'},
        {"lat-shared-memory",   no_argument,        0,      's'},
        {"lat-shared-memory-init-cpu", required_argument, 0, 'u'},

        // bandwidth flags
        {"bw-cpu",              required_argument,  0,      'B'},
        {"bw-iterations",       required_argument,  0,      'I'},
        {"bw-buflen",           required_argument,  0,      'L'},
        {"bw-fine-delay",       required_argument,  0,      'F'},
        {"bw-coarse-delay",     required_argument,  0,      'C'},
        {"bw-use-hugepages",    required_argument,  0,      'H'},
        {"bw-cacheline-bytes",  required_argument,  0,      'Z'},
        {"bw-write",            no_argument,        0,      'W'},

        {"help",                no_argument,        0,      help_val},
        {0,                     0,                  0,      0}
    };

    long cpu;

    while (1) {

        int c = getopt_long(argc, argv, "D:S:d:Qq:f:t:l:n:e:i:z:j:o:crh:w:su:B:I:L:F:C:H:Z:W", long_options, NULL);

        switch (c) {

            case -1:    // all command line parameters have been parsed
                return;

            case 0:
                // getopt_long() set the value through *flag
                break;

         // ---- general flags ------------------------------------------------------------------------------
            case help_val:
            case '?':
            default:
                print_help();  // does not return
                break;

            case 'D':  // --duration seconds    : how many seconds to run
                pargs->duration = strtod(optarg, NULL);
                break;

            case 'S':  // --random-seed seedval
                pargs->random_seedval = strtoul(optarg, NULL, 0);
                break;

            case delay_ticks_val: // --delay-ticks ticks : how many HWCOUNTER ticks to wait for threads to start
                pargs->delay_ticks = strtoul(optarg, NULL, 0);
                if (pargs->delay_seconds_valid) {
                    printf("WARNING: --delay-ticks will override earlier --delay-seconds\n");
                }
                pargs->delay_seconds_valid = 0;
                break;

            case 'd':  // --delay-seconds seconds       : how many seconds to wait for threads to start
                pargs->delay_seconds = strtod(optarg, NULL);
                pargs->delay_seconds_valid = 1;
                break;

            case 'Q':  // --mitigate-spectre-v4 : using this flag can reduce performance
                pargs->ssbs = 0;    // ssbs = 1 means go fast
                break;

            case 'q':  // --hwclock_freq frequency_hz   : to override TSC or CNTVCT frequency
                pargs->hwclock_freq = strtoul(optarg, NULL, 0);
                break;

            case 'f':  // --cpu-freq-mhz frequency_mhz  : the last -f or -t will set the value of cycle_time_ns
                pargs->mhz = strtod(optarg, NULL);
                pargs->cycle_time_ns = 1e3/pargs->mhz;
                break;

            case estimate_hwclock_freq_val:
                pargs->estimate_hwclock_freq_cpu = strtol(optarg, NULL, 0);
                break;

            case 't':  // --cpu-cycle-time-ns nanoseconds   : the last -f or -t will set the value of cycle_time_ns
                pargs->cycle_time_ns = strtod(optarg, NULL);
                pargs->mhz = 1e3/pargs->cycle_time_ns;
                break;

            case show_per_thread_concurrency_val:
                pargs->show_per_thread_concurrency = 1;
                break;

         // ---- lower case flags are for latency threads ---------------------------------------------------
            case 'l':  // --lat-cpu cpu          : CPU on which to run a latency thread.  Repeat for each CPU.
                cpu = strtol(optarg, NULL, 0);
                if (CPU_ISSET(cpu, &pargs->lat_cpuset)) {
                    printf("Warning: CPU%ld was already specified to run a latency thread, so extra -l is redundant.\n", cpu);
                }
                if (CPU_ISSET(cpu, &pargs->bw_cpuset)) {
                    printf("Warning: CPU%ld was already specified to run a bandwidth thread, so this will double-up a latency thread on the same CPU.\n", cpu);
                }
                CPU_SET(cpu, &pargs->lat_cpuset);
                break;

            case 'n':  // --lat-cacheline_count count : number of cachelines for the span of memory to use for latency measurement
                pargs->lat_cacheline_count = strtoul(optarg, NULL, 0);
                break;

            case 'e':  // --lat-secondary-delay ticks  : how many additional ticks for secondary latency threads to start
                pargs->lat_secondary_delay = strtoul(optarg, NULL, 0);
                break;

            case 'i':  // --lat-iterations iters      : number of iterations for latency measurement between interim reports
                pargs->lat_iterations = strtoul(optarg, NULL, 0);
                break;

            case 'z':  // --lat-cacheline-bytes bytes : latency cacheline bytes (the node size for each record to pointer chase)
                pargs->lat_cacheline_bytes = strtoul(optarg, NULL, 0);
                break;

            case 'j':  // --lat-cacheline-stride count : number of cachelines to skip between loads for latency measurement
                pargs->lat_cacheline_stride = strtoul(optarg, NULL, 0);
                break;

            case 'o':  // --lat-offset  lat_offset
                pargs->lat_offset = strtoul(optarg, NULL, 0);
                pargs->has_lat_offset = 1;
                break;

            case 'c':  // --lat-clear-cache     : clear cache for latency
                pargs->lat_clear_cache = 1;
                break;

            case 'r':  // --lat-randomize
                pargs->lat_randomize = 1;
                break;

            case 'h':  // --lat-use-hugepages hugepage_size_string
                pargs->lat_use_hugepages = parse_hugepage_parameter('h', optarg);
                break;

            case 'w':  // --lat-warmup-cpu cpu_num
                cpu = strtol(optarg, NULL, 0);
                if (CPU_ISSET(cpu, &pargs->lat_warmup_cpuset)) {
                    printf("Warning: CPU%ld was already specified to run a warmup latency thread, so extra -w is redundant.\n", cpu);
                }
                if (CPU_ISSET(cpu, &pargs->bw_cpuset)) {
                    printf("Warning: CPU%ld was already specified to run a bandwidth thread, so this will double-up a latency thread on the same CPU.\n", cpu);
                }
                CPU_SET(cpu, &pargs->lat_warmup_cpuset);
                break;

            case 's':  // --lat-shared-memory
                pargs->lat_shared_memory = 1;
                break;

            case 'u':  // --lat-shared-memory-init-cpu cpu_num
                pargs->lat_shared_memory_init_cpu = strtol(optarg, NULL, 0);
                break;

         // ---- upper case flags are for bandwidth ------------------------------------------------------------
            case 'B':  // --bw-cpu cpu_num              : CPU on which to run a bandwidth thread.  Repeat for each CPU.
                cpu = strtol(optarg, NULL, 0);
                if (CPU_ISSET(cpu, &pargs->bw_cpuset)) {
                    printf("Warning: -B %ld was previously already specified\n", cpu);
                }
                if (CPU_ISSET(cpu, &pargs->lat_cpuset)) {
                    printf("Warning: CPU%ld was already specified to run a latency thread, so this will double-up a bandwidth thread on the same CPU.\n", cpu);
                }
                CPU_SET(cpu, &pargs->bw_cpuset);
                break;

            case 'I':  // --bw-iterations iters         : bandwidth
                pargs->bw_iterations = strtoul(optarg, NULL, 0);
                break;

            case 'L':  // --bw-buflen bytes             :  buflen for bandwidth
                pargs->bw_buflen = strtoul(optarg, NULL, 0);
                break;

            case 'F':  // --bw-fine-delay count         : bandwidth fine delay (inner loop nops)
                pargs->bw_inner_nops = strtoul(optarg, NULL, 0);
                break;


            case 'C':  // --bw-coarse-delay count       : bandwidth coarse delay (outer loop nops)
                pargs->bw_outer_nops = strtoul(optarg, NULL, 0);
                break;

            case 'H':  // --bw-use-hugepages hugepage_size_string
                pargs->bw_use_hugepages = parse_hugepage_parameter('H', optarg);
                break;

            case 'Z':  // --bw-cacheline-bytes bytes  : bandwidth cacheline bytes (for bandwidth region size)
                pargs->bw_cacheline_bytes = strtoul(optarg, NULL, 0);
                break;

            case 'W':  // --bw-write                    : use writes for memory bandwidth traffic
                pargs->bw_write = 1;
                break;

        }
    }
}
