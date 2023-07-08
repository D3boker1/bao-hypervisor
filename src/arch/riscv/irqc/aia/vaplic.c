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

#define APLIC_MIN_PRIO 0xFF
#define UPDATE_ALL_HARTS    (-1)

#define BIT32_SET_INTP(reg, intp_id) (reg[intp_id/32] =\
                                      bit32_set(reg[intp_id/32], intp_id%32))
#define BIT32_GET_INTP(reg, intp_id) ((bit32_get(reg[intp_id/32], intp_id%32)\
                                      != 0) ? 1U : 0U)
#define BIT32_CLR_INTP(reg, intp_id) (reg[intp_id/32] =\
                                      bit32_clear(reg[intp_id/32], intp_id%32))

#define ADDR_INSIDE_RANGE(addr, start, end)\
                         (addr >= offsetof(struct aplic_global_hw, start) &&\
                          addr  < offsetof(struct aplic_global_hw, end))

#define GET_HART_INDEX(vcpu, intp_id) ((vaplic_get_target(vcpu, intp_id) >> \
                                        APLIC_TARGET_HART_IDX_SHIFT) & \
                                        APLIC_TARGET_HART_IDX_MASK)

#define INTP_VALID(intp_id) (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS)

/**
 * @brief Converts a virtual cpu id into the physical one
 * 
 * @param vcpu Virtual cpu to convert
 * @return int The physical cpu id; or INVALID_CPUID in case of error.
 */
static inline int vaplic_vcpuid_to_pcpuid(struct vcpu *vcpu, vcpuid_t vhart){
    return vm_translate_to_pcpuid(vcpu->vm, vhart);
}

static uint32_t vaplic_get_domaincfg(struct vcpu *vcpu);
static uint32_t vaplic_get_target(struct vcpu *vcpu, irqid_t intp_id); 
static uint32_t vaplic_get_idelivery(struct vcpu *vcpu, uint16_t idc_id);
static uint32_t vaplic_get_iforce(struct vcpu *vcpu, uint16_t idc_id);
static uint32_t vaplic_get_ithreshold(struct vcpu *vcpu, uint16_t idc_id);

void vaplic_set_hw(struct vm *vm, irqid_t intp_id)
{
    if (intp_id < APLIC_MAX_INTERRUPTS) {
        bitmap_set(vm->arch.vaplic.hw,intp_id);
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
static bool vaplic_get_hw(struct vcpu* vcpu, irqid_t intp_id)
{
    bool ret = false;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (intp_id <= APLIC_MAX_INTERRUPTS) ret = bitmap_get(vaplic->hw, intp_id);
    return ret;
}

static bool vaplic_get_pend(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!BIT32_GET_INTP(vaplic->ip, intp_id);
    }
    return ret;
}

static bool vaplic_get_enbl(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!BIT32_GET_INTP(vaplic->ie, intp_id);
    }
    return ret;
}

/**
 * @brief Returns if a given interrupt is active for this domain.
 * 
 * @param vcpu virtual cpu
 * @param intp_id interrupt id
 * @return true if the interrupt is active
 * @return false if the interrupt is NOT active
 */
static bool vaplic_get_active(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    bool ret = false;
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = !!BIT32_GET_INTP(vaplic->active, intp_id);
    }
    return ret;
}

/**
 * @brief Emulates the notifier aplic module.
 *        (02/11/2022): computes the next pending bit.
 *        Only direct mode is supported. 
 * 
 * @param vcpu 
 * @return irqid_t 
 */
static irqid_t vaplic_emul_notifier(struct vcpu* vcpu){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;

    /** Find highest pending and enabled interrupt */
    uint32_t max_prio = APLIC_MIN_PRIO;
    irqid_t int_id = 0;
    uint32_t hart_index = 0;
    uint32_t prio = 0;

    for (size_t i = 1; i < APLIC_MAX_INTERRUPTS; i++) {
        if (vaplic_get_pend(vcpu, i) && vaplic_get_enbl(vcpu, i)) {
            uint32_t target = vaplic_get_target(vcpu, i); 
            prio = target & 0xFF; 
            
            if (prio < max_prio) {
                max_prio = prio;
                int_id = i;
                hart_index = (target >> 18) & 0x3FFF;
            }
        }
    }

    /** Can interrupt be delivery? */
    uint32_t domaincgfIE = (vaplic_get_domaincfg(vcpu) >> 8) & 0x1;
    uint32_t threshold = vaplic_get_ithreshold(vcpu, hart_index);
    uint32_t delivery = vaplic_get_idelivery(vcpu, hart_index);
    uint32_t force =  vaplic_get_iforce(vcpu, hart_index);
    if ((max_prio < threshold || threshold == 0 || force == 1) && 
         delivery == 1 && domaincgfIE == 1){
        vaplic->topi_claimi[hart_index] = (int_id << 16) | prio;
        return int_id;
    }
    else{
        return 0;
    }
}

