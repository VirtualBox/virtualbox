/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2009 by Aris Adamantiadis
 * Copyright (C) 2016 g10 Code GmbH
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
#include "libssh/wrapper.h"

#include <gcrypt.h>

SHACTX
sha1_init(void)
{
    SHACTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA1, 0);

    return ctx;
}

int
sha1_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
    return SSH_OK;
}

void
sha1_ctx_free(SHACTX c)
{
    gcry_md_close(c);
}

int
sha1_final(unsigned char *md, SHACTX c)
{
    unsigned char *tmp = NULL;

    gcry_md_final(c);
    tmp = gcry_md_read(c, 0);
    if (tmp == NULL) {
        gcry_md_close(c);
        return SSH_ERROR;
    }
    memcpy(md, tmp, SHA_DIGEST_LEN);
    gcry_md_close(c);
    return SSH_OK;
}

int
sha1(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA1, hash, digest, len);
    return SSH_OK;
}

SHA256CTX
sha256_init(void)
{
    SHA256CTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA256, 0);

    return ctx;
}

void
sha256_ctx_free(SHA256CTX c)
{
    gcry_md_close(c);
}

int
sha256_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
    return SSH_OK;
}

int
sha256_final(unsigned char *md, SHACTX c)
{
    unsigned char *tmp = NULL;

    gcry_md_final(c);
    tmp = gcry_md_read(c, 0);
    if (tmp == NULL) {
        gcry_md_close(c);
        return SSH_ERROR;
    }
    memcpy(md, tmp, SHA256_DIGEST_LEN);
    gcry_md_close(c);
    return SSH_OK;
}

int
sha256(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA256, hash, digest, len);
    return SSH_OK;
}

SHA384CTX
sha384_init(void)
{
    SHA384CTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA384, 0);

    return ctx;
}

void
sha384_ctx_free(SHA384CTX c)
{
    gcry_md_close(c);
}

int
sha384_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
    return SSH_OK;
}

int
sha384_final(unsigned char *md, SHACTX c)
{
    unsigned char *tmp = NULL;

    gcry_md_final(c);
    tmp = gcry_md_read(c, 0);
    if (tmp == NULL) {
        gcry_md_close(c);
        return SSH_ERROR;
    }
    memcpy(md, tmp, SHA384_DIGEST_LEN);
    gcry_md_close(c);
    return SSH_OK;
}

int
sha384(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA384, hash, digest, len);
    return SSH_OK;
}

SHA512CTX
sha512_init(void)
{
    SHA512CTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA512, 0);

    return ctx;
}

void
sha512_ctx_free(SHA512CTX c)
{
    gcry_md_close(c);
}

int
sha512_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
    return SSH_OK;
}

int
sha512_final(unsigned char *md, SHACTX c)
{
    unsigned char *tmp = NULL;

    gcry_md_final(c);
    tmp = gcry_md_read(c, 0);
    if (tmp == NULL) {
        gcry_md_close(c);
        return SSH_ERROR;
    }
    memcpy(md, tmp, SHA512_DIGEST_LEN);
    gcry_md_close(c);
    return SSH_OK;
}

int
sha512(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA512, hash, digest, len);
    return SSH_OK;
}

MD5CTX
md5_init(void)
{
    MD5CTX c = NULL;
    gcry_md_open(&c, GCRY_MD_MD5, 0);

    return c;
}

void
md5_ctx_free(MD5CTX c)
{
    gcry_md_close(c);
}

int
md5_update(MD5CTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
    return SSH_OK;
}

int
md5_final(unsigned char *md, MD5CTX c)
{
    unsigned char *tmp = NULL;

    gcry_md_final(c);
    tmp = gcry_md_read(c, 0);
    if (tmp == NULL) {
        gcry_md_close(c);
        return SSH_ERROR;
    }
    memcpy(md, tmp, MD5_DIGEST_LEN);
    gcry_md_close(c);
    return SSH_OK;
}
