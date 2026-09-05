/*
 * kstub/kdata.h - udata for fonts.c.  The kernel keeps the current
 * process in udata.u_ptab; the server sets it to the requesting client
 * before every dispatch, which is exactly what the kernel's syscall
 * entry does.  fonts.c uses it to refuse another process's font slot.
 */
#ifndef KSTUB_KDATA_H
#define KSTUB_KDATA_H

#include "kernel.h"

struct u_data {
	struct p_tab *u_ptab;
};
extern struct u_data udata;

#endif