enum {UPDATE_HART_LINE};
static void vaplic_ipi_handler(uint32_t event, uint64_t data);
CPU_MSG_HANDLER(vaplic_ipi_handler, VPLIC_IPI_ID);

/**
 * @brief Updates the interrupt line for a single hart
 * 
 * @param vcpu virtual cpu
 * @param vhart_index hart id to update
 */
static void vaplic_update_single_hart(struct vcpu* vcpu, vcpuid_t vhart_index){
    vcpuid_t pcpu_id = vaplic_vcpuid_to_pcpuid(vcpu, vhart_index);

    vhart_index &= APLIC_MAX_NUM_HARTS_MAKS;

    /** 
     *  If the current cpu is the targeting cpu, signal the intp 
     *  to the hart
     *  Else, send a mensage to the targeting cpu 
     */
    if(pcpu_id == cpu()->id) {
        if(vaplic_emul_notifier(vcpu)){
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
 * @brief Triggers the hart/harts interrupt line update.
 * 
 * @param vcpu virtual cpu
 * @param vhart_index virtual hart to update the interrupt line. 
 *        If UPDATE_ALL_HARTS were passed, thsi function will trigger
 *        the interrupt line update to all virtual harts running in this vm.  
 */
static void vaplic_update_hart_line(struct vcpu* vcpu, int16_t vhart_index) 
{
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

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
 * @param vcpu virtual hart
 * @return uint32_t domaincfg value 
 */
static uint32_t vaplic_get_domaincfg(struct vcpu *vcpu){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    return vaplic->domaincfg;
}

/**
 * @brief Read the sourcecfg register of a given interrupt
 * 
 * @param vcpu virtual hart
 * @param intp_id interrupt ID
 * @return uint32_t value with the interrupt sourcecfg value
 */
static uint32_t vaplic_get_sourcecfg(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    uint32_t ret = 0;

    if(intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = vaplic->srccfg[intp_id];
    }
    return ret;
}

/**
 * @brief Write the sourcecfg register of a given interrupt
 * 
 * @param vcpu virtual hart
 * @param intp_id interrupt ID
 * @param new_val value to write to sourcecfg
 */
static void vaplic_set_sourcecfg(struct vcpu *vcpu, irqid_t intp_id, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;

    spin_lock(&vaplic->lock);
    if (intp_id > 0 && intp_id < APLIC_MAX_INTERRUPTS && 
        vaplic_get_sourcecfg(vcpu, intp_id) != new_val) {
        /** If intp is being delegated make whole reg 0.
         *  This happens because a S domain is always a leaf. */        
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
        vaplic->srccfg[intp_id] = new_val;

        if (new_val == APLIC_SOURCECFG_SM_INACTIVE){
            BIT32_CLR_INTP(vaplic->active, intp_id);
            /** Zero pend, en and target registers if intp is now inactive */
            BIT32_CLR_INTP(vaplic->ip, intp_id);
            BIT32_CLR_INTP(vaplic->ie, intp_id);
            vaplic->target[intp_id] = 0;
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
    uint32_t ret = 0;
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (reg < APLIC_NUM_SETIx_REGS) ret = vaplic->ip[reg];
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
    if (reg == 0) new_val &= 0xFFFFFFFE;
    if (reg < APLIC_NUM_SETIx_REGS && 
        vaplic_get_setip(vcpu, reg) != new_val) {
        vaplic->ip[reg] = 0;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/APLIC_NUM_SETIx_REGS; i++){
            /** Is this intp a phys. intp? */
            if(vaplic_get_hw(vcpu,i)){
                /** Update in phys. aplic */
                if(!!BIT32_GET_INTP(vaplic->ip, i) && ((new_val >> i) & 1)){
                    aplic_set_pend(i);
                    if(aplic_get_pend(i)){
                        vaplic->ip[reg] |= new_val & (1 << i);
                    }
                }
            } else {
                /** If intp is not phys. emul aplic behaviour */
                vaplic->ip[reg] |= new_val & (1 << i);
                vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
            }
        }
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
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        !BIT32_GET_INTP(vaplic->ip, new_val)) {
        BIT32_SET_INTP(vaplic->ip, new_val);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_set_pend(new_val);
        } else {
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
        }
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
    if (reg < APLIC_NUM_CLRIx_REGS && 
        vaplic_get_setip(vcpu, reg) != new_val) {
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vaplic->ip[reg] &= ~new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/APLIC_NUM_CLRIx_REGS; i++){
            if(vaplic_get_hw(vcpu,i)){
                if(!BIT32_GET_INTP(vaplic->ip, i) && ((new_val >> i) & 1)){
                    aplic_clr_pend(i);
                }
            } else {
                vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
            }
        }
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
    if (reg < APLIC_NUM_CLRIx_REGS) ret = vaplic->in_clrip[reg];
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
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        BIT32_GET_INTP(vaplic->ip, new_val)) {
        BIT32_CLR_INTP(vaplic->ip, new_val);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_clr_pend(new_val);
        } else {
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
        }
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
    if (reg < APLIC_NUM_SETIx_REGS) ret = vaplic->ie[reg];
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
    if (reg < APLIC_NUM_SETIx_REGS && 
        vaplic_get_setie(vcpu, reg) != new_val) {
        /** Update virt setip array */
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vaplic->ie[reg] = new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/APLIC_NUM_SETIx_REGS; i++){
            /** Is this intp a phys. intp? */
            if(vaplic_get_hw(vcpu,i)){
                /** Update in phys. aplic */
                if(BIT32_GET_INTP(vaplic->ie, i) && ((new_val >> i) & 1)){
                    aplic_set_enbl(i);
                }
            } else {
                /** If intp is not phys. emul aplic behaviour */
                vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
            }
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
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        !BIT32_GET_INTP(vaplic->ie, new_val)) {
        BIT32_SET_INTP(vaplic->ie, new_val);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_set_enbl(new_val);
        } else {
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
        }
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
    if (reg < APLIC_NUM_CLRIx_REGS && 
        vaplic_get_setie(vcpu, reg) != new_val) {
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vaplic->ie[reg] &= ~new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/APLIC_NUM_CLRIx_REGS; i++){
            if(vaplic_get_hw(vcpu,i)){
                if(!BIT32_GET_INTP(vaplic->ie, i) && ((new_val >> i) & 1)){
                    aplic_clr_enbl(i);
                }
            } else {
                vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, i));
            }
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
static void vaplic_set_clrienum(struct vcpu *vcpu, uint32_t new_val){
    struct vaplic *vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        BIT32_GET_INTP(vaplic->ie, new_val)) {
        BIT32_CLR_INTP(vaplic->ie, new_val);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_clr_enbl(new_val);
        } else {
            vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, new_val));
        }
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Write to target register of a given interrupt
 * 
 * @param vcpu virtual cpu
 * @param intp_id interrupt ID
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
    
    /** If prio is 0, set to 1 (max) according to the spec*/
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
                vaplic->target[intp_id] = new_vaplic_target;
            }
        } else {
            vaplic->target[intp_id] = new_vaplic_target;
        }
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, intp_id));
    }
    spin_unlock(&vaplic->lock);
}

