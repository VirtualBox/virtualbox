#include "config.h"

#define LIBSSH_STATIC

#include "torture.h"
#include "libssh/crypto.h"
#include "libssh/chacha20-poly1305-common.h"

uint8_t key[32] =
    "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"
    "\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d"
    "\x1e\x1f";

uint8_t IV[16] =
    "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e"
    "\x1f";

uint8_t cleartext[144] =
    "\xb4\xfc\x5d\xc2\x49\x8d\x2c\x29\x4a\xc9\x9a\xb0\x1b\xf8\x29"
    "\xee\x85\x6d\x8c\x04\x34\x7c\x65\xf4\x89\x97\xc5\x71\x70\x41"
    "\x91\x40\x19\x60\xe1\xf1\x8f\x4d\x8c\x17\x51\xd6\xbc\x69\x6e"
    "\xf2\x21\x87\x18\x6c\xef\xc4\xf4\xd9\xe6\x1b\x94\xf7\xd8\xb2"
    "\xe9\x24\xb9\xe7\xe6\x19\xf5\xec\x55\x80\x9a\xc8\x7d\x70\xa3"
    "\x50\xf8\x03\x10\x35\x49\x9b\x53\x58\xd7\x4c\xfc\x5f\x02\xd6"
    "\x28\xea\xcc\x43\xee\x5e\x2b\x8a\x7a\x66\xf7\x00\xee\x09\x18"
    "\x30\x1b\x47\xa2\x16\x69\xc4\x6e\x44\x3f\xbd\xec\x52\xce\xe5"
    "\x41\xf2\xe0\x04\x4f\x5a\x55\x58\x37\xba\x45\x8d\x15\x53\xf6"
    "\x31\x91\x13\x8c\x51\xed\x08\x07\xdb";

uint8_t aes256_cbc_encrypted[144] =
    "\x7f\x1b\x92\xac\xc5\x16\x05\x55\x74\xac\xb4\xe0\x91\x8c\xf8"
    "\x0d\xa9\x72\xa5\x09\xb8\x44\xee\x55\x02\x13\xb7\x52\x0a\xf0"
    "\xac\xd0\x21\x0e\x58\x7b\x34\xfe\xdb\x36\x01\x60\x7d\x18\x3a"
    "\xa9\x15\x18\x5b\x13\xca\xdd\x77\x7d\xdf\x64\xc6\xd5\x75\x4b"
    "\x02\x02\x37\xb1\xf4\x33\xff\x93\xe6\x32\x08\xda\xcb\x5d\xa2"
    "\x8f\x17\x1f\x99\x92\x60\x22\x9d\x6b\xe6\xb2\x5e\xb0\x5d\x26"
    "\x3f\xde\xb8\xc1\xb0\x70\x80\x1c\x00\xd0\x93\x2b\xeb\x0f\xd7"
    "\x70\x7a\x9a\x7a\xa6\x21\x23\x2c\x02\xb7\xcd\x88\x10\x9c\x2d"
    "\x0c\xd3\xfa\xc1\x33\x5b\xe1\xa1\xd4\x3d\x8f\xb8\x50\xc5\xb5"
    "\x72\xdd\x6d\x32\x1f\x58\x00\x48\xbe";

static int get_cipher(struct ssh_cipher_struct *cipher, const char *ciphername)
{
    struct ssh_cipher_struct *ciphers = ssh_get_ciphertab();
    size_t i;
    int cmp;

    assert_non_null(cipher);

    for (i = 0; ciphers[i].name != NULL; i++) {
        cmp = strcmp(ciphername, ciphers[i].name);
        if (cmp == 0){
            memcpy(cipher, &ciphers[i], sizeof(*cipher));
            return SSH_OK;
        }
    }

    return SSH_ERROR;
}

