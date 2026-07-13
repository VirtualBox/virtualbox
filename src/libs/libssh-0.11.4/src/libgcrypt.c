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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/crypto.h"
#include "libssh/wrapper.h"
#include "libssh/string.h"
#include "libssh/misc.h"
#ifdef HAVE_GCRYPT_CHACHA_POLY
#include "libssh/chacha20-poly1305-common.h"
#endif

#ifdef HAVE_LIBGCRYPT
#include <gcrypt.h>

#ifdef HAVE_GCRYPT_CHACHA_POLY

struct chacha20_poly1305_keysched {
    bool initialized;
    /* cipher handle used for encrypting the packets */
    gcry_cipher_hd_t main_hd;
    /* cipher handle used for encrypting the length field */
    gcry_cipher_hd_t header_hd;
    /* mac handle used for authenticating the packets */
    gcry_mac_hd_t mac_hd;
};

static const uint8_t zero_block[CHACHA20_BLOCKSIZE] = {0};
#endif /* HAVE_GCRYPT_CHACHA_POLY */

static int libgcrypt_initialized = 0;

static int alloc_key(struct ssh_cipher_struct *cipher) {
    cipher->key = malloc(cipher->keylen);
    if (cipher->key == NULL) {
      return -1;
    }

    return 0;
}

void ssh_reseed(void){
}

int ssh_kdf(struct ssh_crypto_struct *crypto,
            unsigned char *key, size_t key_len,
            uint8_t key_type, unsigned char *output,
            size_t requested_len)
{
    return sshkdf_derive_key(crypto, key, key_len,
                             key_type, output, requested_len);
}

