/**
 * SPDX-License-Identifier: Apache-2.0 
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <platform.h>

#ifndef IRQC
#error "IRQC not defined for this platform "
#endif

struct platform platform = {

    .cpu_num = 4,

    .region_num = 1,
    .regions =  (struct mem_region[]) {
        {
            .base = 0x80200000,
            .size = 0x100000000 - 0x200000
        }
    },

    .arch = {
        #if (IRQC == PLIC)
        .plic_base = 0xc000000,        
        #elif (IRQC == APLIC)
        .plic_base = 0xd000000,
        #elif (IRQC == AIA)
        .plic_base = 0xd000000,
        #else 
        #error "unknown IRQC type " IRQC
        #endif
    }

};
