# Architecture definition
ARCH:=riscv
# CPU definition
CPU:=
IRQC:=PLIC

drivers := 8250_uart

platform_description:=alsaqr_desc.c
platform-cppflags =
platform-cflags = 
platform-asflags =
platform-ldflags =
