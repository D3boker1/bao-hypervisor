#ifndef _IRQC_H_
#define _IRQC_H_

#include <bao.h>
#include <plic.h>
#include <cpu.h>
#include <vplic.h>
#include <arch/sbi.h>

#define IRQC_MAX_INTERRUPTS (1024)

#define HART_REG_OFF PLIC_CLAIMCMPLT_OFF
#define IRQC_HART_INST PLIC_PLAT_CNTXT_NUM

static inline void irqc_init()
{
    plic_init();
}

static inline void irqc_cpu_init()
{
    plic_cpu_init();
}

static inline void irqc_set_enbl(irqid_t int_id, bool en)
{
    plic_set_enbl(cpu()->arch.plic_cntxt, int_id, en);
}

static inline void irqc_set_prio(irqid_t int_id)
{
    plic_set_prio(int_id, 0xFE);
}

static inline void irqc_handle()
{
    plic_handle();
}

static inline bool irqc_get_pend(irqid_t int_id)
{
    return plic_get_pend(int_id);
}

static inline void irqc_clear_pend(irqid_t int_id)
{
    WARNING("trying to clear external interrupt");
}

static inline void irqc_send_ipi(cpuid_t target_cpu, irqid_t ipi_id)
{
    sbi_send_ipi(1ULL << target_cpu, 0);
}

static inline void virqc_set_hw(struct vm *vm, irqid_t id)
{
    vplic_set_hw(vm, id);
}

#endif //_IRQC_H_