#ifndef LIBCRYPTO_COMPAT_H
#define LIBCRYPTO_COMPAT_H

#include <openssl/opensslv.h>

#define NISTP256 "P-256"
#define NISTP384 "P-384"
#define NISTP521 "P-521"

#if OPENSSL_VERSION_NUMBER < 0x30000000L
#define EVP_PKEY_eq EVP_PKEY_cmp
#endif /* OPENSSL_VERSION_NUMBER */

#endif /* LIBCRYPTO_COMPAT_H */
