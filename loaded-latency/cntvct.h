
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CNTVCT_H
#define CNTVCT_H

#include "args.h"

#ifdef __aarch64__

#define HWCOUNTER "CNTVCT"

static inline unsigned long read_hwcounter(void) {
    unsigned long tick;

    asm volatile("isb" : : : "memory");
    asm volatile("mrs %0, cntvct_el0" : "=r" (tick) );

    return tick;
}

static inline unsigned long read_cntfreq(void) {
    extern args_t args;
    return args.hwclock_freq;
}

static inline unsigned long get_default_cntfreq(void) {
    unsigned long tick;

    asm volatile("isb" : : : "memory");
    asm volatile("mrs %0, cntfrq_el0" : "=r" (tick) );

    return tick;
}

#endif
#endif
