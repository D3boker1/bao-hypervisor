## SPDX-License-Identifier: GPL-2.0
## Copyright (c) Bao Project and Contributors. All rights reserved.

# Architecture definition
ARCH:=riscv
# CPU definition
CPU:=

drivers := 8250_uart

platform-cppflags =
platform-cflags = 
platform-asflags =
platform-ldflags =

platform_description:=cva6_desc.c

# Interrupt controller definition
IRQC:=PLIC