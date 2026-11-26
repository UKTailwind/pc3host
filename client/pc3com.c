/*
 * pc3com.c - a PC's serial ports, for the runtime's OPEN "port:..." AS #n
 * (mmb_runtime.c, mm_open_com).
 *
 * The port is named as the OS names it (Peter, 26 Nov 2026): COM3 on
 * Windows, /dev/ttyUSB0 on Linux - and on Linux a name with no slash is
 * taken to be in /dev, so ttyUSB0 is /dev/ttyUSB0.  Raw bytes, no flow
 * control, the modem lines DTR and RTS raised as a terminal program
 * raises them.  Reading never waits: 0 is "nothing yet".  Writing waits
 * until the OS has taken all of it, which is what the firmware's TX
 * buffer comes to at these speeds.
 *
 * Handles are small integers, so that the runtime, which also builds
 * for the board, sees no OS type.
 *
 *   int pc3_com_open(const char *name, long baud, int bits, int parity,
 *                    int stop)        bits 7 or 8, parity 0 none 1 even
 *                                     2 odd, stop 1 or 2; -1 and errno
 *   int pc3_com_read(int h, void *buf, int n)
 *   int pc3_com_write(int h, const void *buf, int n)
 *   int pc3_com_drain(int h)          wait for what was written to go
 *   int pc3_com_close(int h)
 *   int pc3_com_list(char *buf, int size)   LIST SERIAL PORTS: a line
 *                                     "name<TAB>description" for each
 *                                     port, the count, or -1
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PC3_COM_MAX 16

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>

static HANDLE coms[PC3_COM_MAX];

static HANDLE com_h(int h)
{
	if (h < 0 || h >= PC3_COM_MAX || coms[h] == NULL)
		return NULL;
	return coms[h];
}

int pc3_com_open(const char *name, long baud, int bits, int parity, int stop)
{
	char path[300];
	HANDLE h;
	DCB d;
	COMMTIMEOUTS t;
	int i;

	for (i = 0; i < PC3_COM_MAX && coms[i] != NULL; i++)
		;
	if (i == PC3_COM_MAX) {
		errno = EMFILE;
		return -1;
	}
	/* \\.\COM10 and above need the device namespace; it is right for
	 * COM1 to COM9 too */
	snprintf(path, sizeof path, "\\\\.\\%s", name);
	h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		errno = GetLastError() == ERROR_ACCESS_DENIED ? EBUSY : ENOENT;
		return -1;
	}
	memset(&d, 0, sizeof d);
	d.DCBlength = sizeof d;
	if (!GetCommState(h, &d)) {
		CloseHandle(h);
		errno = ENOTTY;
		return -1;
	}
	d.BaudRate = (DWORD)baud;
	d.ByteSize = (BYTE)bits;
	d.Parity = parity == 1 ? EVENPARITY : parity == 2 ? ODDPARITY : NOPARITY;
	d.StopBits = stop == 2 ? TWOSTOPBITS : ONESTOPBIT;
	d.fBinary = TRUE;
	d.fParity = parity != 0;
	d.fOutxCtsFlow = FALSE;
	d.fOutxDsrFlow = FALSE;
	d.fDtrControl = DTR_CONTROL_ENABLE;
	d.fDsrSensitivity = FALSE;
	d.fOutX = FALSE;
	d.fInX = FALSE;
	d.fNull = FALSE;
	d.fRtsControl = RTS_CONTROL_ENABLE;
	d.fAbortOnError = FALSE;
	if (!SetCommState(h, &d)) {
		CloseHandle(h);
		errno = EINVAL;
		return -1;
	}
	/* a read returns at once with what there is */
	memset(&t, 0, sizeof t);
	t.ReadIntervalTimeout = MAXDWORD;
	SetCommTimeouts(h, &t);
	PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
	coms[i] = h;
	return i;
}

int pc3_com_read(int h, void *buf, int n)
{
	HANDLE x = com_h(h);
	DWORD got = 0;

	if (x == NULL) {
		errno = EBADF;
		return -1;
	}
	if (!ReadFile(x, buf, (DWORD)n, &got, NULL)) {
		errno = EIO;
		return -1;
	}
	return (int)got;
}

int pc3_com_write(int h, const void *buf, int n)
{
	HANDLE x = com_h(h);
	const char *p = (const char *)buf;
	DWORD put;
	int done = 0;

	if (x == NULL) {
		errno = EBADF;
		return -1;
	}
	while (done < n) {
		if (!WriteFile(x, p + done, (DWORD)(n - done), &put, NULL)) {
			errno = EIO;
			return -1;
		}
		done += (int)put;
	}
	return n;
}

int pc3_com_drain(int h)
{
	HANDLE x = com_h(h);

	if (x == NULL) {
		errno = EBADF;
		return -1;
	}
	FlushFileBuffers(x);
	return 0;
}