HMACCTX hmac_init(const void *key, size_t len, enum ssh_hmac_e type) {
  HMACCTX c = NULL;

  switch(type) {
    case SSH_HMAC_SHA1:
      gcry_md_open(&c, GCRY_MD_SHA1, GCRY_MD_FLAG_HMAC);
      break;
    case SSH_HMAC_SHA256:
      gcry_md_open(&c, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
      break;
    case SSH_HMAC_SHA512:
      gcry_md_open(&c, GCRY_MD_SHA512, GCRY_MD_FLAG_HMAC);
      break;
    case SSH_HMAC_MD5:
      gcry_md_open(&c, GCRY_MD_MD5, GCRY_MD_FLAG_HMAC);
      break;
    default:
      c = NULL;
  }

  gcry_md_setkey(c, key, len);

  return c;
}

int hmac_update(HMACCTX c, const void *data, size_t len) {
  gcry_md_write(c, data, len);
  return 1;
}

int hmac_final(HMACCTX c, unsigned char *hashmacbuf, size_t *len) {
  unsigned int tmp = gcry_md_get_algo_dlen(gcry_md_get_algo(c));
  *len = (size_t)tmp;
  memcpy(hashmacbuf, gcry_md_read(c, 0), *len);
  gcry_md_close(c);
  return 1;
}

#ifdef HAVE_BLOWFISH
/* the wrapper functions for blowfish */
static int blowfish_set_key(struct ssh_cipher_struct *cipher, void *key, void *IV){
  if (cipher->key == NULL) {
    if (alloc_key(cipher) < 0) {
      return -1;
    }

    if (gcry_cipher_open(&cipher->key[0], GCRY_CIPHER_BLOWFISH,
        GCRY_CIPHER_MODE_CBC, 0)) {
      SAFE_FREE(cipher->key);
      return -1;
    }
    if (gcry_cipher_setkey(cipher->key[0], key, 16)) {
      gcry_cipher_close(cipher->key[0]);
      SAFE_FREE(cipher->key);
      return -1;
    }
    if (gcry_cipher_setiv(cipher->key[0], IV, 8)) {
      gcry_cipher_close(cipher->key[0]);
      SAFE_FREE(cipher->key);
      return -1;
    }
  }

  return 0;
}

static void blowfish_encrypt(struct ssh_cipher_struct *cipher, void *in,
    void *out, size_t len) {
  gcry_cipher_encrypt(cipher->key[0], out, len, in, len);
}

static void blowfish_decrypt(struct ssh_cipher_struct *cipher, void *in,
    void *out, size_t len) {
  gcry_cipher_decrypt(cipher->key[0], out, len, in, len);
}
#endif /* HAVE_BLOWFISH */

static int aes_set_key(struct ssh_cipher_struct *cipher, void *key, void *IV) {
  int mode=GCRY_CIPHER_MODE_CBC;
  if (cipher->key == NULL) {
    if (alloc_key(cipher) < 0) {
      return -1;
    }
    if(strstr(cipher->name,"-ctr"))
      mode=GCRY_CIPHER_MODE_CTR;
    if (strstr(cipher->name, "-gcm"))
      mode = GCRY_CIPHER_MODE_GCM;
    switch (cipher->keysize) {
      case 128:
        if (gcry_cipher_open(&cipher->key[0], GCRY_CIPHER_AES128,
              mode, 0)) {
          SAFE_FREE(cipher->key);
          return -1;
        }
        break;
      case 192:
        if (gcry_cipher_open(&cipher->key[0], GCRY_CIPHER_AES192,
              mode, 0)) {
          SAFE_FREE(cipher->key);
          return -1;
        }
        break;
      case 256:
        if (gcry_cipher_open(&cipher->key[0], GCRY_CIPHER_AES256,
              mode, 0)) {
          SAFE_FREE(cipher->key);
          return -1;
        }
        break;
      default:
        SSH_LOG(SSH_LOG_TRACE, "Unsupported key length %u.", cipher->keysize);
        SAFE_FREE(cipher->key);
        return -1;
    }
    if (gcry_cipher_setkey(cipher->key[0], key, cipher->keysize / 8)) {
      gcry_cipher_close(cipher->key[0]);
      SAFE_FREE(cipher->key);
      return -1;
    }
    if(mode == GCRY_CIPHER_MODE_CBC){
      if (gcry_cipher_setiv(cipher->key[0], IV, 16)) {
        gcry_cipher_close(cipher->key[0]);
        SAFE_FREE(cipher->key);
        return -1;
      }
    } else if (mode == GCRY_CIPHER_MODE_GCM) {
      /* Store the IV so we can handle the packet counter increments later
       * The IV is passed to the cipher context later.
       */
      memcpy(cipher->last_iv, IV, AES_GCM_IVLEN);
    } else {
      if(gcry_cipher_setctr(cipher->key[0],IV,16)){
        gcry_cipher_close(cipher->key[0]);
        SAFE_FREE(cipher->key);
        return -1;
      }
    }
  }

  return 0;
}

static void aes_encrypt(struct ssh_cipher_struct *cipher,
                        void *in,
                        void *out,
                        size_t len)
{
    gcry_cipher_encrypt(cipher->key[0], out, len, in, len);
}

static void aes_decrypt(struct ssh_cipher_struct *cipher,
                        void *in,
                        void *out,
                        size_t len)
{
    gcry_cipher_decrypt(cipher->key[0], out, len, in, len);
}

static int
aes_aead_get_length(struct ssh_cipher_struct *cipher,
                    void *in,
                    uint8_t *out,
                    size_t len,
                    uint64_t seq)
{
    (void)cipher;
    (void)seq;

    /* The length is not encrypted: Copy it to the result buffer */
    memcpy(out, in, len);

    return SSH_OK;
}

static void
aes_gcm_encrypt(struct ssh_cipher_struct *cipher,
                void *in,
                void *out,
                size_t len,
                uint8_t *tag,
                uint64_t seq)
{
    gpg_error_t err;
    size_t aadlen, authlen;

    (void)seq;

    aadlen = cipher->lenfield_blocksize;
    authlen = cipher->tag_size;

    /* increment IV */
    err = gcry_cipher_setiv(cipher->key[0],
                            cipher->last_iv,
                            AES_GCM_IVLEN);
    /* This actually does not increment the packet counter for the
     * current encryption operation, but for the next one. The first
     * operation needs to be completed with the derived IV.
     *
     * The IV buffer has the following structure:
     * [ 4B static IV ][ 8B packet counter ][ 4B block counter ]
     */
    uint64_inc(cipher->last_iv + 4);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setiv failed: %s",
                gpg_strerror(err));
        return;
    }

    /* Pass the authenticated data (packet_length) */
    err = gcry_cipher_authenticate(cipher->key[0], in, aadlen);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_authenticate failed: %s",
                gpg_strerror(err));
        return;
    }
    memcpy(out, in, aadlen);

    /* Encrypt the rest of the data */
    err = gcry_cipher_encrypt(cipher->key[0],
                              (unsigned char *)out + aadlen,
                              len - aadlen,
                              (unsigned char *)in + aadlen,
                              len - aadlen);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_encrypt failed: %s",
                gpg_strerror(err));
        return;
    }

    /* Calculate the tag */
    err = gcry_cipher_gettag(cipher->key[0],
                             (void *)tag,
                             authlen);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_gettag failed: %s",
                gpg_strerror(err));
        return;
    }
}

