/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2009 by Aris Adamantiadis
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"

#include "libssh/crypto.h"
#include <openssl/rand.h>

/**
 * @addtogroup libssh_misc
 *
 * @{
 */

/**
 * @brief Get random bytes
 *
 * Make sure to always check the return code of this function!
 *
 * @param[in]  where    The buffer to fill with random bytes
 *
 * @param[in]  len      The size of the buffer to fill.
 *
 * @param[in]  strong   Use a strong or private RNG source.
 *
 * @return 1 on success, 0 on error.
 */
int
ssh_get_random(void *where, int len, int strong)
{
#ifdef HAVE_OPENSSL_RAND_PRIV_BYTES
    if (strong) {
        /* Returns -1 when not supported, 0 on error, 1 on success */
        return !!RAND_priv_bytes(where, len);
    }
#else
    (void)strong;
#endif /* HAVE_RAND_PRIV_BYTES */

    /* Returns -1 when not supported, 0 on error, 1 on success */
    return !!RAND_bytes(where, len);
}

/**
 * @}
 */
