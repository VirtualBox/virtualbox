/*
 * Copyright 2019-2021 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_OPENSSLCONF_H
# define OPENSSL_OPENSSLCONF_H
# pragma once

# include <openssl/configuration.h>
# include <openssl/macros.h>

/* Mangle OpenSSL symbols to prevent clashes with other OpenSSL libraries
 *  * (especially shared objects or dylibs). */
#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
# include "openssl-mangling.h"
#endif
#ifndef OPENSSL_MANGLER
# define OPENSSL_MANGLER(name) name
#endif

#endif  /* OPENSSL_OPENSSLCONF_H */
