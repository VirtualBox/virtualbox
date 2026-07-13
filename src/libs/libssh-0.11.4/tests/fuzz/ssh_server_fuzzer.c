/*
# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
*/

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>

#define LIBSSH_STATIC 1
#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <libssh/server.h>

static const char kRSAPrivateKeyPEM[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEowIBAAKCAQEArAOREUWlBXJAKZ5hABYyxnRayDZP1bJeLbPVK+npxemrhHyZ\n"
    "gjdbY3ADot+JRyWjvll2w2GI+3blt0j+x/ZWwjMKu/QYcycYp5HL01goxOxuusZb\n"
    "i+KiHRGB6z0EMdXM7U82U7lA/j//HyZppyDjUDniWabXQJge8ksGXGTiFeAJ/687\n"
    "uV+JJcjGPxAGFQxzyjitf/FrL9S0WGKZbyqeGDzyeBZ1NLIuaiOORyLGSW4duHLD\n"
    "N78EmsJnwqg2gJQmRSaD4BNZMjtbfiFcSL9Uw4XQFTsWugUDEY1AU4c5g11nhzHz\n"
    "Bi9qMOt5DzrZQpD4j0gA2LOHpHhoOdg1ZuHrGQIDAQABAoIBAFJTaqy/jllq8vZ4\n"
    "TKiD900wBvrns5HtSlHJTe80hqQoT+Sa1cWSxPR0eekL32Hjy9igbMzZ83uWzh7I\n"
    "mtgNODy9vRdznfgO8CfTCaBfAzQsjFpr8QikMT6EUI/LpiRL1UaGsNOlSEvnSS0Z\n"
    "b1uDzAdrjL+nsEHEDJud+K9jwSkCRifVMy7fLfaum+YKpdeEz7K2Mgm5pJ/Vg+9s\n"
    "vI2V1q7HAOI4eUVTgJNHXy5ediRJlajQHf/lNUzHKqn7iH+JRl01gt62X8roG62b\n"
    "TbFylbheqMm9awuSF2ucOcx+guuwhkPir8BEMb08j3hiK+TfwPdY0F6QH4OhiKK7\n"
    "MTqTVgECgYEA0vmmu5GOBtwRmq6gVNCHhdLDQWaxAZqQRmRbzxVhFpbv0GjbQEF7\n"
    "tttq3fjDrzDf6CE9RtZWw2BUSXVq+IXB/bXb1kgWU2xWywm+OFDk9OXQs8ui+MY7\n"
    "FiP3yuq3YJob2g5CCsVQWl2CHvWGmTLhE1ODll39t7Y1uwdcDobJN+ECgYEA0LlR\n"
    "hfMjydWmwqooU9TDjXNBmwufyYlNFTH351amYgFUDpNf35SMCP4hDosUw/zCTDpc\n"
    "+1w04BJJfkH1SNvXSOilpdaYRTYuryDvGmWC66K2KX1nLErhlhs17CwzV997nYgD\n"
    "H3OOU4HfqIKmdGbjvWlkmY+mLHyG10bbpOTbujkCgYAc68xHejSWDCT9p2KjPdLW\n"
    "LYZGuOUa6y1L+QX85Vlh118Ymsczj8Z90qZbt3Zb1b9b+vKDe255agMj7syzNOLa\n"
    "/MseHNOyq+9Z9gP1hGFekQKDIy88GzCOYG/fiT2KKJYY1kuHXnUdbiQgSlghODBS\n"
    "jehD/K6DOJ80/FVKSH/dAQKBgQDJ+apTzpZhJ2f5k6L2jDq3VEK2ACedZEm9Kt9T\n"
    "c1wKFnL6r83kkuB3i0L9ycRMavixvwBfFDjuY4POs5Dh8ip/mPFCa0hqISZHvbzi\n"
    "dDyePJO9zmXaTJPDJ42kfpkofVAnfohXFQEy+cguTk848J+MmMIKfyE0h0QMabr9\n"
    "86BUsQKBgEVgoi4RXwmtGovtMew01ORPV9MOX3v+VnsCgD4/56URKOAngiS70xEP\n"
    "ONwNbTCWuuv43HGzJoVFiAMGnQP1BAJ7gkHkjSegOGKkiw12EPUWhFcMg+GkgPhc\n"
    "pOqNt/VMBPjJ/ysHJqmLfQK9A35JV6Cmdphe+OIl28bcKhAOz8Dw\n"
    "-----END RSA PRIVATE KEY-----\n";

/* A userdata struct for session. */
struct session_data_struct {
    /* Pointer to the channel the session will allocate. */
    ssh_channel channel;
    size_t auth_attempts;
    bool authenticated;
};

static int auth_none(ssh_session session, const char *user, void *userdata)
{
    struct session_data_struct *sdata =
        (struct session_data_struct *)userdata;

    (void)session;
    (void)user;

    if (sdata->auth_attempts > 0) {
        sdata->authenticated = true;
    }
    sdata->auth_attempts++;

    if (!sdata->authenticated) {
        return SSH_AUTH_PARTIAL;
    }

    return SSH_AUTH_SUCCESS;
}

static ssh_channel channel_open(ssh_session session, void *userdata)
{
    struct session_data_struct *sdata =
        (struct session_data_struct *)userdata;

    sdata->channel = ssh_channel_new(session);

    return sdata->channel;
}

static int write_rsa_hostkey(const char *rsakey_path)
{
    FILE *fp = NULL;
    size_t nwritten;

    fp = fopen(rsakey_path, "wb");
    if (fp == NULL) {
        return -1;
    }

    nwritten = fwrite(kRSAPrivateKeyPEM, 1, strlen(kRSAPrivateKeyPEM), fp);
    fclose(fp);

    if (nwritten != strlen(kRSAPrivateKeyPEM)) {
        return -1;
    }

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int socket_fds[2] = {-1, -1};
    ssize_t nwritten;
    bool no = false;
    const char *env = NULL;
    int rc;

    /* Our struct holding information about the session. */
    struct session_data_struct sdata = {
        .channel       = NULL,
        .auth_attempts = 0,
        .authenticated = false,
    };

    struct ssh_server_callbacks_struct server_cb = {
        .userdata = &sdata,
        .auth_none_function = auth_none,
        .channel_open_request_session_function = channel_open,
    };

    /* This is the maximum that can be handled by the socket buffer before the
     * other side will read some data. Other option would be feeding the socket
     * from different thread which would not mind if it would be blocked, but I
     * believe all the important inputs should fit into this size */
    if (size > 219264) {
        return -1;
    }

    /* Write SSH RSA host key to disk */
    rc = write_rsa_hostkey("/tmp/libssh_fuzzer_private_key");
    assert(rc == 0);

    /* Set up the socket to send data */
    rc = socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds);
    assert(rc == 0);

    nwritten = send(socket_fds[1], data, size, 0);
    assert((size_t)nwritten == size);

    rc = shutdown(socket_fds[1], SHUT_WR);
    assert(rc == 0);

    /* Set up the libssh server */
    ssh_bind sshbind = ssh_bind_new();
    assert(sshbind != NULL);

    ssh_session session = ssh_new();
    assert(session != NULL);


    env = getenv("LIBSSH_VERBOSITY");
    if (env != NULL && strlen(env) > 0) {
        rc = ssh_bind_options_set(sshbind,
                                  SSH_BIND_OPTIONS_LOG_VERBOSITY_STR,
                                  env);
        assert(rc == 0);
    }
    rc = ssh_bind_options_set(sshbind,
                              SSH_BIND_OPTIONS_HOSTKEY,
                              "/tmp/libssh_fuzzer_private_key");
    assert(rc == 0);
    rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_CIPHERS_C_S, "none");
    assert(rc == 0);
    rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_CIPHERS_S_C, "none");
    assert(rc == 0);
    rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HMAC_C_S, "none");
    assert(rc == 0);
    rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HMAC_S_C, "none");
    assert(rc == 0);
    rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_PROCESS_CONFIG, &no);
    assert(rc == 0);

    ssh_set_auth_methods(session, SSH_AUTH_METHOD_NONE);

    ssh_callbacks_init(&server_cb);
    ssh_set_server_callbacks(session, &server_cb);

    rc = ssh_bind_accept_fd(sshbind, session, socket_fds[0]);
    assert(rc == SSH_OK);

    ssh_event event = ssh_event_new();
    assert(event != NULL);

    if (ssh_handle_key_exchange(session) == SSH_OK) {
        ssh_event_add_session(event, session);

        size_t n = 0;
        while (sdata.authenticated == false || sdata.channel == NULL) {
            if (sdata.auth_attempts >= 3 || n >= 100) {
                break;
            }

            if (ssh_event_dopoll(event, 100) == SSH_ERROR) {
                break;
            }

            n++;
        }
    }

    ssh_event_free(event);

    close(socket_fds[0]);
    close(socket_fds[1]);

    ssh_disconnect(session);
    ssh_free(session);
    ssh_bind_free(sshbind);

    return 0;
}