static int
aes_gcm_decrypt(struct ssh_cipher_struct *cipher,
                void *complete_packet,
                uint8_t *out,
                size_t encrypted_size,
                uint64_t seq)
{
    gpg_error_t err;
    size_t aadlen, authlen;

    (void)seq;

    aadlen = cipher->lenfield_blocksize;
    authlen = cipher->tag_size;

    /* increment IV */
    err = gcry_cipher_setiv(cipher->key[0],
                            cipher->last_iv,
                            AES_GCM_IVLEN);
    /* This actually does not increment the packet counter for the
     * current encryption operation, but for the next one. The first
     * operation needs to be completed with the derived IV.
     *
     * The IV buffer has the following structure:
     * [ 4B static IV ][ 8B packet counter ][ 4B block counter ]
     */
    uint64_inc(cipher->last_iv + 4);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setiv failed: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }

    /* Pass the authenticated data (packet_length) */
    err = gcry_cipher_authenticate(cipher->key[0],
                                   complete_packet,
                                   aadlen);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_authenticate failed: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }
    /* Do not copy the length to the target buffer, because it is already processed */
    //memcpy(out, complete_packet, aadlen);

    /* Encrypt the rest of the data */
    err = gcry_cipher_decrypt(cipher->key[0],
                              out,
                              encrypted_size,
                              (unsigned char *)complete_packet + aadlen,
                              encrypted_size);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_decrypt failed: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }

    /* Check the tag */
    err = gcry_cipher_checktag(cipher->key[0],
                               (unsigned char *)complete_packet + aadlen + encrypted_size,
                               authlen);
    if (gpg_err_code(err) == GPG_ERR_CHECKSUM) {
        SSH_LOG(SSH_LOG_DEBUG, "The authentication tag does not match");
        return SSH_ERROR;
    } else if (err != GPG_ERR_NO_ERROR) {
        SSH_LOG(SSH_LOG_TRACE, "General error while decryption: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }
    return SSH_OK;
}

static int des3_set_key(struct ssh_cipher_struct *cipher, void *key, void *IV) {
  if (cipher->key == NULL) {
    if (alloc_key(cipher) < 0) {
      return -1;
    }
    if (gcry_cipher_open(&cipher->key[0], GCRY_CIPHER_3DES,
          GCRY_CIPHER_MODE_CBC, 0)) {
      SAFE_FREE(cipher->key);
      return -1;
    }
    if (gcry_cipher_setkey(cipher->key[0], key, 24)) {
      gcry_cipher_close(cipher->key[0]);
      SAFE_FREE(cipher->key);
      return -1;
    }
    if (gcry_cipher_setiv(cipher->key[0], IV, 8)) {
      gcry_cipher_close(cipher->key[0]);
      SAFE_FREE(cipher->key);
      return -1;
    }
  }

  return 0;
}

static void des3_encrypt(struct ssh_cipher_struct *cipher, void *in,
    void *out, size_t len) {
  gcry_cipher_encrypt(cipher->key[0], out, len, in, len);
}

static void des3_decrypt(struct ssh_cipher_struct *cipher, void *in,
    void *out, size_t len) {
  gcry_cipher_decrypt(cipher->key[0], out, len, in, len);
}

#ifdef HAVE_GCRYPT_CHACHA_POLY
static void chacha20_cleanup(struct ssh_cipher_struct *cipher)
{
    struct chacha20_poly1305_keysched *ctx = NULL;

    if (cipher->chacha20_schedule == NULL) {
        return;
    }

    ctx = cipher->chacha20_schedule;

    if (ctx->initialized) {
        gcry_cipher_close(ctx->main_hd);
        gcry_cipher_close(ctx->header_hd);
        gcry_mac_close(ctx->mac_hd);
        ctx->initialized = false;
    }

    SAFE_FREE(cipher->chacha20_schedule);
}

static int chacha20_set_encrypt_key(struct ssh_cipher_struct *cipher,
                                    void *key,
                                    UNUSED_PARAM(void *IV))
{
    struct chacha20_poly1305_keysched *ctx = NULL;
    uint8_t *u8key = key;
    gpg_error_t err;

    if (cipher->chacha20_schedule == NULL) {
        ctx = calloc(1, sizeof(*ctx));
        if (ctx == NULL) {
            return -1;
        }
        cipher->chacha20_schedule = ctx;
    } else {
        ctx = cipher->chacha20_schedule;
    }

    if (!ctx->initialized) {
        /* Open cipher/mac handles. */
        err = gcry_cipher_open(&ctx->main_hd, GCRY_CIPHER_CHACHA20,
                               GCRY_CIPHER_MODE_STREAM, 0);
        if (err != 0) {
            SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_open failed: %s",
                    gpg_strerror(err));
            SAFE_FREE(cipher->chacha20_schedule);
            return -1;
        }
        err = gcry_cipher_open(&ctx->header_hd, GCRY_CIPHER_CHACHA20,
                               GCRY_CIPHER_MODE_STREAM, 0);
        if (err != 0) {
            SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_open failed: %s",
                    gpg_strerror(err));
            gcry_cipher_close(ctx->main_hd);
            SAFE_FREE(cipher->chacha20_schedule);
            return -1;
        }
        err = gcry_mac_open(&ctx->mac_hd, GCRY_MAC_POLY1305, 0, NULL);
        if (err != 0) {
            SSH_LOG(SSH_LOG_TRACE, "gcry_mac_open failed: %s",
                    gpg_strerror(err));
            gcry_cipher_close(ctx->main_hd);
            gcry_cipher_close(ctx->header_hd);
            SAFE_FREE(cipher->chacha20_schedule);
            return -1;
        }

        ctx->initialized = true;
    }

    err = gcry_cipher_setkey(ctx->main_hd, u8key, CHACHA20_KEYLEN);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setkey failed: %s",
                gpg_strerror(err));
        chacha20_cleanup(cipher);
        return -1;
    }

    err = gcry_cipher_setkey(ctx->header_hd, u8key + CHACHA20_KEYLEN,
                             CHACHA20_KEYLEN);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setkey failed: %s",
                gpg_strerror(err));
        chacha20_cleanup(cipher);
        return -1;
    }

    return 0;
}

