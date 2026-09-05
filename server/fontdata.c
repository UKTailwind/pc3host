/*
 * fontdata.c - font1, the console's 8x12.
 *
 * fonts.c declares it extern because in the kernel console.c carries
 * the one copy; the server has no console.c yet, so the header is
 * included here instead.  The other eight fonts come with fonts.c.
 */
#include "console_font.h"
