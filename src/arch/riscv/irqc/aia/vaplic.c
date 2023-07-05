/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <vaplic.h>
#include <vm.h>
#include <cpu.h>
#include <emul.h>
#include <mem.h>
#include <interrupts.h>
#include <arch/csrs.h>
#include <config.h>

#define APLIC_MIN_PRIO 0xFF
#define MASK_INTP_ZERO (0xFFFFFFFE)
#define UPDATE_ALL_HARTS (-1)

#define GET_HART_INDEX(vcpu, intp_id) ((vaplic_get_target(vcpu, intp_id) >> \
                                        APLIC_TARGET_HART_IDX_SHIFT) & \
                                        APLIC_TARGET_HART_IDX_MASK)

#define BIT32_SET_INTP(reg, intp_id) (reg[intp_id/32] =\
                                      bit32_set(reg[intp_id/32], intp_id%32))
#define BIT32_GET_INTP(reg, intp_id) ((bit32_get(reg[intp_id/32], intp_id%32)\
                                      != 0) ? 1U : 0U)
#define BIT32_CLR_INTP(reg, intp_id) (reg[intp_id/32] =\
                                      bit32_clear(reg[intp_id/32], intp_id%32))

/**
 * @brief Converts a virtual cpu id into the physical one
 * 
 * @param vcpu Virtual cpu to convert
 * @return cpuid_t The physical cpu id; or INVALID_CPUID in case of error.
 */
static inline cpuid_t vaplic_vcpuid_to_pcpuid(struct vcpu *vcpu, vcpuid_t vhart){
    return vm_translate_to_pcpuid(vcpu->vm, vhart);
}

static uint32_t vaplic_get_domaincfg(struct vcpu *vcpu);
static uint32_t vaplic_get_target(struct vcpu *vcpu, irqid_t intp_id); 
static uint32_t vaplic_get_idelivery(struct vcpu *vcpu, uint16_t idc_id);
static uint32_t vaplic_get_iforce(struct vcpu *vcpu, uint16_t idc_id);
static uint32_t vaplic_get_ithreshold(struct vcpu *vcpu, uint16_t idc_id);

void vaplic_set_hw(struct vm *vm, irqid_t intp_id){
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS) {
        bitmap_set(vm->arch.vaplic.hw, intp_id);
    }
}

/**
 * @brief Returns if a given interrupt is associated to the physical source
 * 
 * @param vcpu virtual cpu running
 * @param intp_id interrupt to evaluate
 * @return true if is a physical intp
 * @return false if is NOT a physical intp
 */
static bool vaplic_get_hw(struct vcpu* vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    bool ret = false;
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!bitmap_get(vaplic->hw, intp_id);
    } 
    return ret;
}

static bool vaplic_get_pend(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    bool ret = false;
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!BIT32_GET_INTP(vaplic->ip, intp_id);
    }
    return ret;
}

static bool vaplic_get_active(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    bool ret = false;
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!BIT32_GET_INTP(vaplic->active, intp_id);
    }
    return ret;
}

static bool vaplic_set_pend(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    bool ret = false;

    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS && 
        !vaplic_get_pend(vcpu, intp_id) &&
        vaplic_get_active(vcpu, intp_id)){
        BIT32_SET_INTP(vaplic->ip, intp_id);
        ret = true;
    }
    return ret;
}

static bool vaplic_get_enbl(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    bool ret = false;
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!BIT32_GET_INTP(vaplic->ie, intp_id);
    }
    return ret;
}

/**
 * @brief Updates the topi register based with the 
 *        highest pend & en interrupt id
 * 
 * @param vcpu 
 * @return irqid_t 
 */