int pc3_com_close(int h)
{
	HANDLE x = com_h(h);

	if (x == NULL) {
		errno = EBADF;
		return -1;
	}
	CloseHandle(x);
	coms[h] = NULL;
	return 0;
}

/* the name Device Manager gives a port - "USB Serial Device (COM44)" -
 * without its "(COM44)", or "" */
static void com_friendly(const char *com, char *out, int size)
{
	/* GUID_DEVCLASS_PORTS, spelt out rather than through initguid */
	static const GUID ports = { 0x4d36e978, 0xe325, 0x11ce,
		{ 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
	HDEVINFO set = SetupDiGetClassDevsA(&ports, NULL, NULL, DIGCF_PRESENT);
	SP_DEVINFO_DATA dev;
	char name[256], want[40], *at;
	DWORD i;

	out[0] = 0;
	if (set == INVALID_HANDLE_VALUE)
		return;
	snprintf(want, sizeof want, "(%s)", com);
	dev.cbSize = sizeof dev;
	for (i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
		if (!SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_FRIENDLYNAME,
						       NULL, (PBYTE)name,
						       sizeof name - 1, NULL))
			continue;
		name[sizeof name - 1] = 0;
		at = strstr(name, want);
		if (at == NULL)
			continue;
		while (at > name && at[-1] == ' ')
			at--;
		*at = 0;
		snprintf(out, (size_t)size, "%s", name);
		break;
	}
	SetupDiDestroyDeviceInfoList(set);
}

/* HKLM\HARDWARE\DEVICEMAP\SERIALCOMM: a value for each serial device
 * present, its data the COM name and its name the driver's device - the
 * description when Device Manager has no friendly name for it */
int pc3_com_list(char *buf, int size)
{
	HKEY k;
	char vname[256], data[64], what[256];
	DWORD i, vlen, dlen, type;
	int used = 0, n = 0;

	buf[0] = 0;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
			  0, KEY_READ, &k) != ERROR_SUCCESS)
		return 0;
	for (i = 0;; i++) {
		vlen = sizeof vname;
		dlen = sizeof data - 1;
		if (RegEnumValueA(k, i, vname, &vlen, NULL, &type,
				  (LPBYTE)data, &dlen) != ERROR_SUCCESS)
			break;
		if (type != REG_SZ)
			continue;
		data[dlen] = 0;
		com_friendly(data, what, (int)sizeof what);
		if (!what[0])
			snprintf(what, sizeof what, "%s", vname);
		if (used + (int)strlen(data) + (int)strlen(what) + 3 >= size)
			break;
		used += snprintf(buf + used, (size_t)(size - used), "%s\t%s\n",
				 data, what);
		n++;
	}
	RegCloseKey(k);
	return n;
}

#else
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int coms[PC3_COM_MAX];		/* fd + 1, 0 free */

static int com_fd(int h)
{
	if (h < 0 || h >= PC3_COM_MAX || coms[h] == 0)
		return -1;
	return coms[h] - 1;
}

static speed_t com_speed(long baud)
{
	static const struct { long b; speed_t s; } t[] = {
		{ 50, B50 }, { 75, B75 }, { 110, B110 }, { 134, B134 },
		{ 150, B150 }, { 200, B200 }, { 300, B300 }, { 600, B600 },
		{ 1200, B1200 }, { 1800, B1800 }, { 2400, B2400 },
		{ 4800, B4800 }, { 9600, B9600 }, { 19200, B19200 },
		{ 38400, B38400 }, { 57600, B57600 }, { 115200, B115200 },
		{ 230400, B230400 },
#ifdef B460800
		{ 460800, B460800 },
#endif
#ifdef B500000
		{ 500000, B500000 },
#endif
#ifdef B576000
		{ 576000, B576000 },
#endif
#ifdef B921600
		{ 921600, B921600 },
#endif
#ifdef B1000000
		{ 1000000, B1000000 },
#endif
#ifdef B1152000
		{ 1152000, B1152000 },
#endif
#ifdef B1500000
		{ 1500000, B1500000 },
#endif
#ifdef B2000000
		{ 2000000, B2000000 },
#endif
#ifdef B2500000
		{ 2500000, B2500000 },
#endif
#ifdef B3000000
		{ 3000000, B3000000 },
#endif
#ifdef B3500000
		{ 3500000, B3500000 },
#endif
#ifdef B4000000
		{ 4000000, B4000000 },
#endif
	};
	unsigned i;

	for (i = 0; i < sizeof t / sizeof t[0]; i++)
		if (t[i].b == baud)
			return t[i].s;
	return (speed_t)0;
}

