#include "config.h"

#define LIBSSH_STATIC
#include <libssh/priv.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "torture.h"
#include "channels.c"

#include <libssh/messages.h>

static void torture_channel_select(void **state)
{
    fd_set readfds;
    int fd;
    int rc;
    int i;

    (void)state; /* unused */

    ZERO_STRUCT(readfds);

    fd = open("/dev/null", 0);
    assert_true(fd > 2);

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    for (i = 0; i < 10; i++) {
        ssh_channel cin[1] = { NULL, };
        ssh_channel cout[1] = { NULL, };
        struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 };

        rc = ssh_select(cin, cout, fd + 1, &readfds, &tv);
        assert_int_equal(rc, SSH_OK);
    }

    close(fd);
}

static void torture_channel_null_session(void **state)
{
    ssh_channel channel = NULL;

    (void)state;

    channel = calloc(1, sizeof(struct ssh_channel_struct));

    assert_non_null(channel);

    channel->state = SSH_CHANNEL_STATE_OPEN;
    channel->session = NULL;

    assert_int_equal(ssh_channel_is_open(channel), 0);

    free(channel);
}

/* Feed a fabricated SSH2_MSG_CHANNEL_OPEN_CONFIRMATION with the given
 * maximum packet size to a channel in OPENING state and return its
 * resulting state. */
static enum ssh_channel_state_e
channel_open_conf_maxpacket(uint32_t maxpacket)
{
    ssh_session session = NULL;
    ssh_channel channel = NULL;
    ssh_buffer packet = NULL;
    enum ssh_channel_state_e result;
    int rc;

    session = ssh_new();
    assert_non_null(session);
    session->flags |= SSH_SESSION_FLAG_AUTHENTICATED;

    channel = ssh_channel_new(session);
    assert_non_null(channel);
    channel->local_channel = ssh_channel_new_id(session);
    channel->state = SSH_CHANNEL_STATE_OPENING;

    packet = ssh_buffer_new();
    assert_non_null(packet);
    rc = ssh_buffer_pack(packet,
                         "dddd",
                         channel->local_channel,
                         (uint32_t)42,       /* sender channel */
                         (uint32_t)64000,    /* initial window size */
                         maxpacket);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_packet_channel_open_conf(session,
                                      SSH2_MSG_CHANNEL_OPEN_CONFIRMATION,
                                      packet,
                                      NULL);
    assert_int_equal(rc, SSH_PACKET_USED);

    result = channel->state;

    SSH_BUFFER_FREE(packet);
    ssh_free(session);

    return result;
}

static void torture_channel_open_conf(void **state)
{
    (void)state; /* unused */

    assert_int_equal(channel_open_conf_maxpacket(32768),
                     SSH_CHANNEL_STATE_OPEN);
}

/* CVE-2026-59843: a maximum packet size of 0 in CHANNEL_OPEN_CONFIRMATION
 * must not open the channel, as it would cause an infinite loop in
 * channel_write_common(). */
static void torture_channel_open_conf_zero_maxpacket(void **state)
{
    (void)state; /* unused */

    assert_int_not_equal(channel_open_conf_maxpacket(0),
                         SSH_CHANNEL_STATE_OPEN);
}

/* Feed a fabricated SSH2_MSG_CHANNEL_OPEN with the given maximum packet
 * size to an unauthenticated session and return the resulting session
 * error string via a static buffer. */
static const char *
channel_open_maxpacket_error(uint32_t maxpacket)
{
    static char error[256];
    ssh_session session = NULL;
    ssh_buffer packet = NULL;
    int rc;

    session = ssh_new();
    assert_non_null(session);

    packet = ssh_buffer_new();
    assert_non_null(packet);
    rc = ssh_buffer_pack(packet,
                         "sddd",
                         "session",
                         (uint32_t)42,       /* sender channel */
                         (uint32_t)64000,    /* initial window size */
                         maxpacket);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_packet_channel_open(session,
                                 SSH2_MSG_CHANNEL_OPEN,
                                 packet,
                                 NULL);
    assert_int_equal(rc, SSH_PACKET_USED);

    snprintf(error, sizeof(error), "%s", ssh_get_error(session));

    SSH_BUFFER_FREE(packet);
    ssh_free(session);

    return error;
}

/* CVE-2026-59843: a maximum packet size of 0 in CHANNEL_OPEN must be
 * rejected before any further processing.  The control case with a valid
 * size proceeds to the session state check, proving the zero case failed
 * on the packet size specifically. */
static void torture_channel_open_zero_maxpacket(void **state)
{
    (void)state; /* unused */

    assert_string_equal(
        channel_open_maxpacket_error(0),
        "Invalid maximum packet size 0 in SSH2_MSG_CHANNEL_OPEN");

    assert_string_equal(
        channel_open_maxpacket_error(32768),
        "Invalid state when receiving channel open request "
        "(must be authenticated)");
}

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_channel_select),
        cmocka_unit_test(torture_channel_null_session),
        cmocka_unit_test(torture_channel_open_conf),
        cmocka_unit_test(torture_channel_open_conf_zero_maxpacket),
        cmocka_unit_test(torture_channel_open_zero_maxpacket),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, NULL, NULL);
    ssh_finalize();

    return rc;
}
