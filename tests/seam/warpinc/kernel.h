/*
 * warpinc/kernel.h - what display_priv.h includes in the board's pixel
 * order, for tests/seam/warp_core.c.  The core it is compiled with uses
 * nothing from the kernel; this only lets the header be read as the
 * board reads it, so the test exercises the board's own PX4_ macros
 * rather than a copy of them.
 */
#ifndef WARPINC_KERNEL_H
#define WARPINC_KERNEL_H
struct p_tab;
#endif