static void torture_crypto_aes256_cbc(void **state)
{
    uint8_t output[sizeof(cleartext)] = {0};
    uint8_t iv[16] = {0};
    struct ssh_cipher_struct cipher = {0};
    int rc;
    (void)state;

    rc = get_cipher(&cipher, "aes256-cbc");
    assert_int_equal(rc, SSH_OK);

    assert_non_null(cipher.set_encrypt_key);
    assert_non_null(cipher.encrypt);

    memcpy(iv, IV, sizeof(IV));
    cipher.set_encrypt_key(&cipher,
            key,
            iv
    );

    cipher.encrypt(&cipher,
            cleartext,
            output,
            sizeof(cleartext)
            );

    assert_memory_equal(output, aes256_cbc_encrypted, sizeof(aes256_cbc_encrypted));
    ssh_cipher_clear(&cipher);

    rc = get_cipher(&cipher, "aes256-cbc");
    assert_int_equal(rc, SSH_OK);

    assert_non_null(cipher.set_decrypt_key);
    assert_non_null(cipher.decrypt);

    memcpy(iv, IV, sizeof(IV));
    cipher.set_decrypt_key(&cipher,
            key,
            iv
    );

    memset(output, '\0', sizeof(output));
    cipher.decrypt(&cipher,
            aes256_cbc_encrypted,
            output,
            sizeof(aes256_cbc_encrypted)
            );

    assert_memory_equal(output, cleartext, sizeof(cleartext));

    ssh_cipher_clear(&cipher);
}

uint8_t chacha20poly1305_key[CHACHA20_KEYLEN*2] =
    "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"
    "\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d"
    "\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c"
    "\x2d\x2e\x2f\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b"
    "\x3c\x3d\x3e\x3f";

#define CLEARTEXT_LENGTH 144
uint8_t chacha20poly1305_cleartext[CLEARTEXT_LENGTH] =
    "\xb4\xfc\x5d\xc2\x49\x8d\x2c\x29\x4a\xc9\x9a\xb0\x1b\xf8\x29"
    "\xee\x85\x6d\x8c\x04\x34\x7c\x65\xf4\x89\x97\xc5\x71\x70\x41"
    "\x91\x40\x19\x60\xe1\xf1\x8f\x4d\x8c\x17\x51\xd6\xbc\x69\x6e"
    "\xf2\x21\x87\x18\x6c\xef\xc4\xf4\xd9\xe6\x1b\x94\xf7\xd8\xb2"
    "\xe9\x24\xb9\xe7\xe6\x19\xf5\xec\x55\x80\x9a\xc8\x7d\x70\xa3"
    "\x50\xf8\x03\x10\x35\x49\x9b\x53\x58\xd7\x4c\xfc\x5f\x02\xd6"
    "\x28\xea\xcc\x43\xee\x5e\x2b\x8a\x7a\x66\xf7\x00\xee\x09\x18"
    "\x30\x1b\x47\xa2\x16\x69\xc4\x6e\x44\x3f\xbd\xec\x52\xce\xe5"
    "\x41\xf2\xe0\x04\x4f\x5a\x55\x58\x37\xba\x45\x8d\x15\x53\xf6"
    "\x31\x91\x13\x8c\x51\xed\x08\x07\xdb";

uint64_t chacha20poly1305_seq = (uint64_t)1234567890 * 98765431;

uint8_t chacha20poly1305_encrypted[sizeof(uint32_t) + CLEARTEXT_LENGTH + POLY1305_TAGLEN] =
    "\xac\x2e\x4c\x54\xf6\x97\x75\xb4\x3b\x8f\xb0\x8e\xb0\x0a\x8e"
    "\xb3\x90\x21\x0d\x7a\xb6\xd3\x03\xf6\xbc\x6e\x3a\x32\x67\xe1"
    "\x13\x65\x43\x3b\x34\x9d\xcb\x62\x7e\x0a\x80\xb0\x45\x87\x07"
    "\x85\x49\x8d\x23\x5f\xac\x9c\x8b\xa8\xd5\x01\x12\xfe\x52\xc6"
    "\x99\xb4\xf2\xde\x12\x78\x79\xea\x1c\x5f\x45\xcd\xf7\xe4\xa0"
    "\x66\x15\x7f\xe3\xf4\x73\x3b\xe0\x52\xac\x2a\x00\x73\xd0\xd7"
    "\x95\xa9\xb9\x3a\xe0\x50\x13\xf4\xdc\xfc\x2a\x64\xb5\xcf\x29"
    "\x88\xef\x4c\x56\x10\x30\x28\xbb\x59\xb8\x23\x58\xab\x01\xa2"
    "\xab\x6b\xdd\xee\x20\x43\xe1\xec\x7a\xe1\xaa\x8b\x60\x19\xde"
    "\x3a\xd1\xd6\x80\x49\x7d\x5c\x81\xb8\x96\xad\x62\x32\xe5\x25"
    "\x72\xe9\x63\x96\xa1\x44\x25\x91\xe1\xdc\x01\xc7\x5c\xa9";

