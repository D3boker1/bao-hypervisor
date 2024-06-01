#include <config.h>


VM_IMAGE(linux_image, XSTR(GUEST_IMGS/linux_wrapper.bin));

struct config config = {
    
    CONFIG_HEADER

    .vmlist_size = 1,
    .vmlist = {

        // VM 1: Linux
        { 
            .image = {
                .base_addr = 0x80200000,
                .load_addr = VM_IMAGE_OFFSET(linux_image),
                .size = VM_IMAGE_SIZE(linux_image),
                .inplace = true
            },

            .entry = 0x80200000,

            .platform = {
                .cpu_num = 2,
                
                .region_num = 1,
                .regions =  (struct vm_mem_region[]) {
                    {
                        .base = 0x80200000,
                        .size = 0x10000000,
                    }
                },

                .dev_num = 1,
                .devs =  (struct vm_dev_region[]) {
                    {   // UART
                        .pa = 0x40000000,
                        .va = 0x40000000,
                        .size = 0x00010000,
                        .interrupt_num = 1,
                        .interrupts = (irqid_t[]) {2}
                    },
                },

                .arch = {
                   .irqc.plic.base = 0xc000000,
                }
            },
        }
     }
};