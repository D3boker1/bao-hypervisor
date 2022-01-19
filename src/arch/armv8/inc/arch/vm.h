/** 
 * Bao, a Lightweight Static Partitioning Hypervisor 
 *
 * Copyright (c) Bao Project (www.bao-project.org), 2019-
 *
 * Authors:
 *      Jose Martins <jose.martins@bao-project.org>
 *
 * Bao is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2 as published by the Free
 * Software Foundation, with a special exception exempting guest code from such
 * license. See the COPYING file in the top-level directory for details. 
 *
 */

#ifndef __ARCH_VM_H__
#define __ARCH_VM_H__

#include <bao.h>
#include <arch/vgic.h>
#include <arch/psci.h>

struct arch_vm_platform {
    struct vgic_dscrp {
        paddr_t gicd_addr;
        paddr_t gicc_addr;
        paddr_t gicr_addr;
        size_t interrupt_num;
    } gic;

    struct {
        streamid_t global_mask;
        size_t group_num;
        struct smmu_group {
            streamid_t mask;
            streamid_t id;
        } *groups;
    } smmu;
};

struct vm_arch {
    struct vgicd vgicd;
    vaddr_t vgicr_addr;
    struct emul_mem vgicd_emul;
    struct emul_mem vgicr_emul;
    struct emul_reg icc_sgir_emul;
    struct emul_reg icc_sre_emul;
};

struct vcpu_arch {
    unsigned long vmpidr;
    struct vgic_priv vgic_priv;
    struct psci_ctx psci_ctx;
};

struct arch_regs {
    uint64_t x[31];
    uint64_t elr_el2;
    uint64_t spsr_el2;
} __attribute__((aligned(16)));  // makes size always aligned to 16 to respect
                                 // stack alignment

struct vcpu* vm_get_vcpu_by_mpidr(struct vm* vm, unsigned long mpidr);
void vcpu_arch_entry();

typedef struct vcpu vcpu_t;

static inline void vcpu_arch_inject_hw_irq(vcpu_t* vcpu, uint64_t id)
{
    vgic_inject_hw(vcpu, id);
}

static inline void vcpu_arch_inject_irq(vcpu_t* vcpu, uint64_t id)
{
    vgic_inject(vcpu, id, 0);
}

#endif /* __ARCH_VM_H__ */
