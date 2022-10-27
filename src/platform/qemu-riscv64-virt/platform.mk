# Architecture definition
ARCH:=riscv
# CPU definition
CPU:=

# Interrupt controller definition
#IRQC:=plic
IRQC:=aia

drivers := sbi_uart

platform-cppflags =
platform-cflags = 
platform-asflags =
platform-ldflags =