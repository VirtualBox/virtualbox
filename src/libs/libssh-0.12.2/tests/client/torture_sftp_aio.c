#define LIBSSH_STATIC

#include "config.h"

#include "torture.h"
#include "sftp.c"

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#define MAX_XFER_BUF_SIZE 16384

#define DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(TEST_NAME) \
    {                            \
        #TEST_NAME,              \
        TEST_NAME,               \
        session_setup,           \
        session_teardown,        \
        NULL                     \
    },                           \
    {                            \
        #TEST_NAME"_proxyjump",  \
        TEST_NAME,               \
        session_proxyjump_setup, \
        session_teardown,        \
        NULL                     \
    }

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);
    torture_setup_sshd_servers(state, false);
    return 0;
}

static int sshd_teardown(void **state)
{
    /* this will take care of the server1 teardown too */
    torture_teardown_sshd_server(state);
    return 0;
}

static int session_setup_helper(void **state, bool with_proxyjump)
{
    struct torture_state *s = *state;
    struct passwd *pwd = NULL;
    int rc;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    if (with_proxyjump) {
        s->ssh.session = torture_ssh_session_proxyjump();
    } else {
        s->ssh.session = torture_ssh_session(s,
                                             TORTURE_SSH_SERVER,
                                             NULL,
                                             TORTURE_SSH_USER_ALICE,
                                             NULL);
    }
    assert_non_null(s->ssh.session);

    s->ssh.tsftp = torture_sftp_session(s->ssh.session);
    assert_non_null(s->ssh.tsftp);

    return 0;
}

static int session_setup(void **state)
{
    return session_setup_helper(state, false);
}

