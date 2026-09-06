/* sys/mman.h - anonymous mappings, always below 4G.  Every mapping a
 * hosted PC3 program makes is memory a 32-bit program address has to
 * reach - bcrun's VM, the PSRAM arena, the PIO buffer, a kernel font -
 * so MAP_32BIT is the rule here rather than a request. */
#ifndef PC3W_SYS_MMAN_H
#define PC3W_SYS_MMAN_H
#include <stddef.h>
#include <pc3w.h>
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_SHARED    1
#define MAP_PRIVATE   2
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_32BIT     0x40
#define MAP_FAILED    ((void *)-1)
static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
	void *p;
	(void)prot; (void)flags; (void)off;
	if (fd != -1)
		return MAP_FAILED;
	p = pc3w_map_low(addr, len);
	return p ? p : MAP_FAILED;
}
static inline int munmap(void *addr, size_t len)
{
	return pc3w_unmap(addr, len);
}
#endif
