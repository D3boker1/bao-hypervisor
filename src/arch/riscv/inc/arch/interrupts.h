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
#define SOFT_INT_ID (IRQC_SOFT_INT_ID)
#define TIMR_INT_ID (IRQC_TIMR_INT_ID)
#define MAX_INTERRUPTS (IRQC_MAX_INTERRUPTS)

#define IPI_CPU_MSG SOFT_INT_ID

#endif /* __ARCH_INTERRUPTS_H__ */
