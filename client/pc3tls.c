/*
 * pc3tls.c - the board's IPPROTO_TLS socket, on a PC, through mbedtls.
 *
 * See pc3tls.h for the contract.  One table of descriptors, one shared
 * configuration (the CA chain, the RNG), one ssl context per
 * connection.  mbedtls's own net_sockets BIO does the reads and writes
 * on the descriptor and turns EAGAIN into WANT_READ/WANT_WRITE, which
 * come back out of here as EAGAIN - so a program polling a non-blocking
 * socket sees exactly what it sees from a TCP one.
 *
 * PC3_TLS_DEBUG=1 in the environment prints what mbedtls said when a
 * handshake or a read fails, since the program only ever learns
 * "timeout" or "connection lost".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"

#include "pc3tls.h"

#define TLS_MAX 16
#define SYSTEM_CA "/etc/ssl/certs/ca-certificates.crt"

enum { TLS_TCP = 0, TLS_HANDSHAKE, TLS_UP };

struct tlsfd {
	int fd;				/* -1 = free */
	int state;
	char host[256];
	mbedtls_ssl_context ssl;
	mbedtls_net_context net;
	int have_ssl;
};

static struct tlsfd tab[TLS_MAX];
static int ntab;			/* slots ever used (fd = -1 in the rest) */
static int inited, debug;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cacert;
static mbedtls_ssl_config conf;
static int ca_loaded;

static void note(const char *what, int rc)
{
	char e[128];

	if (!debug)
		return;
	mbedtls_strerror(rc, e, sizeof e);
	fprintf(stderr, "pc3tls: %s: -0x%04x %s\n", what, (unsigned)-rc, e);
}

static int init(void)
{
	int rc;

	if (inited)
		return 0;
	debug = getenv("PC3_TLS_DEBUG") != NULL;
	if (psa_crypto_init() != PSA_SUCCESS)
		return -1;
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);
	mbedtls_x509_crt_init(&cacert);
	mbedtls_ssl_config_init(&conf);
	rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
				   (const unsigned char *)"pc3tls", 6);
	if (rc) {
		note("ctr_drbg_seed", rc);
		return -1;
	}
	rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
					 MBEDTLS_SSL_TRANSPORT_STREAM,
					 MBEDTLS_SSL_PRESET_DEFAULT);
	if (rc) {
		note("ssl_config_defaults", rc);
		return -1;
	}
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
	/* the machine's own roots, if it has them: authenticated by
	 * default, where the board is encrypted-only until WEB TLS CA */
	if (mbedtls_x509_crt_parse_file(&cacert, SYSTEM_CA) == 0 && cacert.version)
		ca_loaded = 1;
	mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
	mbedtls_ssl_conf_authmode(&conf, ca_loaded ? MBEDTLS_SSL_VERIFY_REQUIRED
						   : MBEDTLS_SSL_VERIFY_OPTIONAL);
	inited = 1;
	return 0;
}

static struct tlsfd *slot(int fd)
{
	int i;

	for (i = 0; i < ntab; i++)
		if (tab[i].fd == fd)
			return &tab[i];
	return NULL;
}

int pc3_tls_isfd(int fd)
{
	return fd >= 0 && slot(fd) != NULL;
}

int pc3_tls_socket(void)
{
	struct tlsfd *t = NULL;
	int fd, i;

	if (init() < 0) {
		errno = EPROTONOSUPPORT;
		return -1;
	}
	for (i = 0; i < ntab; i++)
		if (tab[i].fd < 0) {
			t = &tab[i];
			break;
		}
	if (!t) {
		if (ntab >= TLS_MAX) {
			errno = EMFILE;
			return -1;
		}
		t = &tab[ntab++];
	}
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	memset(t, 0, sizeof *t);
	t->fd = fd;
	t->state = TLS_TCP;
	return fd;
}

int pc3_tls_sethost(int fd, const char *host)
{
	struct tlsfd *t = slot(fd);

	if (!t) {
		errno = EBADF;
		return -1;
	}
	snprintf(t->host, sizeof t->host, "%s", host ? host : "");
	return 0;
}

/* WEB TLS CA: the program's bundle replaces whatever was loaded.  PEM
 * needs its terminating NUL inside the length mbedtls is given; the
 * kernel takes it the same way (NUL terminated) so a program that
 * satisfied the board satisfies this. */
