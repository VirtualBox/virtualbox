/* SPDX-License-Identifier: BSD-3-Clause */

#include <assert.h>
#include <stddef.h>

#include "libslirp.h"

static slirp_ssize_t guestfwd_write(const void *buf, size_t len, void *opaque)
{
    return len;
}

static SlirpCb callbacks;

static struct in_addr in_addr4(uint32_t addr)
{
    struct in_addr ret = { .s_addr = htonl(addr) };

    return ret;
}

static Slirp *guestfwd_slirp(bool disable_dns)
{
    SlirpConfig config = {
        .version = SLIRP_CONFIG_VERSION_MAX,
        .in_enabled = true,
        .vnetwork = in_addr4(0x0a000200),
        .vnetmask = in_addr4(0xffffff00),
        .vhost = in_addr4(0x0a000202),
        .vdhcp_start = in_addr4(0x0a00020f),
        .vnameserver = in_addr4(0x0a000203),
        .disable_dns = disable_dns,
    };

    return slirp_new(&config, &callbacks, NULL);
}

static void assert_guestfwd(Slirp *slirp, struct in_addr addr, int port,
                            bool expect_ok)
{
    int ret;

    ret = slirp_add_guestfwd(slirp, guestfwd_write, NULL, &addr, port);
    assert((ret == 0) == expect_ok);

    if (ret == 0) {
        assert(slirp_remove_guestfwd(slirp, addr, port) == 0);
    }
}

static void test_guestfwd_validation(void)
{
    struct in_addr vhost = in_addr4(0x0a000202);
    struct in_addr vnameserver = in_addr4(0x0a000203);
    Slirp *slirp;

    slirp = guestfwd_slirp(false);
    assert_guestfwd(slirp, vhost, 8080, true);
    assert_guestfwd(slirp, vnameserver, 8080, true);
    assert_guestfwd(slirp, vhost, 67, true);
    assert_guestfwd(slirp, vhost, 69, true);
    assert_guestfwd(slirp, vnameserver, 53, false);
    slirp_cleanup(slirp);

    slirp = guestfwd_slirp(true);
    assert_guestfwd(slirp, vnameserver, 53, true);
    slirp_cleanup(slirp);
}

int main(int argc, char *argv[])
{
    test_guestfwd_validation();
}
