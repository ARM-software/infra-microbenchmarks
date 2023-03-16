
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <string.h>

#include "alloc.h"

void * do_alloc(size_t length, int use_hugepages, size_t nonhuge_alignment) {

    if (use_hugepages != HUGEPAGES_NONE) {
        int hugepage_size_flag = HUGEPAGES_DEFAULT;
        switch (use_hugepages) {
            case HUGEPAGES_64K:
                hugepage_size_flag = MAP_HUGE_64KB;
                break;
            case HUGEPAGES_2M:
                hugepage_size_flag = MAP_HUGE_2MB;
                break;
            case HUGEPAGES_32M:
                hugepage_size_flag = MAP_HUGE_32MB;
                break;
            case HUGEPAGES_512M:
                hugepage_size_flag = MAP_HUGE_512MB;
                break;
            case HUGEPAGES_1G:
                hugepage_size_flag = MAP_HUGE_1GB;
                break;
            case HUGEPAGES_16G:
                hugepage_size_flag = MAP_HUGE_16GB;
                break;
        }
        void * mmap_ret = mmap(NULL, length,
                       PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|MAP_POPULATE|hugepage_size_flag,
                       -1, 0);

        if (mmap_ret == MAP_FAILED) {
            printf("mmap returned %p (MAP_FAILED) for latency thread setup. Exiting!\n"
                   "You probably need to allocate hugepages. Try:\n"
                   " sudo apt-get install libhugetlbfs-bin\n"
                   " sudo hugeadm --create-global-mounts\n"
                   " sudo hugeadm --pool-pages-max DEFAULT:+1000\n"
                   "(Only the last line is needed after a reboot.)\n"
                   "Or, no pages of the requested hugepage size are available.\n",
                   mmap_ret);
            exit(-1);
        }

        return mmap_ret;
    }

    void * p;

    int ret = posix_memalign((void **) &p, nonhuge_alignment, length);

    if (ret) {
        printf("posix_memalign returned %d, exiting\n", ret);
        exit(-1);
    }

    // prefault
    memset(p, 1, length);

    return p;
}