int pc3_tls_ca(const void *buf, uint32_t len)
{
	const unsigned char *b = buf;
	unsigned char *copy = NULL;
	int rc;

	if (init() < 0) {
		errno = EINVAL;
		return -1;
	}
	mbedtls_x509_crt_free(&cacert);
	mbedtls_x509_crt_init(&cacert);
	if (!buf || len == 0) {
		/* WEB TLS NOVERIFY: no bundle, and the kernel's meaning of
		 * none - encrypted, not authenticated */
		ca_loaded = 0;
		mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
		mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
		return 0;
	}
	if (len > 10 && memcmp(b, "-----BEGIN", 10) == 0) {
		uint32_t n = len;

		while (n > 0 && b[n - 1] == 0)
			n--;
		copy = malloc(n + 1);
		if (!copy) {
			errno = ENOMEM;
			return -1;
		}
		memcpy(copy, b, n);
		copy[n] = 0;
		rc = mbedtls_x509_crt_parse(&cacert, copy, n + 1);
		free(copy);
	} else
		rc = mbedtls_x509_crt_parse(&cacert, b, len);
	/* parse returns the number of certificates it could not parse
	 * when at least one did, and an error when none did */
	if (rc < 0 || cacert.version == 0) {
		note("x509_crt_parse", rc < 0 ? rc : 0);
		mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
		errno = EINVAL;
		return -1;
	}
	ca_loaded = 1;
	mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	return 0;
}

static int start_ssl(struct tlsfd *t)
{
	int rc;

	mbedtls_ssl_init(&t->ssl);
	t->have_ssl = 1;
	rc = mbedtls_ssl_setup(&t->ssl, &conf);
	if (rc) {
		note("ssl_setup", rc);
		return -1;
	}
	/* A name is checked; no name (a dotted-quad connection) checks
	 * the chain alone, which is what the board's kernel does, and
	 * mbedtls wants told so explicitly. */
	rc = mbedtls_ssl_set_hostname(&t->ssl, t->host[0] ? t->host : NULL);
	if (rc) {
		note("ssl_set_hostname", rc);
		return -1;
	}
	t->net.fd = t->fd;
	mbedtls_ssl_set_bio(&t->ssl, &t->net, mbedtls_net_send, mbedtls_net_recv, NULL);
	t->state = TLS_HANDSHAKE;
	return 0;
}

int pc3_tls_connect(int fd, const struct sockaddr *sa, socklen_t len)
{
	struct tlsfd *t = slot(fd);
	int rc;

	if (!t) {
		errno = EBADF;
		return -1;
	}
	if (t->state == TLS_TCP) {
		rc = connect(fd, sa, len);
		if (rc < 0 && errno != EISCONN)
			return -1;	/* EINPROGRESS, EALREADY, or a refusal */
		if (start_ssl(t) < 0) {
			errno = ECONNREFUSED;
			return -1;
		}
	}
	if (t->state == TLS_HANDSHAKE) {
		rc = mbedtls_ssl_handshake(&t->ssl);
		if (rc == 0) {
			t->state = TLS_UP;
			return 0;
		}
		if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
			errno = EINPROGRESS;
			return -1;
		}
		note("handshake", rc);
		if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && debug) {
			uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
			char v[512];
			mbedtls_x509_crt_verify_info(v, sizeof v, "pc3tls:   ", flags);
			fputs(v, stderr);
		}
		errno = ECONNREFUSED;
		return -1;
	}
	return 0;			/* already up */
}

long pc3_tls_read(int fd, void *buf, size_t n)
{
	struct tlsfd *t = slot(fd);
	int rc;

	if (!t) {
		errno = EBADF;
		return -1;
	}
	if (t->state != TLS_UP) {
		errno = ENOTCONN;
		return -1;
	}
	rc = mbedtls_ssl_read(&t->ssl, buf, n);
	if (rc > 0)
		return rc;
	if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
		errno = EAGAIN;
		return -1;
	}
	if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
		return 0;		/* the peer said goodbye: EOF */
	note("read", rc);
	errno = ECONNRESET;
	return -1;
}

long pc3_tls_write(int fd, const void *buf, size_t n)
{
	struct tlsfd *t = slot(fd);
	int rc;

	if (!t) {
		errno = EBADF;
		return -1;
	}
	if (t->state != TLS_UP) {
		errno = ENOTCONN;
		return -1;
	}
	rc = mbedtls_ssl_write(&t->ssl, buf, n);
	if (rc >= 0)
		return rc;
	if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
		errno = EAGAIN;
		return -1;
	}
	note("write", rc);
	errno = EPIPE;
	return -1;
}

int pc3_tls_close(int fd)
{
	struct tlsfd *t = slot(fd);

	if (!t) {
		errno = EBADF;
		return -1;
	}
	if (t->have_ssl) {
		if (t->state == TLS_UP)
			mbedtls_ssl_close_notify(&t->ssl);	/* best effort */
		mbedtls_ssl_free(&t->ssl);
	}
	t->fd = -1;
	t->have_ssl = 0;
	return close(fd);
}