int pc3_com_open(const char *name, long baud, int bits, int parity, int stop)
{
	char path[300];
	struct termios tio;
	speed_t sp = com_speed(baud);
	int i, fd, fl;

	if (sp == (speed_t)0) {
		errno = EINVAL;			/* a rate termios has no name for */
		return -1;
	}
	for (i = 0; i < PC3_COM_MAX && coms[i] != 0; i++)
		;
	if (i == PC3_COM_MAX) {
		errno = EMFILE;
		return -1;
	}
	if (strchr(name, '/') == NULL)
		snprintf(path, sizeof path, "/dev/%s", name);
	else
		snprintf(path, sizeof path, "%s", name);
	/* O_NONBLOCK so that the open does not wait for carrier; the port
	 * then blocks for writes and VMIN 0 VTIME 0 makes reads return */
	fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0)
		return -1;
	fl = fcntl(fd, F_GETFL);
	if (fl >= 0)
		fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
	if (tcgetattr(fd, &tio) < 0) {
		close(fd);
		return -1;
	}
	cfmakeraw(&tio);
	tio.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
	tio.c_cflag &= ~CRTSCTS;
#endif
	tio.c_cflag |= (bits == 7 ? CS7 : CS8) | CLOCAL | CREAD;
	if (parity)
		tio.c_cflag |= PARENB | (parity == 2 ? PARODD : 0);
	if (stop == 2)
		tio.c_cflag |= CSTOPB;
	tio.c_iflag &= ~(IXON | IXOFF | IXANY);
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;
	cfsetispeed(&tio, sp);
	cfsetospeed(&tio, sp);
	if (tcsetattr(fd, TCSANOW, &tio) < 0) {
		close(fd);
		return -1;
	}
	tcflush(fd, TCIOFLUSH);
	coms[i] = fd + 1;
	return i;
}

int pc3_com_read(int h, void *buf, int n)
{
	int fd = com_fd(h), got;

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	got = (int)read(fd, buf, (size_t)n);
	if (got < 0 && (errno == EAGAIN || errno == EINTR))
		return 0;
	return got;
}

int pc3_com_write(int h, const void *buf, int n)
{
	int fd = com_fd(h), done = 0, w;
	const char *p = (const char *)buf;

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	while (done < n) {
		w = (int)write(fd, p + done, (size_t)(n - done));
		if (w < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		done += w;
	}
	return n;
}

int pc3_com_drain(int h)
{
	int fd = com_fd(h);

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	return tcdrain(fd);
}

int pc3_com_close(int h)
{
	int fd = com_fd(h);

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	close(fd);
	coms[h] = 0;
	return 0;
}
/* the first line of a small sysfs file, or "" */
static void com_sysline(const char *path, char *out, int size)
{
	FILE *f = fopen(path, "r");
	int n;

	out[0] = 0;
	if (f == NULL)
		return;
	if (fgets(out, size, f) == NULL)
		out[0] = 0;
	fclose(f);
	n = (int)strlen(out);
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
		out[--n] = 0;
}

static int com_namecmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* /sys/class/tty: a tty with a device behind it is a serial port - the
 * consoles and ptys have none.  An 8250 ttyS slot with no UART in it
 * says type 0 and is left out.  The description is the USB device's
 * product, or the driver's name. */
int pc3_com_list(char *buf, int size)
{
	DIR *d = opendir("/sys/class/tty");
	struct dirent *e;
	char *names[256], path[512], what[128], link[512];
	int nn = 0, i, used = 0, n = 0;
	ssize_t l;

	buf[0] = 0;
	if (d == NULL)
		return 0;
	while ((e = readdir(d)) != NULL && nn < 256) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof path, "/sys/class/tty/%s/device", e->d_name);
		if (access(path, F_OK) != 0)
			continue;
		if (strncmp(e->d_name, "ttyS", 4) == 0) {
			snprintf(path, sizeof path, "/sys/class/tty/%s/type",
				 e->d_name);
			com_sysline(path, what, sizeof what);
			if (strcmp(what, "0") == 0)
				continue;
		}
		names[nn++] = strdup(e->d_name);
	}
	closedir(d);
	qsort(names, (size_t)nn, sizeof names[0], com_namecmp);
	for (i = 0; i < nn; i++) {
		/* a USB adapter's product: the interface's parent for ACM,
		 * one further up for a usb-serial port */
		snprintf(path, sizeof path, "/sys/class/tty/%s/device/../product",
			 names[i]);
		com_sysline(path, what, sizeof what);
		if (!what[0]) {
			snprintf(path, sizeof path,
				 "/sys/class/tty/%s/device/../../product", names[i]);
			com_sysline(path, what, sizeof what);
		}
		if (!what[0]) {
			snprintf(path, sizeof path, "/sys/class/tty/%s/device/driver",
				 names[i]);
			l = readlink(path, link, sizeof link - 1);
			if (l > 0) {
				char *b;

				link[l] = 0;
				b = strrchr(link, '/');
				snprintf(what, sizeof what, "%.120s", b ? b + 1 : link);
			}
		}
		if (used + (int)strlen(names[i]) + (int)strlen(what) + 8 < size) {
			used += snprintf(buf + used, (size_t)(size - used),
					 "/dev/%s\t%s\n", names[i], what);
			n++;
		}
		free(names[i]);
	}
	return n;
}
#endif