static void chacha20_poly1305_aead_encrypt(struct ssh_cipher_struct *cipher,
                                           void *in,
                                           void *out,
                                           size_t len,
                                           uint8_t *tag,
                                           uint64_t seq)
{
    struct ssh_packet_header *in_packet = in, *out_packet = out;
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    uint8_t poly_key[CHACHA20_BLOCKSIZE];
    size_t taglen = POLY1305_TAGLEN;
    gpg_error_t err;

    seq = htonll(seq);

    /* step 1, prepare the poly1305 key */
    err = gcry_cipher_setiv(ctx->main_hd, (uint8_t *)&seq, sizeof(seq));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setiv failed: %s",
                gpg_strerror(err));
        goto out;
    }
    /* Output full ChaCha block so that counter increases by one for
     * payload encryption step. */
    err = gcry_cipher_encrypt(ctx->main_hd,
                              poly_key,
                              sizeof(poly_key),
                              zero_block,
                              sizeof(zero_block));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_encrypt failed: %s",
                gpg_strerror(err));
        goto out;
    }
    err = gcry_mac_setkey(ctx->mac_hd, poly_key, POLY1305_KEYLEN);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_mac_setkey failed: %s",
                gpg_strerror(err));
        goto out;
    }

    /* step 2, encrypt length field */
    err = gcry_cipher_setiv(ctx->header_hd, (uint8_t *)&seq, sizeof(seq));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setiv failed: %s",
                gpg_strerror(err));
        goto out;
    }
    err = gcry_cipher_encrypt(ctx->header_hd,
                              (uint8_t *)&out_packet->length,
                              sizeof(uint32_t),
                              (uint8_t *)&in_packet->length,
                              sizeof(uint32_t));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_encrypt failed: %s",
                gpg_strerror(err));
        goto out;
    }

    /* step 3, encrypt packet payload (main_hd counter == 1) */
    err = gcry_cipher_encrypt(ctx->main_hd,
                              out_packet->payload,
                              len - sizeof(uint32_t),
                              in_packet->payload,
                              len - sizeof(uint32_t));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_encrypt failed: %s",
                gpg_strerror(err));
        goto out;
    }

    /* step 4, compute the MAC */
    err = gcry_mac_write(ctx->mac_hd, (uint8_t *)out_packet, len);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_mac_write failed: %s",
                gpg_strerror(err));
        goto out;
    }
    err = gcry_mac_read(ctx->mac_hd, tag, &taglen);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_mac_read failed: %s",
                gpg_strerror(err));
        goto out;
    }

