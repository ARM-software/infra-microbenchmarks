
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RDTSC_H
#define RDTSC_H

#include "args.h"

#ifdef __x86_64__
#include <x86intrin.h>

#define HWCOUNTER "TSC"

static inline unsigned long read_hwcounter(void) {
    unsigned long tick;

    tick = __rdtsc();

    return tick;
}


static inline unsigned long read_cntfreq(void) {
    extern args_t args;
    return args.hwclock_freq;
}

unsigned long estimate_hwclock_freq(long cpu_num, size_t n, int verbose, struct timeval target_measurement_duration);

static inline unsigned long get_default_cntfreq(void) {

    const struct timeval target_measurement_duration = { .tv_sec = 0, .tv_usec = 100000 };
    unsigned long hwclock_freq;
    long cpu_num = 0;       // XXX: measure on CPU0, assume it is online
    size_t num_samples = 1;
    int verbose = 0;

    printf("Measuring TSC frequency on CPU0 for %lu.%06lu seconds... ",
            target_measurement_duration.tv_sec, target_measurement_duration.tv_usec);
    fflush(stdout);
    hwclock_freq = estimate_hwclock_freq(cpu_num, num_samples, verbose, target_measurement_duration);
    printf("%lu Hz\n", hwclock_freq);
    printf("Use --hwclock-freq to override this result.\n");
    printf("Use --estimate-hwclock-freq to do longer measurements.\n\n");

    return hwclock_freq;
}


#endif
#endif
