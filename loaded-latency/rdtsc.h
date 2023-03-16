
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

static inline unsigned long get_default_cntfreq(void) {

    const unsigned long default_cntfreq = 2900000000;

    printf("Assuming hwcounter frequency = %lu Hz\n", default_cntfreq);
    printf("Use --hwclock-freq to override\n");
    printf("Use --estimate-hwclock-freq to measure an estimate.\n");

    return default_cntfreq;
}


#endif
#endif
