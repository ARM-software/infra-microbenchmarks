
/*
 * SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ALLOC_H
#define ALLOC_H

enum {
    HUGEPAGES_NONE,
    HUGEPAGES_DEFAULT,
    HUGEPAGES_64K,
    HUGEPAGES_2M,
    HUGEPAGES_32M,
    HUGEPAGES_512M,
    HUGEPAGES_1G,
    HUGEPAGES_16G,
    HUGEPAGES_MAX_ENUM
};


void * do_alloc(size_t length, int use_hugepages, size_t nonhuge_alignment);

#endif