out:
    explicit_bzero(poly_key, sizeof(poly_key));
}

static int chacha20_poly1305_aead_decrypt_length(
        struct ssh_cipher_struct *cipher,
        void *in,
        uint8_t *out,
        size_t len,
        uint64_t seq)
{
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    gpg_error_t err;

    if (len < sizeof(uint32_t)) {
        return SSH_ERROR;
    }
    seq = htonll(seq);

    err = gcry_cipher_setiv(ctx->header_hd, (uint8_t *)&seq, sizeof(seq));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setiv failed: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }
    err = gcry_cipher_decrypt(ctx->header_hd,
                              out,
                              sizeof(uint32_t),
                              in,
                              sizeof(uint32_t));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_decrypt failed: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }

    return SSH_OK;
}

static int chacha20_poly1305_aead_decrypt(struct ssh_cipher_struct *cipher,
                                          void *complete_packet,
                                          uint8_t *out,
                                          size_t encrypted_size,
                                          uint64_t seq)
{
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    uint8_t *mac = (uint8_t *)complete_packet + sizeof(uint32_t) +
                   encrypted_size;
    uint8_t poly_key[CHACHA20_BLOCKSIZE];
    int ret = SSH_ERROR;
    gpg_error_t err;

    seq = htonll(seq);

    /* step 1, prepare the poly1305 key */
    err = gcry_cipher_setiv(ctx->main_hd, (uint8_t *)&seq, sizeof(seq));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_setiv failed: %s",
                gpg_strerror(err));
        goto out;
    }
    /* Output full ChaCha block so that counter increases by one for
     * decryption step. */
    err = gcry_cipher_encrypt(ctx->main_hd,
                              poly_key,
                              sizeof(poly_key),
                              zero_block,
                              sizeof(zero_block));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_encrypt failed: %s",
                gpg_strerror(err));
        goto out;
    }
    err = gcry_mac_setkey(ctx->mac_hd, poly_key, POLY1305_KEYLEN);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_mac_setkey failed: %s",
                gpg_strerror(err));
        goto out;
    }

    /* step 2, check MAC */
    err = gcry_mac_write(ctx->mac_hd, (uint8_t *)complete_packet,
                         encrypted_size + sizeof(uint32_t));
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_mac_write failed: %s",
                gpg_strerror(err));
        goto out;
    }
    err = gcry_mac_verify(ctx->mac_hd, mac, POLY1305_TAGLEN);
    if (gpg_err_code(err) == GPG_ERR_CHECKSUM) {
        SSH_LOG(SSH_LOG_PACKET, "poly1305 verify error");
        goto out;
    } else if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_mac_verify failed: %s",
                gpg_strerror(err));
        goto out;
    }

    /* step 3, decrypt packet payload (main_hd counter == 1) */
    err = gcry_cipher_decrypt(ctx->main_hd,
                              out,
                              encrypted_size,
                              (uint8_t *)complete_packet + sizeof(uint32_t),
                              encrypted_size);
    if (err != 0) {
        SSH_LOG(SSH_LOG_TRACE, "gcry_cipher_decrypt failed: %s",
                gpg_strerror(err));
        goto out;
    }

    ret = SSH_OK;

out:
    explicit_bzero(poly_key, sizeof(poly_key));
    return ret;
}
#endif /* HAVE_GCRYPT_CHACHA_POLY */

