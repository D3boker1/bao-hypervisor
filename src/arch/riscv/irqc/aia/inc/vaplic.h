/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef VAPLIC_H
#define VAPLIC_H

#include <bao.h>
#include <aplic.h>
#include <arch/spinlock.h>
#include <bitmap.h>
#include <emul.h>

/**
 * @brief   Data structure to the virtual interrupt controller when a APLIC 
 *          is the platform interrupt controller.
 * 
 */
struct vaplic {
    spinlock_t lock;
    size_t idc_num;
    BITMAP_ALLOC(hw, APLIC_MAX_INTERRUPTS);
    uint32_t domaincfg;
    uint32_t srccfg[APLIC_MAX_INTERRUPTS-1];
    uint32_t ip[APLIC_MAX_INTERRUPTS/32];
    uint32_t in_clrip[APLIC_MAX_INTERRUPTS/32];
    uint32_t ie[APLIC_MAX_INTERRUPTS/32];
    uint32_t target[APLIC_MAX_INTERRUPTS-1];
    BITMAP_ALLOC(idelivery, APLIC_DOMAIN_NUM_HARTS);
    BITMAP_ALLOC(iforce, APLIC_DOMAIN_NUM_HARTS);
    uint32_t ithreshold[APLIC_DOMAIN_NUM_HARTS];
    uint32_t topi_claimi[APLIC_DOMAIN_NUM_HARTS];
    struct emul_mem aplic_domain_emul;
    struct emul_mem aplic_idc_emul;
};

struct vm;
struct vcpu;

/**
 * @brief Initialize the virtual APLIC for a given virtual machine.
 * 
 * @param vm Virtual machine associated to the virtual APLIC
 * @param vaplic_base address base of the physical APLIC
 * 
 */
void virqc_init(struct vm *vm, struct arch_platform *arch_platform);

/**
 * @brief Inject an interrupt into a vm.
 * 
 * @param vcpu Virtual CPU that will inject the interrupt
 * @param id interrupt identification
 * 
 * The virtual CPU passed by arg. may not be the CPU to which the interrupt 
 * is associated. In that case the vcpu will send a msg to the target cpu. 
 */
void vaplic_inject(struct vcpu *vcpu, irqid_t id);

/**
 * @brief For a given virtual machine and a interrupt, associate this interrupt
 *        with the physical one. Thus, interrupt id is mapped to the physical
 *        id source.
 * 
 * @param vm Virtual machine to associate the 1-1 virt/phys interrupt
 * @param id interrupt identification to associate.
 */
void vaplic_set_hw(struct vm *vm, irqid_t id);

typedef struct vcpu vcpu_t;
static inline void virqc_inject(vcpu_t *vcpu, uint64_t id)
{
    vaplic_inject(vcpu, id);
}

#endif /* __VAPLIC_H__ */