/**
 * @brief Read target register from a given interrupt
 * 
 * @param vcpu virtual cpu
 * @param intp_id interrupt ID
 * @return uint32_t value with target value
 */
static uint32_t vaplic_get_target(struct vcpu *vcpu, irqid_t intp_id){
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    uint32_t ret = 0;
    
    if (intp_id != 0 && intp_id < APLIC_MAX_INTERRUPTS){
        ret = vaplic->target[intp_id];
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
        if (new_val) 
            bitmap_set(vaplic->idelivery, idc_id);
        else
            bitmap_clear(vaplic->idelivery, idc_id);
    }
    spin_unlock(&vaplic->lock);

    vaplic_update_hart_line(vcpu, idc_id);
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
        if (new_val) 
            bitmap_set(vaplic->iforce, idc_id);
        else
            bitmap_clear(vaplic->iforce, idc_id);
    }
    spin_unlock(&vaplic->lock);

    vaplic_update_hart_line(vcpu, idc_id);
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
    spin_unlock(&vaplic->lock);

    vaplic_update_hart_line(vcpu, idc_id);
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
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    if (idc_id < vaplic->idc_num){
        ret = vaplic->topi_claimi[idc_id];
        /** Sepurious intp*/
        if (ret == 0){
            // Clear the virt iforce bit
            vaplic->iforce[idc_id] = 0;
            if(vaplic_get_hw(vcpu,(ret >> 16))){
                // Clear the physical iforce bit
                // aplic_idc_set_iforce(idc_id, 0);
            }
        }
        // Clear the virt pending bit for te read intp
        BIT32_CLR_INTP(vaplic->ip, (ret >> 16));
        if(vaplic_get_hw(vcpu,(ret >> 16))){
            // Clear the physical pending bit for te read intp
            aplic_idc_get_claimi(idc_id);
        }
        vaplic_update_hart_line(vcpu, idc_id);
    }
    return ret;
}

