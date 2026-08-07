#include "config.h"

#include "sftp_common.c"
#include "torture.h"

#define LIBSSH_STATIC

static void test_sftp_parse_longname(void **state)
{
    const char *lname = NULL;
    char *value = NULL;

    /* state not used */
    (void)state;

    /* Valid example from SFTP draft, page 18:
     * https://datatracker.ietf.org/doc/draft-spaghetti-sshm-filexfer/
     */
    lname = "-rwxr-xr-x   1 mjos     staff      348911 Mar 25 14:29 t-filexfer";
    value = sftp_parse_longname(lname, SFTP_LONGNAME_PERM);
    assert_string_equal(value, "-rwxr-xr-x");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_OWNER);
    assert_string_equal(value, "mjos");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_GROUP);
    assert_string_equal(value, "staff");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_SIZE);
    assert_string_equal(value, "348911");
    free(value);
    /* This function is broken further as the date contains space which breaks
     * the parsing altogether */
    value = sftp_parse_longname(lname, SFTP_LONGNAME_DATE);
    assert_string_equal(value, "Mar");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_TIME);
    assert_string_equal(value, "25");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_NAME);
    assert_string_equal(value, "14:29");
    free(value);
}

static void test_sftp_parse_longname_invalid(void **state)
{
    const char *lname = NULL;
    char *value = NULL;

    /* state not used */
    (void)state;

    /* Invalid inputs should not crash
     */
    lname = NULL;
    value = sftp_parse_longname(lname, SFTP_LONGNAME_PERM);
    assert_null(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_NAME);
    assert_null(value);

    lname = "";
    value = sftp_parse_longname(lname, SFTP_LONGNAME_PERM);
    assert_string_equal(value, "");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_NAME);
    assert_null(value);

    lname = "-rwxr-xr-x   1";
    value = sftp_parse_longname(lname, SFTP_LONGNAME_PERM);
    assert_string_equal(value, "-rwxr-xr-x");
    free(value);
    value = sftp_parse_longname(lname, SFTP_LONGNAME_NAME);
    assert_null(value);
}

int torture_run_tests(void)
{
    int rc;
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sftp_parse_longname),
        cmocka_unit_test(test_sftp_parse_longname_invalid),
    };

    rc = cmocka_run_group_tests(tests, NULL, NULL);
    return rc;
}