static void torture_crypto_chacha20poly1305(void **state)
{
    uint8_t input[sizeof(uint32_t) + sizeof(chacha20poly1305_cleartext)];
    uint8_t output[sizeof(input) + POLY1305_TAGLEN] = {0};
    uint8_t *outtag = output + sizeof(input);
    struct ssh_cipher_struct cipher = {0};
    uint32_t in_length;
    int rc;
    (void)state;

    /* Chacha20-poly1305 is not FIPS-allowed cipher */
    if (ssh_fips_mode()) {
        skip();
    }

    assert_int_equal(sizeof(output), sizeof(chacha20poly1305_encrypted));

    in_length = htonl(sizeof(chacha20poly1305_cleartext));
    memcpy(input, &in_length, sizeof(uint32_t));
    memcpy(input + sizeof(uint32_t), chacha20poly1305_cleartext,
           sizeof(chacha20poly1305_cleartext));

    rc = get_cipher(&cipher, "chacha20-poly1305@openssh.com");
    assert_int_equal(rc, SSH_OK);

    assert_int_equal(sizeof(chacha20poly1305_key) * 8, cipher.keysize);
    assert_non_null(cipher.set_encrypt_key);
    assert_non_null(cipher.aead_encrypt);

    rc = cipher.set_encrypt_key(&cipher, chacha20poly1305_key, NULL);
    assert_int_equal(rc, SSH_OK);

    cipher.aead_encrypt(&cipher, input, output, sizeof(input), outtag,
                        chacha20poly1305_seq);
    assert_memory_equal(output, chacha20poly1305_encrypted,
                        sizeof(chacha20poly1305_encrypted));
    ssh_cipher_clear(&cipher);

    memset(output, '\0', sizeof(output));

    rc = get_cipher(&cipher, "chacha20-poly1305@openssh.com");
    assert_int_equal(rc, SSH_OK);

    assert_non_null(cipher.set_decrypt_key);
    assert_non_null(cipher.aead_decrypt);
    assert_non_null(cipher.aead_decrypt_length);

    rc = cipher.set_decrypt_key(&cipher, chacha20poly1305_key, NULL);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt_length(&cipher, chacha20poly1305_encrypted,
                                    output, sizeof(uint32_t),
                                    chacha20poly1305_seq);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt(&cipher, chacha20poly1305_encrypted,
                             output + sizeof(uint32_t), sizeof(cleartext),
                             chacha20poly1305_seq);
    assert_int_equal(rc, SSH_OK);

    assert_memory_equal(output, input, sizeof(input));

    ssh_cipher_clear(&cipher);
}

