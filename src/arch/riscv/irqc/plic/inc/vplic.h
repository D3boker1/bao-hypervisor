/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef VPLIC_H
#define VPLIC_H

#include <bao.h>
#include <plic.h>
#include <arch/spinlock.h>
#include <bitmap.h>
#include <emul.h>

struct vplic {
    spinlock_t lock;
    size_t cntxt_num;
    BITMAP_ALLOC(hw, PLIC_MAX_INTERRUPTS);
    BITMAP_ALLOC(pend, PLIC_MAX_INTERRUPTS);
    BITMAP_ALLOC(act, PLIC_MAX_INTERRUPTS);
    uint32_t prio[PLIC_MAX_INTERRUPTS];
    BITMAP_ALLOC_ARRAY(enbl, PLIC_MAX_INTERRUPTS, PLIC_PLAT_CNTXT_NUM);
    uint32_t threshold[PLIC_PLAT_CNTXT_NUM];
    struct emul_mem plic_global_emul;
    struct emul_mem plic_claimcomplte_emul;
};

struct vm;
struct vcpu;
struct arch_vm_platform;
void vplic_init(struct vm *vm, const struct arch_vm_platform *arch_vm_platform);
void vplic_inject(struct vcpu *vcpu, irqid_t id);
void vplic_set_hw(struct vm *vm, irqid_t id);

static inline void virqc_init(struct vm *vm, const struct arch_vm_platform *arch_vm_platform)
{
    vplic_init(vm, arch_vm_platform);
}

typedef struct vcpu vcpu_t;
static inline void virqc_inject(vcpu_t *vcpu, irqid_t id)
{
    vplic_inject(vcpu, id);
}

#endif //VPLIC_H
