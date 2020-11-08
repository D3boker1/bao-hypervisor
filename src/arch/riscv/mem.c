/**
 * Bao Hypervisor
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

#include <mem.h>

#include <platform.h>
#include <cpu.h>

void as_arch_init(addr_space_t *as) {}

void mem_arch_init(uint64_t load_addr, uint64_t config_addr)
{
    if (cpu.id == CPU_MASTER) {
        const int lvl = 0;
        size_t lvl_size = pt_lvlsize(&cpu.as.pt, lvl);
        uintptr_t lvl_mask = ~(lvl_size - 1);
        pte_t *pt = cpu.as.pt.root;

        /**
         *  Create identity mapping of existing physical memory regions using
         * the largest pages possible pte (in riscv this is always at level 0
         * pt).
         */

        for (int i = 0; i < platform.region_num; i++) {
            struct mem_region *reg = &platform.regions[i];
            uintptr_t base = reg->base & lvl_mask;
            uintptr_t top = (reg->base + reg->size) & lvl_mask;
            int num_entries = ((top - base - 1) / lvl_size) + 1;

            uintptr_t addr = base;
            for (int j = 0; j < num_entries; j++) {
                int index = PTE_INDEX(lvl, addr);
                pte_set(&pt[index], addr, PTE_SUPERPAGE, PTE_HYP_FLAGS);
                addr += lvl_size;
            }
        }
    }

    cpu_sync_barrier(&cpu_glb_sync);
}

typedef struct cpu cpu_t;
void switch_space(cpu_t *new_cpu, uint64_t new_rootpt_pa)
{
    ERROR("switch address space not implemented");
}

bool mem_translate(addr_space_t *as, void *va, uint64_t *pa)
{
    pte_t *pte = pt_get_pte(&as->pt, as->pt.dscr->lvls - 1, va);
    if (pte && pte_valid(pte)) {
        *pa = pte_addr(pte);
        *pa = (*pa & PAGE_ADDR_MSK) | ((uint64_t)va & ~PAGE_ADDR_MSK);
        return true;
    } else {
        return false;
    }
}