static bool vaplic_update_topi(struct vcpu* vcpu){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    uint32_t intp_prio = APLIC_MIN_PRIO;
    irqid_t intp_id = APLIC_MAX_INTERRUPTS;
    uint32_t intp_hart_index = 0;
    uint32_t prio = 0;
    uint32_t idc_threshold = 0;
    bool domain_enbl = false;
    bool idc_enbl = false;
    bool idc_force =  false;
    bool force_intp = false;

    /** Find highest pending and enabled interrupt */
    for (size_t i = 1; i < APLIC_MAX_INTERRUPTS; i++) {
        if (GET_HART_INDEX(vcpu, i) == vcpu->id) {
            if (vaplic_get_pend(vcpu, i) && vaplic_get_enbl(vcpu, i)) {
                prio = vaplic_get_target(vcpu, i) & APLIC_TARGET_IPRIO_MASK; 
                if (prio < intp_prio) {
                    intp_prio = prio;
                    intp_id = i;
                    intp_hart_index = vcpu->id;
                }
            }   
        }
    }

    /** Can interrupt be delivered? */
    idc_threshold = vaplic_get_ithreshold(vcpu, intp_hart_index);
    domain_enbl = !!(vaplic_get_domaincfg(vcpu) & APLIC_DOMAINCFG_IE);
    idc_enbl = !!(vaplic_get_idelivery(vcpu, intp_hart_index));
    idc_force = !!(vaplic_get_iforce(vcpu, intp_hart_index));
    
    if(idc_force && (intp_id == APLIC_MAX_INTERRUPTS)){
        intp_id = 0;
        intp_prio = 0;
        force_intp = true;
    }
    
    if (intp_id != APLIC_MAX_INTERRUPTS) {
        if ((intp_prio < idc_threshold || idc_threshold == 0 || force_intp) && 
            idc_enbl && domain_enbl){
            force_intp = false;
            vaplic->topi_claimi[intp_hart_index] = (intp_id << 16) | intp_prio;
            return true;
        }
    }
    vaplic->topi_claimi[intp_hart_index] = 0;
    return false;
}

enum {UPDATE_HART_LINE};
static void vaplic_ipi_handler(uint32_t event, uint64_t data);
CPU_MSG_HANDLER(vaplic_ipi_handler, VPLIC_IPI_ID);

static void vaplic_update_single_hart(struct vcpu* vcpu, vcpuid_t vhart_index){
    vcpuid_t pcpu_id = vaplic_vcpuid_to_pcpuid(vcpu, vhart_index);

    vhart_index &= APLIC_MAX_NUM_HARTS_MAKS;

    /** 
     *  If the current cpu is the targeting cpu, signal the intp 
     *  to the hart
     *  Else, send a mensage to the targeting cpu 
     */
    if(pcpu_id == cpu()->id) {
        if(vaplic_update_topi(vcpu)){
            CSRS(CSR_HVIP, HIP_VSEIP);
        } else  {
            CSRC(CSR_HVIP, HIP_VSEIP);
        }
    } else {
        struct cpu_msg msg = {VPLIC_IPI_ID, UPDATE_HART_LINE, vhart_index};
        cpu_send_msg(pcpu_id, &msg);       
    }
}

/**
 * @brief Update the CPU interrupt line.
 * 
 * @param vcpu 
 */
static void vaplic_update_hart_line(struct vcpu* vcpu, int16_t vhart_index) 
{
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    if (aplic_msi_mode())
        return;

    if (vhart_index == UPDATE_ALL_HARTS){
        for(size_t i = 0; i < APLIC_DOMAIN_NUM_HARTS; i++){
            vaplic_update_single_hart(vcpu, (vcpuid_t)i);
        }
    } else if (vhart_index < vaplic->idc_num){
        vaplic_update_single_hart(vcpu, (vcpuid_t)vhart_index);
    }
}

/**
 * @brief Processes an incoming event.
 * 
 * @param event the event id
 * @param data
 */
static void vaplic_ipi_handler(uint32_t event, uint64_t data) 
{
    switch(event) {
        case UPDATE_HART_LINE:
            vaplic_update_hart_line(cpu()->vcpu, (int16_t)data);
            break;
    }
}

/** APLIC Functions emulation */

/**
 * @brief Write to domaincfg register a new value.
 * 
 * @param vcpu 
 * @param new_val The new value to write to domaincfg
 */
static void vaplic_set_domaincfg(struct vcpu *vcpu, uint32_t new_val){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    /** Update only the virtual domaincfg */
    /** Only Interrupt Enable and Delivery mode are configurable */
    new_val &= (APLIC_DOMAINCFG_IE | APLIC_DOMAINCFG_DM);
    vaplic->domaincfg = new_val | APLIC_DOMAINCFG_RO80;
    vaplic_update_hart_line(vcpu, UPDATE_ALL_HARTS);
    spin_unlock(&vaplic->lock);

}

/**
 * @brief Read from domaincfg
 * 
 * @param vcpu 
 * @return uint32_t domaincfg value 
 */
static uint32_t vaplic_get_domaincfg(struct vcpu *vcpu){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    ret = vaplic->domaincfg;
    return ret;
}

/**
 * @brief Read from a given interrupt sourcecfg register
 * 
 * @param vcpu 
 * @param intp_id Interrupt id to read
 * @return uint32_t 
 */