#ifdef WITH_INSECURE_NONE
static void
none_crypt(UNUSED_PARAM(struct ssh_cipher_struct *cipher),
           void *in,
           void *out,
           size_t len)
{
    memcpy(out, in, len);
}
#endif /* WITH_INSECURE_NONE */

/* the table of supported ciphers */
static struct ssh_cipher_struct ssh_ciphertab[] = {
#ifdef HAVE_BLOWFISH
  {
    .name            = "blowfish-cbc",
    .blocksize       = 8,
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 128,
    .set_encrypt_key = blowfish_set_key,
    .set_decrypt_key = blowfish_set_key,
    .encrypt     = blowfish_encrypt,
    .decrypt     = blowfish_decrypt
  },
#endif /* HAVE_BLOWFISH */
  {
    .name            = "aes128-ctr",
    .blocksize       = 16,
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 128,
    .set_encrypt_key = aes_set_key,
    .set_decrypt_key = aes_set_key,
    .encrypt     = aes_encrypt,
    .decrypt     = aes_encrypt
  },
  {
      .name            = "aes192-ctr",
      .blocksize       = 16,
      .keylen          = sizeof(gcry_cipher_hd_t),
      .key             = NULL,
      .keysize         = 192,
      .set_encrypt_key = aes_set_key,
      .set_decrypt_key = aes_set_key,
      .encrypt     = aes_encrypt,
      .decrypt     = aes_encrypt
  },
  {
      .name            = "aes256-ctr",
      .blocksize       = 16,
      .keylen          = sizeof(gcry_cipher_hd_t),
      .key             = NULL,
      .keysize         = 256,
      .set_encrypt_key = aes_set_key,
      .set_decrypt_key = aes_set_key,
      .encrypt     = aes_encrypt,
      .decrypt     = aes_encrypt
  },
  {
    .name            = "aes128-cbc",
    .blocksize       = 16,
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 128,
    .set_encrypt_key = aes_set_key,
    .set_decrypt_key = aes_set_key,
    .encrypt     = aes_encrypt,
    .decrypt     = aes_decrypt
  },
  {
    .name            = "aes192-cbc",
    .blocksize       = 16,
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 192,
    .set_encrypt_key = aes_set_key,
    .set_decrypt_key = aes_set_key,
    .encrypt     = aes_encrypt,
    .decrypt     = aes_decrypt
  },
  {
    .name            = "aes256-cbc",
    .blocksize       = 16,
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 256,
    .set_encrypt_key = aes_set_key,
    .set_decrypt_key = aes_set_key,
    .encrypt     = aes_encrypt,
    .decrypt     = aes_decrypt
  },
  {
    .name            = "aes128-gcm@openssh.com",
    .blocksize       = 16,
    .lenfield_blocksize = 4, /* not encrypted, but authenticated */
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 128,
    .tag_size        = AES_GCM_TAGLEN,
    .set_encrypt_key = aes_set_key,
    .set_decrypt_key = aes_set_key,
    .aead_encrypt    = aes_gcm_encrypt,
    .aead_decrypt_length = aes_aead_get_length,
    .aead_decrypt    = aes_gcm_decrypt,
  },
  {
    .name            = "aes256-gcm@openssh.com",
    .blocksize       = 16,
    .lenfield_blocksize = 4, /* not encrypted, but authenticated */
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 256,
    .tag_size        = AES_GCM_TAGLEN,
    .set_encrypt_key = aes_set_key,
    .set_decrypt_key = aes_set_key,
    .aead_encrypt    = aes_gcm_encrypt,
    .aead_decrypt_length = aes_aead_get_length,
    .aead_decrypt    = aes_gcm_decrypt,
  },
  {
    .name            = "3des-cbc",
    .blocksize       = 8,
    .keylen          = sizeof(gcry_cipher_hd_t),
    .key             = NULL,
    .keysize         = 192,
    .set_encrypt_key = des3_set_key,
    .set_decrypt_key = des3_set_key,
    .encrypt     = des3_encrypt,
    .decrypt     = des3_decrypt
  },
  {
#ifdef HAVE_GCRYPT_CHACHA_POLY
    .ciphertype      = SSH_AEAD_CHACHA20_POLY1305,
    .name            = "chacha20-poly1305@openssh.com",
    .blocksize       = 8,
    .lenfield_blocksize = 4,
    .keylen          = sizeof(struct chacha20_poly1305_keysched),
    .keysize         = 2 * CHACHA20_KEYLEN * 8,
    .tag_size        = POLY1305_TAGLEN,
    .set_encrypt_key = chacha20_set_encrypt_key,
    .set_decrypt_key = chacha20_set_encrypt_key,
    .aead_encrypt    = chacha20_poly1305_aead_encrypt,
    .aead_decrypt_length = chacha20_poly1305_aead_decrypt_length,
    .aead_decrypt    = chacha20_poly1305_aead_decrypt,
    .cleanup         = chacha20_cleanup
#else
    .name = "chacha20-poly1305@openssh.com"
#endif
  },
#ifdef WITH_INSECURE_NONE
  {
    .name            = "none",
    .blocksize       = 8,
    .keysize         = 0,
    .encrypt         = none_crypt,
    .decrypt         = none_crypt
  },
#endif /* WITH_INSECURE_NONE */
  {
    .name            = NULL,
    .blocksize       = 0,
    .keylen          = 0,
    .key             = NULL,
    .keysize         = 0,
    .set_encrypt_key = NULL,
    .set_decrypt_key = NULL,
    .encrypt     = NULL,
    .decrypt     = NULL
  }
};

