/* sys/statvfs.h - a drive's size and free space, which the runtime asks
 * for MM.INFO(DISK SIZE) and MM.INFO(FREE SPACE).  Windows answers in
 * bytes (GetDiskFreeSpaceEx); here they are counted in 512-byte blocks,
 * the Fuzix libc's unit, which every Windows volume's sizes divide. */
#ifndef PC3W_SYS_STATVFS_H
#define PC3W_SYS_STATVFS_H
#include <pc3w.h>
struct statvfs {
	unsigned long f_bsize;
	unsigned long f_frsize;
	unsigned long long f_blocks;
	unsigned long long f_bfree;
	unsigned long long f_bavail;
	unsigned long long f_files;
	unsigned long long f_ffree;
	unsigned long long f_favail;
	unsigned long f_fsid;
	unsigned long f_flag;
	unsigned long f_namemax;
};
static inline int statvfs(const char *path, struct statvfs *v)
{
	unsigned long long total, avail, nfree;

	if (pc3w_diskspace(path, &total, &avail, &nfree) != 0)
		return -1;
	v->f_bsize = v->f_frsize = 512;
	v->f_blocks = total / 512;
	v->f_bfree = nfree / 512;
	v->f_bavail = avail / 512;
	v->f_files = v->f_ffree = v->f_favail = 0;
	v->f_fsid = v->f_flag = 0;
	v->f_namemax = 255;
	return 0;
}
#endif