static uint32_t vaplic_get_sourcecfg(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t real_int_id = intp_id - 1;
    uint32_t ret = 0;

    if(intp_id != 0){
        struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
        if (real_int_id < APLIC_MAX_INTERRUPTS){
            ret = vaplic->srccfg[real_int_id];
        } 
    }
    return ret;
}

/**
 * @brief Write to sourcecfg register of a given interrupt
 * 
 * @param vcpu 
 * @param intp_id interrupt id to write to
 * @param new_val value to write to sourcecfg
 */
static void vaplic_set_sourcecfg(struct vcpu *vcpu, irqid_t intp_id, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    if (intp_id > 0 && intp_id < APLIC_MAX_INTERRUPTS && 
        vaplic_get_sourcecfg(vcpu, intp_id) != new_val) {
        /** If intp is being delegated make whole reg 0.
         *  This happens because this domain is always a leaf. */        
        new_val &= (new_val & APLIC_SRCCFG_D) ? 0 : APLIC_SRCCFG_SM;

        /** If SM is reserved make intp inactive */
        if(new_val == 2 || new_val == 3)
            new_val = APLIC_SOURCECFG_SM_INACTIVE;
        
        /** Only edge sense can be virtualized for know */
        if(new_val  == APLIC_SOURCECFG_SM_LEVEL_HIGH){
            new_val = APLIC_SOURCECFG_SM_EDGE_RISE;
        } else if (new_val  == APLIC_SOURCECFG_SM_LEVEL_LOW){
            new_val = APLIC_SOURCECFG_SM_EDGE_FALL;
        }

        if(vaplic_get_hw(vcpu, intp_id)){
            aplic_set_sourcecfg(intp_id, new_val);
            new_val = aplic_get_sourcecfg(intp_id); 
        }
        vaplic->srccfg[intp_id-1] = new_val;

        if (new_val == APLIC_SOURCECFG_SM_INACTIVE){
            BIT32_CLR_INTP(vaplic->active, intp_id);
            /** Zero pend, en and target registers if intp is now inactive */
            BIT32_CLR_INTP(vaplic->ip, intp_id);
            BIT32_CLR_INTP(vaplic->ie, intp_id);
            vaplic->target[intp_id-1] = 0;
        } else {
            BIT32_SET_INTP(vaplic->active, intp_id);
        }
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, intp_id));
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Get the pending bits for interrupts [32*reg:(32*reg)+31]
 * 
 * @param vcpu virtual cpu
 * @param reg regiter index
 * @return uint32_t value with pending bit mapped per bit
 */
static uint32_t vaplic_get_setip(struct vcpu *vcpu, uint8_t reg){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    uint32_t ret = 0;

    if (reg < APLIC_NUM_SETIx_REGS){
        ret = vaplic->ip[reg];
        ret |= aplic_get32_pend(reg); 
    }
    return ret;
}

/**
 * @brief Set the pending bits for interrupts [32*reg:(32*reg)+31]
 * 
 * @param vcpu virtual cpu
 * @param reg regiter index
 * @param new_val value with pending bit mapped per bit
 */