struct ssh_cipher_struct *ssh_get_ciphertab(void)
{
  return ssh_ciphertab;
}

/*
 * Extract an MPI from the given s-expression SEXP named NAME which is
 * encoded using INFORMAT and store it in a newly allocated ssh_string
 * encoded using OUTFORMAT.
 */
ssh_string ssh_sexp_extract_mpi(const gcry_sexp_t sexp,
                                const char *name,
                                enum gcry_mpi_format informat,
                                enum gcry_mpi_format outformat)
{
    gpg_error_t err;
    ssh_string result = NULL;
    gcry_sexp_t fragment = NULL;
    gcry_mpi_t mpi = NULL;
    size_t size;

    fragment = gcry_sexp_find_token(sexp, name, 0);
    if (fragment == NULL) {
        goto fail;
    }

    mpi = gcry_sexp_nth_mpi(fragment, 1, informat);
    if (mpi == NULL) {
        goto fail;
    }

    err = gcry_mpi_print(outformat, NULL, 0, &size, mpi);
    if (err != 0) {
        goto fail;
    }

    result = ssh_string_new(size);
    if (result == NULL) {
        goto fail;
    }

    err = gcry_mpi_print(outformat, ssh_string_data(result), size, NULL, mpi);
    if (err != 0) {
        ssh_string_burn(result);
        SSH_STRING_FREE(result);
        result = NULL;
        goto fail;
    }

fail:
    gcry_sexp_release(fragment);
    gcry_mpi_release(mpi);
    return result;
}


/**
 * @internal
 *
 * @brief Initialize libgcrypt's subsystem
 */
int ssh_crypto_init(void)
{
    UNUSED_VAR(size_t i);

    if (libgcrypt_initialized) {
        return SSH_OK;
    }

    gcry_check_version(NULL);

    /* While the secure memory is not set up */
    gcry_control (GCRYCTL_SUSPEND_SECMEM_WARN);

    if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P, 0)) {
        gcry_control(GCRYCTL_INIT_SECMEM, 4096);
        gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
    }

    /* Re-enable warning */
    gcry_control (GCRYCTL_RESUME_SECMEM_WARN);

#ifndef HAVE_GCRYPT_CHACHA_POLY
    for (i = 0; ssh_ciphertab[i].name != NULL; i++) {
        int cmp;
        cmp = strcmp(ssh_ciphertab[i].name, "chacha20-poly1305@openssh.com");
        if (cmp == 0) {
            memcpy(&ssh_ciphertab[i],
                   ssh_get_chacha20poly1305_cipher(),
                   sizeof(struct ssh_cipher_struct));
            break;
        }
    }
#endif

    libgcrypt_initialized = 1;

    return SSH_OK;
}

/**
 * @internal
 *
 * @brief Finalize libgcrypt's subsystem
 */
void ssh_crypto_finalize(void)
{
    if (!libgcrypt_initialized) {
        return;
    }

    gcry_control(GCRYCTL_TERM_SECMEM);

    libgcrypt_initialized = 0;
}

#endif
