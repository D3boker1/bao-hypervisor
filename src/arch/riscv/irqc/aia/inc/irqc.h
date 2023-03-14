#ifndef _IRQC_H_
#define _IRQC_H_

#include <aplic.h>
#include <imsic.h>
#include <cpu.h>
#include <vaplic.h>
#include <arch/sbi.h>

#define XPLIC_MAX_INTERRUPTS (1024)

#define HART_REG_OFF APLIC_IDC_OFF
#define IRQC_HART_INST APLIC_PLAT_IDC_NUM

static inline void irqc_init()
{
    aplic_init();
}

static inline void irqc_cpu_init()
{
    if(aplic_msi_mode()){
        imsic_init();        
    } else {
        aplic_idc_init();
    }
}

static inline void irqc_set_enbl(irqid_t int_id, bool en)
{
    aplic_set_ienum(int_id);
    if(aplic_msi_mode())
        imsic_set_enbl(int_id);
}

static inline void irqc_set_prio(irqid_t int_id)
{
    if(aplic_msi_mode()){
        aplic_set_target(int_id, (cpu()->id << APLIC_TARGET_HART_IDX_SHIFT) | 
                         int_id);
    } else {
        aplic_set_target(int_id, (cpu()->id << APLIC_TARGET_HART_IDX_SHIFT) | 
                         APLIC_TARGET_PRIO_DEFAULT);
    }
}

static inline void irqc_handle()
{
    if (aplic_msi_mode()) {
        imsic_handle();
    } else {
        aplic_handle();
    }
}

static inline bool irqc_get_pend(irqid_t int_id)
{
    if(aplic_msi_mode()){
        return imsic_get_pend(int_id);
    } else {
        return aplic_get_pend(int_id);
    }
}

static inline void irqc_set_clrienum(irqid_t int_id)
{
    aplic_set_clrienum(int_id);
}

static inline void irqc_send_ipi(cpuid_t target_cpu, irqid_t ipi_id)
{
    if(aplic_msi_mode()){
        imsic_send_msi(target_cpu, ipi_id);
    } else {
        sbi_send_ipi(1ULL << target_cpu, 0);
    }
}

static inline void virqc_set_hw(struct vm *vm, irqid_t id)
{
    vaplic_set_hw(vm, id);
}

#endif //_IRQC_H_