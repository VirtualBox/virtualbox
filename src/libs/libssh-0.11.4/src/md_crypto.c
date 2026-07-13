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

#include "libcrypto-compat.h"
#include "libssh/crypto.h"
#include "libssh/wrapper.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

SHACTX
sha1_init(void)
{
    int rc;
    SHACTX c = EVP_MD_CTX_new();
    if (c == NULL) {
        return NULL;
    }
    rc = EVP_DigestInit_ex(c, EVP_sha1(), NULL);
    if (rc == 0) {
        EVP_MD_CTX_free(c);
        c = NULL;
    }
    return c;
}

void
sha1_ctx_free(SHACTX c)
{
    EVP_MD_CTX_free(c);
}

int
sha1_update(SHACTX c, const void *data, size_t len)
{
    int rc = EVP_DigestUpdate(c, data, len);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha1_final(unsigned char *md, SHACTX c)
{
    unsigned int mdlen = 0;
    int rc = EVP_DigestFinal(c, md, &mdlen);

    EVP_MD_CTX_free(c);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha1(const unsigned char *digest, size_t len, unsigned char *hash)
{
    SHACTX c = sha1_init();
    int rc;

    if (c == NULL) {
        return SSH_ERROR;
    }
    rc = sha1_update(c, digest, len);
    if (rc != SSH_OK) {
        EVP_MD_CTX_free(c);
        return SSH_ERROR;
    }
    return sha1_final(hash, c);
}

SHA256CTX
sha256_init(void)
{
    int rc;
    SHA256CTX c = EVP_MD_CTX_new();
    if (c == NULL) {
        return NULL;
    }
    rc = EVP_DigestInit_ex(c, EVP_sha256(), NULL);
    if (rc == 0) {
        EVP_MD_CTX_free(c);
        c = NULL;
    }
    return c;
}

void
sha256_ctx_free(SHA256CTX c)
{
    EVP_MD_CTX_free(c);
}

int
sha256_update(SHA256CTX c, const void *data, size_t len)
{
    int rc = EVP_DigestUpdate(c, data, len);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha256_final(unsigned char *md, SHA256CTX c)
{
    unsigned int mdlen = 0;
    int rc = EVP_DigestFinal(c, md, &mdlen);

    EVP_MD_CTX_free(c);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha256(const unsigned char *digest, size_t len, unsigned char *hash)
{
    SHA256CTX c = sha256_init();
    int rc;

    if (c == NULL) {
        return SSH_ERROR;
    }
    rc = sha256_update(c, digest, len);
    if (rc != SSH_OK) {
        EVP_MD_CTX_free(c);
        return SSH_ERROR;
    }
    return sha256_final(hash, c);
}

SHA384CTX
sha384_init(void)
{
    int rc;
    SHA384CTX c = EVP_MD_CTX_new();
    if (c == NULL) {
        return NULL;
    }
    rc = EVP_DigestInit_ex(c, EVP_sha384(), NULL);
    if (rc == 0) {
        EVP_MD_CTX_free(c);
        c = NULL;
    }
    return c;
}

void
sha384_ctx_free(SHA384CTX c)
{
    EVP_MD_CTX_free(c);
}

int
sha384_update(SHA384CTX c, const void *data, size_t len)
{
    int rc = EVP_DigestUpdate(c, data, len);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha384_final(unsigned char *md, SHA384CTX c)
{
    unsigned int mdlen = 0;
    int rc = EVP_DigestFinal(c, md, &mdlen);

    EVP_MD_CTX_free(c);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha384(const unsigned char *digest, size_t len, unsigned char *hash)
{
    SHA384CTX c = sha384_init();
    int rc;

    if (c == NULL) {
        return SSH_ERROR;
    }
    rc = sha384_update(c, digest, len);
    if (rc != SSH_OK) {
        EVP_MD_CTX_free(c);
        return SSH_ERROR;
    }
    return sha384_final(hash, c);
}

SHA512CTX
sha512_init(void)
{
    int rc = 0;
    SHA512CTX c = EVP_MD_CTX_new();
    if (c == NULL) {
        return NULL;
    }
    rc = EVP_DigestInit_ex(c, EVP_sha512(), NULL);
    if (rc == 0) {
        EVP_MD_CTX_free(c);
        c = NULL;
    }
    return c;
}

void
sha512_ctx_free(SHA512CTX c)
{
    EVP_MD_CTX_free(c);
}

int
sha512_update(SHA512CTX c, const void *data, size_t len)
{
    int rc = EVP_DigestUpdate(c, data, len);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha512_final(unsigned char *md, SHA512CTX c)
{
    unsigned int mdlen = 0;
    int rc = EVP_DigestFinal(c, md, &mdlen);

    EVP_MD_CTX_free(c);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
sha512(const unsigned char *digest, size_t len, unsigned char *hash)
{
    SHA512CTX c = sha512_init();
    int rc;

    if (c == NULL) {
        return SSH_ERROR;
    }
    rc = sha512_update(c, digest, len);
    if (rc != SSH_OK) {
        EVP_MD_CTX_free(c);
        return SSH_ERROR;
    }
    return sha512_final(hash, c);
}

MD5CTX
md5_init(void)
{
    int rc;
    MD5CTX c = EVP_MD_CTX_new();
    if (c == NULL) {
        return NULL;
    }
    rc = EVP_DigestInit_ex(c, EVP_md5(), NULL);
    if (rc == 0) {
        EVP_MD_CTX_free(c);
        c = NULL;
    }
    return c;
}

void
md5_ctx_free(MD5CTX c)
{
    EVP_MD_CTX_free(c);
}

int
md5_update(MD5CTX c, const void *data, size_t len)
{
    int rc = EVP_DigestUpdate(c, data, len);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}

int
md5_final(unsigned char *md, MD5CTX c)
{
    unsigned int mdlen = 0;
    int rc = EVP_DigestFinal(c, md, &mdlen);

    EVP_MD_CTX_free(c);
    if (rc != 1) {
        return SSH_ERROR;
    }
    return SSH_OK;
}