/**
 * @brief register access emulation functions
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

static void vaplic_emul_srccfg_access(struct emul_access *acc){
    int intp = (acc->addr & 0xFFF)/4;
    if (acc->write) {
        vaplic_set_sourcecfg(cpu()->vcpu, intp, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_sourcecfg(cpu()->vcpu, intp));
    }
}

static void vaplic_emul_setip_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_setip(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_setip(cpu()->vcpu, reg));
    }
}

static void vaplic_emul_setipnum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_setipnum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

static void vaplic_emul_in_clrip_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_in_clrip(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_in_clrip(cpu()->vcpu, reg));
    }
}

static void vaplic_emul_clripnum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_clripnum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

static void vaplic_emul_setie_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_setie(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_setie(cpu()->vcpu, reg));
    }
}

static void vaplic_emul_setienum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_setienum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

static void vaplic_emul_clrie_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_clrie(cpu()->vcpu, reg, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

static void vaplic_emul_clrienum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_clrienum(cpu()->vcpu, vcpu_readreg(cpu()->vcpu, acc->reg));
    }
}

static void vaplic_emul_target_access(struct emul_access *acc){
    int intp = (acc->addr & 0xFFF)/4;
    if (acc->write) {
        vaplic_set_target(cpu()->vcpu, intp, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_target(cpu()->vcpu, intp));
    }
}

static void vaplic_emul_idelivery_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) {
        vaplic_set_idelivery(cpu()->vcpu, idc_id, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_idelivery(cpu()->vcpu, idc_id));
    }
}

static void vaplic_emul_iforce_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) {
        vaplic_set_iforce(cpu()->vcpu, idc_id, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_iforce(cpu()->vcpu, idc_id));
    }
}

static void vaplic_emul_ithreshold_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) {
        vaplic_set_ithreshold(cpu()->vcpu, idc_id, vcpu_readreg(cpu()->vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_ithreshold(cpu()->vcpu, idc_id));
    }
}

static void vaplic_emul_topi_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) return;
    vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_topi(cpu()->vcpu, idc_id));
}

static void vaplic_emul_claimi_access(struct emul_access *acc, idcid_t idc_id){
    if (acc->write) return;
    vcpu_writereg(cpu()->vcpu, acc->reg, vaplic_get_claimi(cpu()->vcpu, idc_id));
}

// ============================================================================
void vaplic_inject(struct vcpu *vcpu, irqid_t intp_id)
{
    struct vaplic * vaplic = &vcpu->vm->arch.vaplic;
    spin_lock(&vaplic->lock);
    
    /** Intp has a valid ID and the virtual interrupt is not pending*/
    if (intp_id > 0 && intp_id < APLIC_MAX_INTERRUPTS && !vaplic_get_pend(vcpu, intp_id)){
        if(vaplic->srccfg[intp_id] != APLIC_SOURCECFG_SM_INACTIVE &&
           vaplic->srccfg[intp_id] != APLIC_SOURCECFG_SM_DETACH){
            BIT32_SET_INTP(vaplic->ip, intp_id);
        }
        vaplic_update_hart_line(vcpu, GET_HART_INDEX(vcpu, intp_id));
    }
    spin_unlock(&vaplic->lock);
}

