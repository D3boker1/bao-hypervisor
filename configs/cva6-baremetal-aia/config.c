#include <config.h>

VM_IMAGE(baremetal_image, XSTR(GUEST_IMGS/baremetal.bin));

struct config config = {
    
    CONFIG_HEADER

    .vmlist_size = 1,
    .vmlist = {
        { 
            .image = {
                .base_addr = 0x80200000,
                .load_addr = VM_IMAGE_OFFSET(baremetal_image),
                .size = VM_IMAGE_SIZE(baremetal_image)
            },

            .entry = 0x80200000,

            .platform = {
                .cpu_num = 1,
                
                .region_num = 1,
                .regions =  (struct vm_mem_region[]) {
                    {
                        // .base = 0x80200000,
                        // .size = 0x20000000,
                        .base = 0x80200000,
                        .place_phys = true,
                        .phys = 0x90000000,
                        .size = 0x20E00000,
                    }
                },

                .dev_num = 2,
                .devs =  (struct vm_dev_region[]) {
                    {   /** uart */
                        .pa = 0x10000000,   
                        .va = 0x10000000,  
                        .size = 0x00010000,  
                        .interrupt_num = 1,
                        .interrupts = (irqid_t[]) {1}
                    },
                    {   /** timer */
                        .pa = 0x18000000,   
                        .va = 0x18000000,  
                        .size = 0x00001000,  
                        .interrupt_num = 4,
                        .interrupts = (irqid_t[]) {4,5,6,7}
                    },
                    // {   /** spi */
                    //     .pa = 0x20000000,   
                    //     .va = 0x20000000,  
                    //     .size = 0x00001000,  
                    //     .interrupt_num = 1,
                    //     .interrupts = (irqid_t[]) {2}
                    // },
                    // {   /** IMSIC */
                    //     .pa = 0x28001000,   
                    //     .va = 0x28000000,  
                    //     .size = 0x00001000,  
                    //     .interrupt_num = 0,
                    //     .interrupts = (irqid_t[]) {}
                    // },
                    // {   /** lowrisc-eth */
                    //     .pa = 0x30000000,   
                    //     .va = 0x30000000,  
                    //     .size = 0x00008000,  
                    //     .interrupt_num = 1,
                    //     .interrupts = (irqid_t[]) {3}
                    // },
                    // {   /** gpio */
                    //     .pa = 0x40000000,   
                    //     .va = 0x40000000,  
                    //     .size = 0x00010000,  
                    //     .interrupt_num = 0,
                    //     .interrupts = (irqid_t[]) {}
                    // },
                },

                .arch = {
                //    .plic_base = 0xd000000,
                //    .imsic_base = 0x28000000,
                    .irqc = {
                        .aia = {
                            .aplic = {
                                .base = 0xd000000,
                            },
                            .imsic = {
                                .base = 0x28000000,
                            }, 
                        },
                    },
                }
            },
        }
     }
};
