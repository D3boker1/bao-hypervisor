/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef ARCH_INTERRUPTS_H
#define ARCH_INTERRUPTS_H

#include <bao.h>
#include <irqc.h>

/**
 * In riscv, the ipi (software interrupt) and timer interrupts dont actually
 * have an ID as their are treated differently from external interrupts
 * routed by the external interrupt controller, the PLIC.
 * Will define their ids as the ids after the maximum possible in the PLIC.
 */
#if (IRQC == AIA)
#define SOFT_INT_ID 1
#else
#define SOFT_INT_ID (IRQC_MAX_INTERRUPTS + 1)
#endif
#define TIMR_INT_ID (IRQC_MAX_INTERRUPTS + 2)
#define MAX_INTERRUPTS (TIMR_INT_ID + 1)

// #if (IRQC == AIA)
// #define IPI_CPU_MSG 1
// #else
#define IPI_CPU_MSG SOFT_INT_ID
// #endif

#endif /* __ARCH_INTERRUPTS_H__ */
