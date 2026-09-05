/*
 * kstub/kernel.h - what fonts.c means by <kernel.h> when it is built
 * into the PC3 device server: struct p_tab, and the C library.  Nothing
 * else in the kernel's header is wanted by the files this server
 * compiles, and nothing here is made up - struct p_tab is the server's,
 * defined once in pc3d.h.
 */
#ifndef KSTUB_KERNEL_H
#define KSTUB_KERNEL_H

#include <stdint.h>
#include <string.h>
#include "../pc3d.h"

#endif