static void vaplic_set_setip(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if (reg == 0) new_val &= MASK_INTP_ZERO;
    if (reg < APLIC_NUM_SETIx_REGS) {
        vaplic->ip[reg] = new_val & vaplic->active[reg];
        vaplic_update_hart_line(vcpu, UPDATE_ALL_HARTS);
        // Alternative code. Waiting review.
        // for(size_t i = (reg*APLIC_NUM_INTP_PER_REG); 
        //     i < (reg*APLIC_NUM_INTP_PER_REG) + APLIC_NUM_INTP_PER_REG; i++){
        //     if (!!bit32_get(new_val, i%32)){
        //         if(vaplic_set_pend(vcpu, i)){
        //             vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
        //         }
        //     }
        // }
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Set the pending bits for a given interrupt
 * 
 * @param vcpu virtual cpu
 * @param new_val value w/ the interrupt source number
 */
static void vaplic_set_setipnum(struct vcpu *vcpu, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if(vaplic_set_pend(vcpu, new_val)){
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Clear the pending bits for interrupts [32*reg:(32*reg)+31]
 * 
 * @param vcpu virtual cpu 
 * @param reg  regiter index
 * @param new_val value with intp to be cleared per bit
 */
static void vaplic_set_in_clrip(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if (reg == 0) new_val &= MASK_INTP_ZERO;
    if (reg < APLIC_NUM_CLRIx_REGS) {
        aplic_set32_pend(reg, new_val);
        vaplic->ip[reg] &= ~(new_val);
        vaplic->ip[reg] |= aplic_get32_pend(reg);
        vaplic_update_hart_line(vcpu, UPDATE_ALL_HARTS);
        // Alternative code. Waiting review.
        // for(size_t i = (reg*APLIC_NUM_INTP_PER_REG); 
        //     i < (reg*APLIC_NUM_INTP_PER_REG) + APLIC_NUM_INTP_PER_REG; i++){
        //     if (vaplic_get_active(vcpu, i)){
        //         if(vaplic_get_hw(vcpu,i)){
        //             aplic_clr_pend(i);
        //             if(!aplic_get_pend(i)){
        //                 BIT32_CLR_INTP(vaplic->ip, i);
        //             }
        //         } else {
        //             BIT32_CLR_INTP(vaplic->ip, i);
        //         }
        //         vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
        //     }
        // }
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Get the rectified input values per source
 *        NOTE: Not implemented as stated! 
 *        TODO: return the pending bits?
 * 
 * @param vcpu virtual cpu 
 * @param reg regiter index
 * @return uint32_t value with rectified intp per bit
 */
static uint32_t vaplic_get_in_clrip(struct vcpu *vcpu, uint8_t reg){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (reg < APLIC_NUM_CLRIx_REGS) ret = aplic_get_inclrip(reg);
    return ret;
}

/**
 * @brief Clear the pending bits for a given interrupt
 * 
 * @param vcpu virtual cpu
 * @param new_val value w/ the interrupt source number
 */
static void vaplic_set_clripnum(struct vcpu *vcpu, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    if (vaplic_get_active(vcpu, new_val) &&
        vaplic_get_pend(vcpu, new_val)){
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_clr_pend(new_val);
            if (!aplic_get_pend(new_val)){
                BIT32_CLR_INTP(vaplic->ip, new_val);
            }
        } else {
            BIT32_CLR_INTP(vaplic->ip, new_val);
        }
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Get the enabled bits for interrupts [32*reg:(32*reg)+31]
 * 
 * @param vcpu virtual cpu
 * @param reg regiter index
 * @return uint32_t value with enabled bit mapped per bit
 */
static uint32_t vaplic_get_setie(struct vcpu *vcpu, uint32_t reg){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;

    if (reg < APLIC_NUM_SETIx_REGS){
        for(size_t i = (reg*APLIC_NUM_INTP_PER_REG); 
            i < (reg*APLIC_NUM_INTP_PER_REG) + APLIC_NUM_INTP_PER_REG; i++){
                ret |= (BIT32_GET_INTP(vaplic->ie, i) << i%32);
        }
    }
    return ret;
}

/**
 * @brief Set the enabled bits for interrupts [32*reg:(32*reg)+31]
 * 
 * @param vcpu virtual cpu
 * @param reg regiter index
 * @param new_val value with enbaled bit mapped per bit
 */
static void vaplic_set_setie(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if (reg == 0) new_val &= MASK_INTP_ZERO;
    if (reg < APLIC_NUM_SETIx_REGS && 
        vaplic_get_setie(vcpu, reg) != new_val) {
        for(size_t i = (reg*APLIC_NUM_INTP_PER_REG); 
            i < (reg*APLIC_NUM_INTP_PER_REG) + APLIC_NUM_INTP_PER_REG; i++){
            if (vaplic_get_active(vcpu, i)){
                if(vaplic_get_hw(vcpu,i)){
                    aplic_set_enbl(i);
                    if(aplic_get_enbl(i)){
                        BIT32_SET_INTP(vaplic->ie, i);
                    }
                } else {
                    BIT32_SET_INTP(vaplic->ie, i);
                }
            }
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
        }
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Set the enabled bits for a given interrupt
 * 
 * @param vcpu virtual cpu
 * @param new_val value w/ the interrupt source number
 */
static void vaplic_set_setienum(struct vcpu *vcpu, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;
 
    spin_lock(&vaplic->lock);
    if (vaplic_get_active(vcpu, new_val) &&
        !vaplic_get_enbl(vcpu, new_val)) {
        if(vaplic_get_hw(vcpu, new_val)){
            aplic_set_enbl(new_val);
            if (aplic_get_enbl(new_val)){
                BIT32_SET_INTP(vaplic->ie, new_val);
            }
        } else {
            BIT32_SET_INTP(vaplic->ie, new_val);
        }
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Clear the enabled bits for interrupts [32*reg:(32*reg)+31]
 * 
 * @param vcpu virtual cpu 
 * @param reg  regiter index
 * @param new_val value with intp to be cleared per bit
 */
static void vaplic_set_clrie(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if (reg == 0) new_val &= MASK_INTP_ZERO;
    if (reg < APLIC_NUM_SETIx_REGS){
        for(size_t i = (reg*APLIC_NUM_INTP_PER_REG); 
            i < (reg*APLIC_NUM_INTP_PER_REG) + APLIC_NUM_INTP_PER_REG; i++){
            if (vaplic_get_active(vcpu, i)){
                if(vaplic_get_hw(vcpu,i)){
                    aplic_clr_enbl(i);
                    if(!aplic_get_enbl(i)){
                        BIT32_CLR_INTP(vaplic->ie, i);
                    }
                } else {
                    BIT32_CLR_INTP(vaplic->ie, i);
                }
            }
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
        }
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Clear the enabled bits for a given interrupt
 * 
 * @param vcpu virtual cpu
 * @param new_val value w/ the interrupt source number
 */
static void vaplic_clr_enbl(struct vcpu *vcpu, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if (vaplic_get_active(vcpu, new_val) &&
        vaplic_get_enbl(vcpu, new_val)) {
        if(vaplic_get_hw(vcpu, new_val)){
            aplic_clr_enbl(new_val);
            if (!aplic_get_enbl(new_val)){
                BIT32_CLR_INTP(vaplic->ie, new_val);
            }
        } else {
            BIT32_CLR_INTP(vaplic->ie, new_val);
        }
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Write to target register of a given interrupt
 * 
 * @param vcpu 
 * @param intp_id interrupt id to write to
 * @param new_val value to write to target
 */
static void vaplic_set_target(struct vcpu *vcpu, irqid_t intp_id, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;
    uint16_t hart_index = (new_val >> APLIC_TARGET_HART_IDX_SHIFT) & APLIC_TARGET_HART_IDX_MASK;
    cpuid_t pcpu_id = vm_translate_to_pcpuid(vcpu->vm, hart_index);
    uint32_t new_aplic_target = 0;
    uint32_t new_vaplic_target = 0;

    spin_lock(&vaplic->lock);
    if(pcpu_id == INVALID_CPUID){
        /** If the hart index is invalid, make it vcpu = 0 
         *  and read the new pcpu.
         *  Software should not write anything other than legal 
         *  values to such a field */
        hart_index = 0;
        pcpu_id = vm_translate_to_pcpuid(vcpu->vm, hart_index);
    }
    
    if(!aplic_msi_mode()){
        /** If prio is 0, set to 1 (max)*/
        new_val &= APLIC_TARGET_IPRIO_MASK;
        if (new_val == 0) {
            new_val = APLIC_TARGET_PRIO_DEFAULT;
        }
        /** Write the target CPU in hart index */
        new_aplic_target = new_val|(pcpu_id << APLIC_TARGET_HART_IDX_SHIFT);
        new_vaplic_target = new_val|(hart_index << APLIC_TARGET_HART_IDX_SHIFT);

        if (vaplic_get_active(vcpu, intp_id) && 
            vaplic_get_target(vcpu, intp_id) != new_val) {
            if(vaplic_get_hw(vcpu, intp_id)){
                aplic_set_target(intp_id, new_aplic_target);
                if(aplic_get_target(intp_id) == new_aplic_target){
                    vaplic->target[intp_id-1] = new_vaplic_target;
                }
            } else {
                vaplic->target[intp_id-1] = new_vaplic_target;
            }
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, intp_id));
        }
    } else {
        new_val &= APLIC_TARGET_EEID_MASK;
        new_val |= (1ULL<<APLIC_TARGET_GUEST_IDX_SHIFT);
        new_val |= (pcpu_id << APLIC_TARGET_HART_IDX_SHIFT);

        if(vaplic_get_hw(vcpu, intp_id)){
            aplic_set_target(intp_id, new_val);
            if(aplic_get_target(intp_id) == new_val){
                vaplic->target[intp_id-1] = new_val;
            }
        } else {
            vaplic->target[intp_id-1] = new_val;
        }
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Read from a given interrupt target register
 * 
 * @param vcpu 
 * @param intp_id Interrupt id to read
 * @return uint32_t value with target from intp_id
 */
static uint32_t vaplic_get_target(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    cpuid_t pcpu_id = 0;
    cpuid_t vcpu_id = 0;
    
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        /** Translate the physical cpu into the its virtual pair */
        pcpu_id = vaplic->target[intp_id -1] >> APLIC_TARGET_HART_IDX_SHIFT;
        vcpu_id = vm_translate_to_vcpuid(vcpu->vm, pcpu_id);
        ret = vaplic->target[intp_id -1] & APLIC_TARGET_IPRIO_MASK;
        ret |= (vcpu_id << APLIC_TARGET_HART_IDX_SHIFT);
    }
    return ret;
}

/** IDC Functions emulation */

/**
 * @brief Set idelivery register for a given idc. Only 0 and 1 are allowed.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @param new_val new value to write in iforce
 */
static void vaplic_set_idelivery(struct vcpu *vcpu, uint16_t idc_id, uint32_t new_val){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    new_val = (new_val & 0x1);
    if (idc_id < vaplic->idc_num){
        if (new_val != 0) 
            bitmap_set(vaplic->idelivery, idc_id);
        else
            bitmap_clear(vaplic->idelivery, idc_id);
    }
    vaplic_update_hart_line(vcpu, idc_id);
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Read idelivery register from a given idc.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @return uint32_t value read from idelivery
 */
static uint32_t vaplic_get_idelivery(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (idc_id < vaplic->idc_num) ret = bitmap_get( vaplic->idelivery, idc_id);
    return ret;
}

/**
 * @brief Set iforce register for a given idc. Only 0 and 1 are allowed.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @param new_val new value to write in iforce
 */
static void vaplic_set_iforce(struct vcpu *vcpu, uint16_t idc_id, uint32_t new_val){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    new_val = (new_val & 0x1);
    if (idc_id < vaplic->idc_num){
        if (new_val != 0) 
            bitmap_set(vaplic->iforce, idc_id);
        else
            bitmap_clear(vaplic->iforce, idc_id);
    }
    vaplic_update_hart_line(vcpu, idc_id);
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Read idelivery register from a given idc.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @return uint32_t value read from iforce
 */
static uint32_t vaplic_get_iforce(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (idc_id < vaplic->idc_num) ret = bitmap_get(vaplic->iforce, idc_id);
    return ret;
}

/**
 * @brief Set ithreshold register for a given idc.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @param new_val new value to write in ithreshold
 */
static void vaplic_set_ithreshold(struct vcpu *vcpu, uint16_t idc_id, uint32_t new_val){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    if (idc_id < vaplic->idc_num){
        vaplic->ithreshold[idc_id] = new_val;
    }
    vaplic_update_hart_line(vcpu, idc_id);
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Read ithreshold register from a given idc.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @return uint32_t value read from ithreshold
 */
static uint32_t vaplic_get_ithreshold(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (idc_id < vaplic->idc_num) ret = vaplic->ithreshold[idc_id];
    return ret;
}

/**
 * @brief Read topi register from a given idc.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @return uint32_t value read from topi
 */
static uint32_t vaplic_get_topi(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (idc_id < vaplic->idc_num) ret = vaplic->topi_claimi[idc_id];
    return ret;
}

/**
 * @brief Read claimi register from a given idc.
 * 
 * @param vcpu virtual CPU
 * @param idc_id idc identifier
 * @return uint32_t value read from claimi
 */
static uint32_t vaplic_get_claimi(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct vaplic* vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    if (idc_id < vaplic->idc_num){
        ret = vaplic->topi_claimi[idc_id];
        // Clear the virt pending bit for te read intp
        BIT32_CLR_INTP(vaplic->ip, (ret >> 16));
        /** Spurious intp*/
        if (ret == 0){
            // Clear the virt iforce bit
            bitmap_clear(vaplic->iforce, idc_id);
        }
        vaplic_update_hart_line(vcpu, idc_id);
    }
    spin_unlock(&vaplic->lock);
    return ret;
}

/**
 * @brief domaincfg register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_domaincfg_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_domaincfg(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_domaincfg(cpu()->vcpu));
    }
}

/**
 * @brief sourcecfg register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_srccfg_access(struct emul_access *acc){
    int intp = (acc->addr & 0xFFF)/4;
    if (acc->write) {
        vaplic_set_sourcecfg(cpu()->vcpu, intp, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_sourcecfg(cpu()->vcpu, intp));
    }
}

/**
 * @brief setip register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_setip_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/4;
    if (acc->write) {
        vaplic_set_setip(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_setip(cpu()->vcpu, reg));
    }
}

/**
 * @brief setipnum register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_setipnum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_setipnum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

/**
 * @brief in_clrip register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_in_clrip_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/4;
    if (acc->write) {
        vaplic_set_in_clrip(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_in_clrip(cpu()->vcpu, reg));
    }
}

/**
 * @brief clripnum register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_clripnum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_clripnum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

/**
 * @brief setie register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_setie_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/4;
    if (acc->write) {
        vaplic_set_setie(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_setie(cpu()->vcpu, reg));
    }
}

/**
 * @brief setienum register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_setienum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_setienum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

/**
 * @brief clrie register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_clrie_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/4;
    if (acc->write) {
        vaplic_set_clrie(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

/**
 * @brief clrienum register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_clrienum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_clr_enbl(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

/**
 * @brief target register access emulation function 
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_target_access(struct emul_access *acc){
    int intp = (acc->addr & 0xFFF)/4;
    if (acc->write) {
        vaplic_set_target(cpu()->vcpu, intp, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_target(cpu()->vcpu, intp));
    }
}

/**
 * @brief idelivery register access emulation function 
 * 
 * @param acc access information
 * @param idc_id idc unique identifier
 *  
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_idelivery_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) {
        vaplic_set_idelivery(cpu()->vcpu, idc_id, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_idelivery(cpu()->vcpu, idc_id));
    }
}

/**
 * @brief iforce register access emulation function 
 * 
 * @param acc access information
 * @param idc_id idc unique identifier
 *  
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_iforce_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) {
        vaplic_set_iforce(cpu()->vcpu, idc_id, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_iforce(cpu()->vcpu, idc_id));
    }
}

/**
 * @brief ithreshold register access emulation function 
 * 
 * @param acc access information
 * @param idc_id idc unique identifier
 *  
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_ithreshold_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) {
        vaplic_set_ithreshold(cpu()->vcpu, idc_id, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_ithreshold(cpu()->vcpu, idc_id));
    }
}

/**
 * @brief topi register access emulation function 
 * 
 * @param acc access information
 * @param idc_id idc unique identifier
 *  
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_topi_access(struct emul_access *acc, idcid_t idc_id){
    if (!acc->write){
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_topi(cpu()->vcpu, idc_id));
    }
}

/**
 * @brief claimi register access emulation function 
 * 
 * @param acc access information
 * @param idc_id idc unique identifier
 *  
 * It determines whether it needs to call the write or read funcion
 * for the choosen register.
 */
static void vaplic_emul_claimi_access(struct emul_access *acc, idcid_t idc_id){
    if (!acc->write){
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_claimi(cpu()->vcpu, idc_id));
    } 
}

/**
 * @brief Injects a given interrupt into a given vcpu 
 * 
 * @param vcpu vcpu to inject the interrupt
 * @param intp_id interrupt unique id
 */
void vaplic_inject(struct vcpu *vcpu, irqid_t intp_id)
{
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    /** If the intp was successfully injected, update the heart line. */
    if (vaplic_set_pend(vcpu, intp_id)){
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, intp_id));
    }
    spin_unlock(&vaplic->lock);
}

static bool vaplic_domain_emul_reserved (uint16_t addr) {
    bool ret = false;
    if ((addr < 0x1C00 && addr > 0x0FFC) ||
        (addr < 0x1CDC && addr > 0x1C7C) || 
        (addr < 0x1D00 && addr > 0x1CDC) ||
        (addr < 0x1DDC && addr > 0x1D7C) ||
        (addr < 0x1E00 && addr > 0x1DDC) ||
        (addr < 0x1EDC && addr > 0x1E7C) ||
        (addr < 0x1F00 && addr > 0x1EDC) ||
        (addr < 0x1FDC && addr > 0x1F7C) ||
        (addr < 0x2000 && addr > 0x1FDC) ||
        (addr < 0x3000 && addr > 0x2004) ) {
            ret = true;
        }
    return ret;
}

/**
 * @brief Function to handle writes (or reads) to (from) domain structure.
 * 
 * @param acc emulated access
 * @return true if conclude without errors.
 * @return false if the access is not aligned.
 */
static bool vaplic_domain_emul_handler(struct emul_access *acc)
{
    uint16_t emul_addr = 0;

    // only allow aligned word accesses
    if (acc->width != 4 || acc->addr & 0x3) return false;

    emul_addr = acc->addr & 0x3fff;

    if (vaplic_domain_emul_reserved(emul_addr)){
        if(!acc->write) {
            vcpu_writereg(cpu()->vcpu, acc->reg, 0);
        }
        return true;
    }

    switch (emul_addr >> 12)
    {
        case 0:
            if (emul_addr == 0x0000) { /** domaincfg */
                vaplic_emul_domaincfg_access(acc);
            } else { /** sourcecfg */
                vaplic_emul_srccfg_access(acc);
            }
            break;
        case 1:
            switch (emul_addr >> 7)
            {
            case 0x38: /** setip */
                vaplic_emul_setip_access(acc);
                break;
            case 0x39: /** setipnum */
                vaplic_emul_setipnum_access(acc);
                break;
            case 0x3A: /** in_clrip */
                vaplic_emul_in_clrip_access(acc);
                break;
            case 0x3B: /** clripnum */
                vaplic_emul_clripnum_access(acc);
                break;
            case 0x3C: /** setie */
                vaplic_emul_setie_access(acc);
                break;
            case 0x3D: /** setienum */
                vaplic_emul_setienum_access(acc);
                break;
            case 0x3E: /** clrie */
                vaplic_emul_clrie_access(acc);
                break;
            case 0x3F: /** clrienum */
                vaplic_emul_clrienum_access(acc);
                break;
            default:
                if(!acc->write) {
                    vcpu_writereg(cpu()->vcpu, acc->reg, 0);
                }
                break;
            }
            break;
        case 3:
            if (emul_addr == 0x3000) { /** genmsi */
                vaplic_emul_domaincfg_access(acc);
            } else { /** target */
                vaplic_emul_target_access(acc);
            }
            break;
        default:
            if(!acc->write) {
                vcpu_writereg(cpu()->vcpu, acc->reg, 0);
            }
            break;
    }

    return true;
}

/**
 * @brief Function to handle writes (or reads) to (from) IDC structure.
 * 
 * @param acc emulated access
 * @return true  if conclude without errors.
 * @return false if the access is not aligned.
 */
static bool vaplic_idc_emul_handler(struct emul_access *acc)
{
    // only allow aligned word accesses
    if (acc->width != 4 || acc->addr & 0x3) return false;

    uint32_t addr = acc->addr;
    idcid_t idc_id = ((acc->addr - APLIC_IDC_OFF - 
            cpu()->vcpu->vm->arch.vaplic.aplic_domain_emul.va_base) >> 5) 
            & APLIC_MAX_NUM_HARTS_MAKS;

    if(!(idc_id < cpu()->vcpu->vm->arch.vaplic.idc_num)){
        if(!acc->write) {
            vcpu_writereg(cpu()->vcpu, acc->reg, 0);
        }
        return true;
    }

    addr = addr - cpu()->vcpu->vm->arch.vaplic.aplic_idc_emul.va_base;
    addr = addr - (sizeof(struct aplic_hart_hw) * idc_id);
    switch (addr & 0x1F) {
        case APLIC_IDC_IDELIVERY_OFF:
            vaplic_emul_idelivery_access(acc, idc_id);
            break;
        case APLIC_IDC_IFORCE_OFF:
            vaplic_emul_iforce_access(acc, idc_id);
            break;
        case APLIC_IDC_ITHRESHOLD_OFF:
            vaplic_emul_ithreshold_access(acc, idc_id);
            break;
        case APLIC_IDC_TOPI_OFF:
            vaplic_emul_topi_access(acc, idc_id);
            break;
        case APLIC_IDC_CLAIMI_OFF:
            vaplic_emul_claimi_access(acc, idc_id);
            break;
        default:
            if(!acc->write) {
                vcpu_writereg(cpu()->vcpu, acc->reg, 0);
            }
            break;
    }

    return true;
}

void virqc_init(struct vm *vm, struct arch_vm_platform arch_vm_platform)
{
    if (cpu()->id == vm->master) {
        vm->arch.vaplic.aplic_domain_emul = (struct emul_mem) {
            .va_base = arch_vm_platform.irqc.aia.aplic.base,
            .size = sizeof(struct aplic_global_hw),
            .handler = vaplic_domain_emul_handler
        };

        vm_emul_add_mem(vm, &vm->arch.vaplic.aplic_domain_emul);

        /** 
         *  Emulate the IDC only if the aplic is in direct mode.
         *  Should we do not compile the IDC functions based on a compiler 
         *  macro (IRQC = AIA)?  
        */
        if (!aplic_msi_mode())
        {
            vm->arch.vaplic.aplic_idc_emul = (struct emul_mem) {
            .va_base = arch_vm_platform.irqc.aia.aplic.base + APLIC_IDC_OFF,
            .size = sizeof(struct aplic_hart_hw)*APLIC_DOMAIN_NUM_HARTS,
            .handler = vaplic_idc_emul_handler
            };

            vm_emul_add_mem(vm, &vm->arch.vaplic.aplic_idc_emul);
            /* 1 IDC per hart */
            vm->arch.vaplic.idc_num = vm->cpu_num;
        }
    }
}