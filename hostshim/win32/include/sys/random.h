/* sys/random.h - getrandom from the system's RNG. */
#ifndef PC3W_SYS_RANDOM_H
#define PC3W_SYS_RANDOM_H
#include <stddef.h>
#include <pc3w.h>
static inline long getrandom(void *buf, size_t n, unsigned flags)
{
	(void)flags;
	return pc3w_random(buf, n) == 0 ? (long)n : -1;
}
#endif