static bool vaplic_domain_emul_reserved (uint16_t addr) {
    bool ret = false;
    if (ADDR_INSIDE_RANGE(addr, reserved1, setip)       ||
        ADDR_INSIDE_RANGE(addr, reserved2, setipnum)    ||
        ADDR_INSIDE_RANGE(addr, reserved3, in_clrip)    ||
        ADDR_INSIDE_RANGE(addr, reserved4, clripnum)    ||
        ADDR_INSIDE_RANGE(addr, reserved5, setie)       ||
        ADDR_INSIDE_RANGE(addr, reserved6, setienum)    ||
        ADDR_INSIDE_RANGE(addr, reserved7, clrie)       ||
        ADDR_INSIDE_RANGE(addr, reserved8, clrienum)    ||
        ADDR_INSIDE_RANGE(addr, reserved9, setipnum_le) ||
        ADDR_INSIDE_RANGE(addr, reserved10, genmsi)){
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
    bool read_only_zero = false;

    // only allow aligned word accesses
    if (acc->width != 4 || acc->addr & 0x3) return false;

    emul_addr = (acc->addr - 
                cpu()->vcpu->vm->arch.vaplic.aplic_domain_emul.va_base) & 
                0x3fff;

    if (vaplic_domain_emul_reserved(emul_addr)){
        read_only_zero = true;
    } else {
        switch (emul_addr >> 12){
            case 0:
                if (emul_addr == offsetof(struct aplic_global_hw, domaincfg)) {
                    vaplic_emul_domaincfg_access(acc);
                } else {
                    vaplic_emul_srccfg_access(acc);
                }
                break;
            case 1:
                switch (emul_addr >> 7){
                case offsetof(struct aplic_global_hw, setip) >> 7:
                    vaplic_emul_setip_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, setipnum) >> 7:
                    vaplic_emul_setipnum_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, in_clrip) >> 7:
                    vaplic_emul_in_clrip_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, clripnum) >> 7:
                    vaplic_emul_clripnum_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, setie) >> 7:
                    vaplic_emul_setie_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, setienum) >> 7:
                    vaplic_emul_setienum_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, clrie) >> 7:
                    vaplic_emul_clrie_access(acc);
                    break;
                case offsetof(struct aplic_global_hw, clrienum) >> 7:
                    vaplic_emul_clrienum_access(acc);
                    break;
                default:
                    read_only_zero = true;
                    break;
                }
                break;
            case 3:
                if (emul_addr == offsetof(struct aplic_global_hw, genmsi)) {
                    read_only_zero = true;
                } else {
                    vaplic_emul_target_access(acc);
                }
                break;
            default:
                read_only_zero = true;
                break;
        }
    }

    if (read_only_zero){
        if(!acc->write) {
            vcpu_writereg(cpu()->vcpu, acc->reg, 0);
        }
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
        case offsetof(struct aplic_hart_hw, idelivery):
            vaplic_emul_idelivery_access(acc, idc_id);
            break;
        case offsetof(struct aplic_hart_hw, iforce):
            vaplic_emul_iforce_access(acc, idc_id);
            break;
        case offsetof(struct aplic_hart_hw, ithreshold):
            vaplic_emul_ithreshold_access(acc, idc_id);
            break;
        case offsetof(struct aplic_hart_hw, topi):
            vaplic_emul_topi_access(acc, idc_id);
            break;
        case offsetof(struct aplic_hart_hw, claimi):
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

void virqc_init(struct vm *vm, struct arch_platform *arch_platform)
{
    if (cpu()->id == vm->master) {
        vm->arch.vaplic.aplic_domain_emul = (struct emul_mem) {
            .va_base = arch_platform->irqc.aia.aplic.base,
            .size = sizeof(struct aplic_global_hw),
            .handler = vaplic_domain_emul_handler
        };

        vm_emul_add_mem(vm, &vm->arch.vaplic.aplic_domain_emul);

        vm->arch.vaplic.aplic_idc_emul = (struct emul_mem) {
            .va_base = arch_platform->irqc.aia.aplic.base + APLIC_IDC_OFF,
            .size = sizeof(struct aplic_hart_hw)*APLIC_DOMAIN_NUM_HARTS,
            .handler = vaplic_idc_emul_handler
        };

        vm_emul_add_mem(vm, &vm->arch.vaplic.aplic_idc_emul);

        /* 1 IDC per hart */
        vm->arch.vaplic.idc_num = vm->cpu_num;
    }
}