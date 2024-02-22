#include <config.h>


VM_IMAGE(baremetal_image, XSTR(/home/d3boker1/Build/cva6-sdk/install64/baremetal.bin));

struct config config = {
    
    CONFIG_HEADER

    .vmlist_size = 1,
    .vmlist = {

        // VM 1: baremetal
        { 
            .image = {
                .base_addr = 0x80200000,
                .load_addr = VM_IMAGE_OFFSET(baremetal_image),
                .size = VM_IMAGE_SIZE(baremetal_image)
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
                   .irqc.aplic.base = 0xd000000,
                }
            },
        }
     }
};