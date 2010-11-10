/*

	Minimal OpenSSL Helper
	Copyright (C) 2010 Fedor Kozhevnikov

	Licensed under GNU GPL v2 or later

*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdint.h>

#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>


#define _dprintf(args...)	while (0) {}


typedef struct {
	SSL* ssl;
	int sd;
} mssl_cookie_t;

static SSL_CTX* ctx;

static inline void mssl_cleanup(int err)
{
	if (err) ERR_print_errors_fp(stderr);
	SSL_CTX_free(ctx);
	ctx = NULL;
}

static ssize_t mssl_read(void *cookie, char *buf, size_t len)
{
	_dprintf("%s\n", __FUNCTION__);

	mssl_cookie_t *kuki = cookie;
	return SSL_read(kuki->ssl, buf, len);
}

static ssize_t mssl_write(void *cookie, const char *buf, size_t len)
{
	_dprintf("%s\n", __FUNCTION__);

	mssl_cookie_t *kuki = cookie;
	return SSL_write(kuki->ssl, buf, len);
}

static int mssl_seek(void *cookie, __offmax_t *pos, int whence)
{
	_dprintf("%s()\n", __FUNCTION__);
	errno = EIO;
	return -1;
}

static int mssl_close(void *cookie)
{
	_dprintf("%s()\n", __FUNCTION__);

	mssl_cookie_t *kuki = cookie;
	if (!kuki) return 0;

	if (kuki->ssl) {
		SSL_shutdown(kuki->ssl);
		SSL_free(kuki->ssl);
	}

	free(kuki);
	return 0;
}

static const cookie_io_functions_t mssl = {
	mssl_read, mssl_write, mssl_seek, mssl_close
};

static FILE *_ssl_fopen(int sd, int client)
{
	int r;
	mssl_cookie_t *kuki;
	FILE *f;

	_dprintf("%s()\n", __FUNCTION__);

	if ((kuki = calloc(1, sizeof(*kuki))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	kuki->sd = sd;

	if ((kuki->ssl = SSL_new(ctx)) == NULL) {
		_dprintf("%s: SSL_new failed\n", __FUNCTION__);
		goto ERROR;
	}

	SSL_set_fd(kuki->ssl, kuki->sd);
	r = client ? SSL_connect(kuki->ssl) : SSL_accept(kuki->ssl);
	if (r == -1) {
		ERR_print_errors_fp(stderr);
		goto ERROR;
	}

	_dprintf("SSL connection using %s\n", SSL_get_cipher(kuki->ssl));

	if ((f = fopencookie(kuki, "r+", mssl)) == NULL) {
		_dprintf("%s: fopencookie failed\n", __FUNCTION__);
		goto ERROR;
	}
	return f;

ERROR:
	mssl_close(kuki);
	return NULL;
}

FILE *ssl_server_fopen(int sd)
{
	_dprintf("%s()\n", __FUNCTION__);
	return _ssl_fopen(sd, 0);
}

FILE *ssl_client_fopen(int sd)
{
	_dprintf("%s()\n", __FUNCTION__);
	return _ssl_fopen(sd, 1);
}

int mssl_init(char *cert, char *priv)
{
	int client = (cert == NULL);

	_dprintf("%s()\n", __FUNCTION__);

	SSL_load_error_strings();
	SSLeay_add_ssl_algorithms();

	ctx = SSL_CTX_new(client ? SSLv2_client_method() : SSLv23_server_method());
	if (!ctx) {
		ERR_print_errors_fp(stderr);
		return 0;
	}

	if (cert) {
		_dprintf("SSL_CTX_use_certificate_file(%s)\n", cert);
		if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) <= 0) {
			_dprintf("SSL_CTX_use_certificate_file() failed\n");
			mssl_cleanup(1);
			return 0;
		}
	}
	if (cert && priv) {
		_dprintf("SSL_CTX_use_PrivateKey_file(%s)\n", priv);
		if (SSL_CTX_use_PrivateKey_file(ctx, priv, SSL_FILETYPE_PEM) <= 0) {
			_dprintf("SSL_CTX_use_PrivateKey_file() failed\n");
			mssl_cleanup(1);
			return 0;
		}
		if (!SSL_CTX_check_private_key(ctx)) {
			_dprintf("Private key does not match the certificate public key\n");
			mssl_cleanup(0);
			return 0;
		}
	}

	_dprintf("%s() success\n", __FUNCTION__);
	return 1;
}