static void torture_crypto_chacha20poly1305_bad_packet_length(void **state)
{
    uint8_t output[sizeof(uint32_t) + sizeof(chacha20poly1305_cleartext)] = {0};
    uint8_t encrypted_bad[sizeof(chacha20poly1305_encrypted)];
    struct ssh_cipher_struct cipher = {0};
    int rc;
    (void)state;

    /* Chacha20-poly1305 is not FIPS-allowed cipher */
    if (ssh_fips_mode()) {
        skip();
    }

    /* Test corrupted packet length */
    memcpy(encrypted_bad, chacha20poly1305_encrypted, sizeof(encrypted_bad));
    encrypted_bad[1] ^= 1;

    rc = get_cipher(&cipher, "chacha20-poly1305@openssh.com");
    assert_int_equal(rc, SSH_OK);

    rc = cipher.set_decrypt_key(&cipher, chacha20poly1305_key, NULL);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt_length(&cipher, encrypted_bad,
                                    output, sizeof(uint32_t),
                                    chacha20poly1305_seq);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt(&cipher, encrypted_bad,
                             output + sizeof(uint32_t), sizeof(cleartext),
                             chacha20poly1305_seq);
    assert_int_equal(rc, SSH_ERROR);

    ssh_cipher_clear(&cipher);
}

static void torture_crypto_chacha20poly1305_bad_data(void **state)
{
    uint8_t output[sizeof(uint32_t) + sizeof(chacha20poly1305_cleartext)] = {0};
    uint8_t encrypted_bad[sizeof(chacha20poly1305_encrypted)];
    struct ssh_cipher_struct cipher = {0};
    int rc;
    (void)state;

    /* Chacha20-poly1305 is not FIPS-allowed cipher */
    if (ssh_fips_mode()) {
        skip();
    }

    /* Test corrupted data */
    memcpy(encrypted_bad, chacha20poly1305_encrypted, sizeof(encrypted_bad));
    encrypted_bad[100] ^= 1;

    rc = get_cipher(&cipher, "chacha20-poly1305@openssh.com");
    assert_int_equal(rc, SSH_OK);

    rc = cipher.set_decrypt_key(&cipher, chacha20poly1305_key, NULL);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt_length(&cipher, encrypted_bad,
                                    output, sizeof(uint32_t),
                                    chacha20poly1305_seq);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt(&cipher, encrypted_bad,
                             output + sizeof(uint32_t), sizeof(cleartext),
                             chacha20poly1305_seq);
    assert_int_equal(rc, SSH_ERROR);

    ssh_cipher_clear(&cipher);
}

static void torture_crypto_chacha20poly1305_bad_tag(void **state)
{
    uint8_t output[sizeof(uint32_t) + sizeof(chacha20poly1305_cleartext)] = {0};
    uint8_t encrypted_bad[sizeof(chacha20poly1305_encrypted)];
    struct ssh_cipher_struct cipher = {0};
    int rc;
    (void)state;

    /* Chacha20-poly1305 is not FIPS-allowed cipher */
    if (ssh_fips_mode()) {
        skip();
    }

    /* Test corrupted tag */
    assert_int_equal(sizeof(encrypted_bad), sizeof(chacha20poly1305_encrypted));
    memcpy(encrypted_bad, chacha20poly1305_encrypted, sizeof(encrypted_bad));
    encrypted_bad[sizeof(encrypted_bad) - 1] ^= 1;

    rc = get_cipher(&cipher, "chacha20-poly1305@openssh.com");
    assert_int_equal(rc, SSH_OK);

    rc = cipher.set_decrypt_key(&cipher, chacha20poly1305_key, NULL);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt_length(&cipher, encrypted_bad,
                                    output, sizeof(uint32_t),
                                    chacha20poly1305_seq);
    assert_int_equal(rc, SSH_OK);

    rc = cipher.aead_decrypt(&cipher, encrypted_bad,
                             output + sizeof(uint32_t), sizeof(cleartext),
                             chacha20poly1305_seq);
    assert_int_equal(rc, SSH_ERROR);

    ssh_cipher_clear(&cipher);
}

int torture_run_tests(void) {
    int rc;
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_crypto_aes256_cbc),
        cmocka_unit_test(torture_crypto_chacha20poly1305),
        cmocka_unit_test(torture_crypto_chacha20poly1305_bad_packet_length),
        cmocka_unit_test(torture_crypto_chacha20poly1305_bad_data),
        cmocka_unit_test(torture_crypto_chacha20poly1305_bad_tag),
    };

    ssh_init();
    rc = cmocka_run_group_tests(tests, NULL, NULL);
    ssh_finalize();
    return rc;
}
