
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BANDWIDTH_H
#define BANDWIDTH_H

struct bw_thread_info {
    pthread_t     thread_id;
    unsigned long hwcounter_start;
    unsigned long hwcounter_stop;
    unsigned long actual_hwcounter_start;   // output
    unsigned long actual_hwcounter_stop;    // output
    int           thread_num;
    int           cpu;              // cpu on which this thread is run
    size_t        bw_buflen;        // bytes
    size_t        inner_nops;
    size_t        outer_nops;
    size_t        iterations;
    size_t        bw_cacheline_bytes;
    int           bw_use_hugepages;
    int           bw_write;
    double        avg_bw;                   // output
    char          threadname[32];
};


void bandwidth_thread (struct bw_thread_info * bw_tinfo);

#endif