static int session_proxyjump_setup(void **state)
{
    return session_setup_helper(state, true);
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    torture_rmdirs(s->ssh.tsftp->testdir);
    torture_sftp_close(s->ssh.tsftp);
    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void torture_sftp_aio_read_file(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    struct {
        char *buf;
        ssize_t bytes_read;
    } a = {0}, b = {0};

    sftp_file file = NULL;
    sftp_attributes file_attr = NULL;
    int fd;

    size_t chunk_size;
    int in_flight_requests = 20;

    sftp_aio aio = NULL;
    struct ssh_list *aio_queue = NULL;
    sftp_limits_t li = NULL;

    size_t file_size;
    size_t total_bytes_requested;
    size_t to_read, total_bytes_read;
    ssize_t bytes_requested;

    int i, rc;

    /* Get the max limit for reading, use it as the chunk size */
    li = sftp_limits(t->sftp);
    assert_non_null(li);
    chunk_size = li->max_read_length;

    a.buf = calloc(chunk_size, 1);
    assert_non_null(a.buf);

    b.buf = calloc(chunk_size, 1);
    assert_non_null(b.buf);

    aio_queue = ssh_list_new();
    assert_non_null(aio_queue);

    file = sftp_open(t->sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    fd = open(SSH_EXECUTABLE, O_RDONLY, 0);
    assert_int_not_equal(fd, -1);

    /* Get the file size */
    file_attr = sftp_stat(t->sftp, SSH_EXECUTABLE);
    assert_non_null(file_attr);
    file_size = file_attr->size;

    total_bytes_requested = 0;
    for (i = 0;
         i < in_flight_requests && total_bytes_requested < file_size;
         ++i) {
        to_read = file_size - total_bytes_requested;
        if (to_read > chunk_size) {
            to_read = chunk_size;
        }

        bytes_requested = sftp_aio_begin_read(file, to_read, &aio);
        assert_int_equal(bytes_requested, to_read);
        total_bytes_requested += bytes_requested;

        /* enqueue */
        rc = ssh_list_append(aio_queue, aio);
        assert_int_equal(rc, SSH_OK);
    }

    total_bytes_read = 0;
    while ((aio = ssh_list_pop_head(sftp_aio, aio_queue)) != NULL) {
        a.bytes_read = sftp_aio_wait_read(&aio, a.buf, chunk_size);
        assert_int_not_equal(a.bytes_read, SSH_ERROR);

        total_bytes_read += (size_t)a.bytes_read;
        if (total_bytes_read != file_size) {
            assert_int_equal((size_t)a.bytes_read, chunk_size);
            /*
             * Failure of this assertion means that a short
             * read is encountered but we have not reached
             * the end of file yet. A short read before reaching
             * the end of file should not occur for our test where
             * the chunk size respects the max limit for reading.
             */
        }

        /*
         * Check whether the bytes read above are bytes
         * present in the file or some garbage was stored
         * in the buffer supplied to sftp_aio_wait_read().
         */
        b.bytes_read = read(fd, b.buf, a.bytes_read);
        assert_int_equal(a.bytes_read, b.bytes_read);

        rc = memcmp(a.buf, b.buf, (size_t)a.bytes_read);
        assert_int_equal(rc, 0);

        /* Issue more read requests if needed */
        if (total_bytes_requested == file_size) {
            continue;
        }

        /* else issue more requests */
        to_read = file_size - total_bytes_requested;
        if (to_read > chunk_size) {
            to_read = chunk_size;
        }

        bytes_requested = sftp_aio_begin_read(file, to_read, &aio);
        assert_int_equal(bytes_requested, to_read);
        total_bytes_requested += bytes_requested;

        /* enqueue */
        rc = ssh_list_append(aio_queue, aio);
        assert_int_equal(rc, SSH_OK);
    }

    /*
     * Check whether sftp server responds with an
     * eof for more requests.
     */
    bytes_requested = sftp_aio_begin_read(file, chunk_size, &aio);
    assert_int_equal(bytes_requested, chunk_size);

    a.bytes_read = sftp_aio_wait_read(&aio, a.buf, chunk_size);
    assert_int_equal(a.bytes_read, 0);

    /* Clean up */
    sftp_attributes_free(file_attr);
    close(fd);
    sftp_close(file);
    ssh_list_free(aio_queue);
    free(b.buf);
    free(a.buf);
    sftp_limits_free(li);
}

static void torture_sftp_aio_read_more_than_cap(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    sftp_limits_t li = NULL;
    sftp_file file = NULL;
    sftp_aio aio = NULL;

    char *buf = NULL;
    ssize_t bytes;

    /* Get the max limit for reading */
    li = sftp_limits(t->sftp);
    assert_non_null(li);

    file = sftp_open(t->sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    /* Try reading more than the max limit */
    bytes = sftp_aio_begin_read(file,
                                li->max_read_length * 2,
                                &aio);
    assert_int_equal(bytes, li->max_read_length);

    buf = calloc(li->max_read_length, 1);
    assert_non_null(buf);

    bytes = sftp_aio_wait_read(&aio, buf, li->max_read_length);
    assert_int_not_equal(bytes, SSH_ERROR);

    free(buf);
    sftp_close(file);
    sftp_limits_free(li);
}

static void torture_sftp_aio_write_file(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    char file_path[128] = {0};
    sftp_file file = NULL;
    int fd;

    struct {
        char *buf;
        ssize_t bytes;
    } wr = {0}, rd = {0};

    size_t chunk_size;
    ssize_t bytes_requested;
    int in_flight_requests = 2;

    sftp_limits_t li = NULL;
    sftp_aio *aio_queue = NULL;
    int rc, i;

    /* Get the max limit for writing, use it as the chunk size */
    li = sftp_limits(t->sftp);
    assert_non_null(li);
    chunk_size = li->max_write_length;

    rd.buf = calloc(chunk_size, 1);
    assert_non_null(rd.buf);

    wr.buf = calloc(chunk_size, 1);
    assert_non_null(wr.buf);

    aio_queue = malloc(sizeof(sftp_aio) * in_flight_requests);
    assert_non_null(aio_queue);

    snprintf(file_path, sizeof(file_path),
             "%s/libssh_sftp_aio_write_test", t->testdir);
    file = sftp_open(t->sftp, file_path, O_CREAT | O_WRONLY, 0777);
    assert_non_null(file);

    fd = open(file_path, O_RDONLY, 0);
    assert_int_not_equal(fd, -1);

    for (i = 0; i < in_flight_requests; ++i) {
        bytes_requested = sftp_aio_begin_write(file,
                                               wr.buf,
                                               chunk_size,
                                               &aio_queue[i]);
        assert_int_equal(bytes_requested, chunk_size);
    }

    for (i = 0; i < in_flight_requests; ++i) {
        wr.bytes = sftp_aio_wait_write(&aio_queue[i]);
        assert_int_equal(wr.bytes, chunk_size);

        /*
         * Check whether the bytes written to the file
         * by SFTP AIO write api were the bytes present
         * in the buffer to write or some garbage was
         * written to the file.
         */
        rd.bytes = read(fd, rd.buf, wr.bytes);
        assert_int_equal(rd.bytes, wr.bytes);

        rc = memcmp(rd.buf, wr.buf, wr.bytes);
        assert_int_equal(rc, 0);
    }

    /* Clean up */
    close(fd);
    sftp_close(file);
    free(aio_queue);

    rc = unlink(file_path);
    assert_int_equal(rc, 0);

    free(wr.buf);
    free(rd.buf);
    sftp_limits_free(li);
}

static void torture_sftp_aio_write_more_than_cap(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    sftp_limits_t li = NULL;
    char *buf = NULL;
    size_t buf_size;

    char file_path[128] = {0};
    sftp_file file = NULL;

    sftp_aio aio = NULL;
    ssize_t bytes;
    int rc;

    li = sftp_limits(t->sftp);
    assert_non_null(li);

    buf_size = li->max_write_length * 2;
    buf = calloc(buf_size, 1);
    assert_non_null(buf);

    snprintf(file_path, sizeof(file_path),
             "%s/libssh_sftp_aio_write_test_cap", t->testdir);
    file = sftp_open(t->sftp, file_path, O_CREAT | O_WRONLY, 0777);
    assert_non_null(file);

    /* Try writing more than the max limit for writing */
    bytes = sftp_aio_begin_write(file, buf, buf_size, &aio);
    assert_int_equal(bytes, li->max_write_length);

    bytes = sftp_aio_wait_write(&aio);
    assert_int_equal(bytes, li->max_write_length);

    /* Clean up */
    sftp_close(file);

    rc = unlink(file_path);
    assert_int_equal(rc, 0);

    free(buf);
    sftp_limits_free(li);
}

static void torture_sftp_aio_read_negative(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    char *buf = NULL;
    sftp_file file = NULL;
    sftp_aio aio = NULL;
    sftp_limits_t li = NULL;

    size_t chunk_size;
    ssize_t bytes;
    int rc;

    li = sftp_limits(t->sftp);
    assert_non_null(li);
    chunk_size = li->max_read_length;

    buf = calloc(chunk_size, 1);
    assert_non_null(buf);

    /* Open a file for reading */
    file = sftp_open(t->sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    /* Passing NULL as the sftp file handle */
    bytes = sftp_aio_begin_read(NULL, chunk_size, &aio);
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing 0 as the number of bytes to read */
    bytes = sftp_aio_begin_read(file, 0, &aio);
    assert_int_equal(bytes, SSH_ERROR);

    /*
     * Passing NULL instead of a pointer to a location to
     * store an aio handle.
     */
    bytes = sftp_aio_begin_read(file, chunk_size, NULL);
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing NULL instead of a pointer to an aio handle */
    bytes = sftp_aio_wait_read(NULL, buf, sizeof(buf));
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing NULL as the buffer's address */
    bytes = sftp_aio_begin_read(file, chunk_size, &aio);
    assert_int_equal(bytes, chunk_size);

    bytes = sftp_aio_wait_read(&aio, NULL, sizeof(buf));
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing 0 as the buffer size */
    bytes = sftp_aio_begin_read(file, chunk_size, &aio);
    assert_int_equal(bytes, chunk_size);

    bytes = sftp_aio_wait_read(&aio, buf, 0);
    assert_int_equal(bytes, SSH_ERROR);

    /*
     * Test for the scenario when the number
     * of bytes read exceed the buffer size.
     */
    rc = sftp_seek(file, 0); /* Seek to the start of file */
    assert_int_equal(rc, 0);

    bytes = sftp_aio_begin_read(file, 2, &aio);
    assert_int_equal(bytes, 2);

    bytes = sftp_aio_wait_read(&aio, buf, 1);
    assert_int_equal(bytes, SSH_ERROR);

    sftp_close(file);
    free(buf);
    sftp_limits_free(li);
}

static void torture_sftp_aio_write_negative(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    char *buf = NULL;

    char file_path[128] = {0};
    sftp_file file = NULL;
    sftp_aio aio = NULL;
    sftp_limits_t li = NULL;

    size_t chunk_size;
    ssize_t bytes;
    int rc;

    li = sftp_limits(t->sftp);
    assert_non_null(li);
    chunk_size = li->max_write_length;

    buf = calloc(chunk_size, 1);
    assert_non_null(buf);

    /* Open a file for writing */
    snprintf(file_path, sizeof(file_path),
             "%s/libssh_sftp_aio_write_test_negative", t->testdir);
    file = sftp_open(t->sftp, file_path, O_CREAT | O_WRONLY, 0777);
    assert_non_null(file);

    /* Passing NULL as the sftp file handle */
    bytes = sftp_aio_begin_write(NULL, buf, chunk_size, &aio);
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing NULL as the buffer's address */
    bytes = sftp_aio_begin_write(file, NULL, chunk_size, &aio);
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing 0 as the size of buffer */
    bytes = sftp_aio_begin_write(file, buf, 0, &aio);
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing NULL instead of a pointer to a location to store an aio handle */
    bytes = sftp_aio_begin_write(file, buf, chunk_size, NULL);
    assert_int_equal(bytes, SSH_ERROR);

    /* Passing NULL instead of a pointer to an aio handle */
    bytes = sftp_aio_wait_write(NULL);
    assert_int_equal(bytes, SSH_ERROR);

    sftp_close(file);
    rc = unlink(file_path);
    assert_int_equal(rc, 0);

    free(buf);
    sftp_limits_free(li);
}

/*
 * Test that waiting for read responses in an order different from the
 * sending order of corresponding read requests works properly.
 *
 * (For example, if Requests Rq1 and Rq2 have responses Rs1 and Rs2
 * respectively, and Rq1 is sent first followed by Rq2. Then waiting for
 * response Rs2 first and then Rs1 should work properly)
 */
static void torture_sftp_aio_read_unordered_wait(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    sftp_file file = NULL;
    sftp_aio aio_1 = NULL, aio_2 = NULL;
    ssize_t bytes_requested, bytes_read;

    struct {
        /* buffer to store read data */
        char *buf;

        /* buffer to store data expected to be read */
        char *expected;

        /*
         * length of the data to read. Keep this length small enough so that we
         * don't get short reads due to the sftp limits.
         */
        size_t len;
    } r1 = {0}, r2 = {0};

    int fd, rc;

    /* Initialize r1 */
    r1.len = 10;

    r1.buf = calloc(r1.len, 1);
    assert_non_null(r1.buf);

    r1.expected = calloc(r1.len, 1);
    assert_non_null(r1.expected);

    /* Initialize r2 */
    r2.len = 20;

    r2.buf = calloc(r2.len, 1);
    assert_non_null(r2.buf);

    r2.expected = calloc(r2.len, 1);
    assert_non_null(r2.expected);

    /* Get data that is expected to be read from the file */
    fd = open(SSH_EXECUTABLE, O_RDONLY, 0);
    assert_int_not_equal(fd, -1);

    bytes_read = read(fd, r1.expected, r1.len);
    assert_int_equal(bytes_read, r1.len);

    bytes_read = read(fd, r2.expected, r2.len);
    assert_int_equal(bytes_read, r2.len);

    /* Open an sftp file for reading */
    file = sftp_open(t->sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    /*
     * Issue 2 consecutive read requests (send the second request immediately
     * after sending the first without waiting for the first's response)
     */
    bytes_requested = sftp_aio_begin_read(file, r1.len, &aio_1);
    assert_int_equal(bytes_requested, r1.len);

    bytes_requested = sftp_aio_begin_read(file, r2.len, &aio_2);
    assert_int_equal(bytes_requested, r2.len);

    /*
     * Wait for the responses in opposite order (Instead of waiting for response
     * 1 first and then response 2, wait for response 2 first and then wait for
     * response 1)
     */
    bytes_read = sftp_aio_wait_read(&aio_2, r2.buf, r2.len);
    assert_int_equal(bytes_read, r2.len);
    assert_memory_equal(r2.buf, r2.expected, r2.len);

    bytes_read = sftp_aio_wait_read(&aio_1, r1.buf, r1.len);
    assert_int_equal(bytes_read, r1.len);
    assert_memory_equal(r1.buf, r1.expected, r1.len);

    /* Clean up */
    sftp_close(file);

    rc = close(fd);
    assert_int_equal(rc, 0);

    free(r2.expected);
    free(r2.buf);

    free(r1.expected);
    free(r1.buf);
}

/*
 * Test that waiting for write responses in an order different from the
 * sending order of corresponding write requests works properly.
 *
 * (For example, if Requests Rq1 and Rq2 have responses Rs1 and Rs2
 * respectively, and Rq1 is sent first followed by Rq2. Then waiting for
 * response Rs2 first and then Rs1 should work properly)
 */
static void torture_sftp_aio_write_unordered_wait(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    char file_path[128] = {0};
    sftp_file file = NULL;
    sftp_aio aio_1 = NULL, aio_2 = NULL;
    ssize_t bytes_requested, bytes_written, bytes_read;
    size_t i;
    int rc, fd;

    struct {
        /*
         * length of the data to write. Keep this length small enough so that we
         * don't get short writes due to the sftp limits
         */
        size_t len;

        /* data to write */
        char *data;

        /* buffer used to validate the written data */
        char *buf;
    } r1 = {0}, r2 = {0};

    /* Initialize r1 */
    r1.len = 10;

    r1.data = calloc(r1.len, 1);
    assert_non_null(r1.data);

    for (i = 0; i < r1.len; ++i) {
        r1.data[i] = (char)rand();
    }

    r1.buf = calloc(r1.len, 1);
    assert_non_null(r1.buf);

    /* Initialize r2 */
    r2.len = 20;

    r2.data = calloc(r2.len, 1);
    assert_non_null(r2.data);

    for (i = 0; i < r2.len; ++i) {
        r2.data[i] = (char)rand();
    }

    r2.buf = calloc(r2.len, 1);
    assert_non_null(r2.buf);

    /* Open an sftp file for writing */
    snprintf(file_path,
             sizeof(file_path),
             "%s/libssh_sftp_aio_write_unordered_wait",
             t->testdir);
    file = sftp_open(t->sftp, file_path, O_CREAT | O_WRONLY, 0777);
    assert_non_null(file);

    /*
     * Issue two consecutive write requests (send the second request immediately
     * after sending the first without waiting for the first's response)
     */
    bytes_requested = sftp_aio_begin_write(file, r1.data, r1.len, &aio_1);
    assert_int_equal(bytes_requested, r1.len);

    bytes_requested = sftp_aio_begin_write(file, r2.data, r2.len, &aio_2);
    assert_int_equal(bytes_requested, r2.len);

    /*
     * Wait for the responses in opposite order (Instead of waiting for response
     * 1 first and then response 2, wait for response 2 first and then wait for
     * response 1)
     */
    bytes_written = sftp_aio_wait_write(&aio_2);
    assert_int_equal(bytes_written, r2.len);

    bytes_written = sftp_aio_wait_write(&aio_1);
    assert_int_equal(bytes_written, r1.len);

    /*
     * Validate that the data has been written to the file correctly by reading
     * from the file.
     */
    fd = open(file_path, O_RDONLY, 0);
    assert_int_not_equal(fd, -1);

    /* Validate that write request 1's data has been written to file */
    bytes_read = read(fd, r1.buf, r1.len);
    assert_int_equal(bytes_read, r1.len);
    assert_memory_equal(r1.data, r1.buf, r1.len);

    /* Validate that write request 2's data has been written to file */
    bytes_read = read(fd, r2.buf, r2.len);
    assert_int_equal(bytes_read, r2.len);
    assert_memory_equal(r2.data, r2.buf, r2.len);

    /* Clean up */
    rc = close(fd);
    assert_int_equal(rc, 0);

    sftp_close(file);

    rc = unlink(file_path);
    assert_int_equal(rc, 0);

    free(r2.buf);
    free(r2.data);

    free(r1.buf);
    free(r1.data);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(torture_sftp_aio_read_file),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(
            torture_sftp_aio_read_more_than_cap),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(torture_sftp_aio_write_file),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(
            torture_sftp_aio_write_more_than_cap),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(torture_sftp_aio_read_negative),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(torture_sftp_aio_write_negative),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(
            torture_sftp_aio_read_unordered_wait),
        DIRECT_AND_PROXYJUMP_SETUP_TEARDOWN(
            torture_sftp_aio_write_unordered_wait),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
