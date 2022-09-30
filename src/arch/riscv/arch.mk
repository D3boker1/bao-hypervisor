CROSS_COMPILE := riscv64-unknown-elf-

arch-cppflags = 
arch-cflags = -mcmodel=medany -march=rv64g
arch-asflags =
arch-ldflags = -z common-page-size=0x1000

# Interrupt controller source files
irqc_arch_dir=$(src_dir)/arch/riscv/irqc/$(IRQC)