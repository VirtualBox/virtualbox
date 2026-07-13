#include "config.h"

#define LIBSSH_STATIC

#include "torture.h"
#include "libssh/bignum.h"
#include "libssh/string.h"

static void check_str (int n, ssh_string str)
{
    if (n > 0 && n <= 127) {
        assert_int_equal(1, ntohl (str->size));
        assert_int_equal(n, str->data[0]);
    } else if (n > 127 && n <= 255) {
        assert_int_equal(2, ntohl (str->size));
        assert_int_equal(0, str->data[0]);
        assert_int_equal(n, str->data[1]);
    } else if (n > 255 && n <= 32767) {
        assert_int_equal(2, ntohl (str->size));
        assert_int_equal(n >> 8, str->data[0]);
        assert_int_equal(n & 0xFF, str->data[1]);
    } else {
        assert_int_equal(3, ntohl (str->size));
        assert_int_equal(n >> 16, str->data[0]);
        assert_int_equal((n >> 8) & 0xFF, str->data[1]);
        assert_int_equal(n & 0xFF, str->data[2]);
    }
}

static void check_bignum(int n, const char *nstr)
{
    bignum num = NULL, num2 = NULL;
    bignum num3 = NULL;
    ssh_string str = NULL;
    char *dec = NULL;

    num = bignum_new();
    assert_non_null(num);

    assert_int_equal (1, bignum_set_word (num, n));

    ssh_print_bignum("num", num);

    dec = bignum_bn2dec (num);
    assert_non_null (dec);
    assert_string_equal (nstr, dec);
    ssh_crypto_free(dec);

    /* ssh_make_bignum_string */

    str = ssh_make_bignum_string(num);
    assert_non_null(str);

    check_str (n, str);

    /* ssh_make_string_bn */

    num2 = ssh_make_string_bn(str);
    ssh_string_free (str);
    assert_non_null(num2);

    ssh_print_bignum("num2", num2);

    assert_int_equal (0, bignum_cmp (num, num2));

    dec = bignum_bn2dec (num2);
    assert_non_null (dec);
    assert_string_equal (nstr, dec);
    ssh_crypto_free(dec);

    bignum_dup(num, &num3);
    assert_non_null(num3);
    assert_int_equal(0, bignum_cmp(num, num3));

    bignum_safe_free(num);
    bignum_safe_free(num2);
    bignum_safe_free(num3);
}


static void torture_bignum(void **state) {
    (void) state; /* unused */

    ssh_set_log_level(SSH_LOG_TRACE);

    check_bignum (1, "1");
    check_bignum (17, "17");
    check_bignum (42, "42");
    check_bignum (127, "127");
    check_bignum (128, "128");
    check_bignum (254, "254");
    check_bignum (255, "255");
    check_bignum (256, "256");
    check_bignum (257, "257");
    check_bignum (300, "300");
    check_bignum (32767, "32767");
    check_bignum (32768, "32768");
    check_bignum (65535, "65535");
    check_bignum (65536, "65536");
}

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_bignum),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, NULL, NULL);
    ssh_finalize();
    return rc;
}
