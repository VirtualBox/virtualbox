#include "config.h"
#include "torture.h"
#include "libssh/options.h"
#include "libssh/session.h"
#include "match.c"
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#include <net/if.h>
#include <stdbool.h>

/* This list contains common local subnet addresses and more generic ones */
#define IPV4_LIST                                                       \
    "158.46.192.0/18,213.86.215.224/27,61.67.54.0/23,164.155.128.0/21," \
    "171.10.0.0/16,205.59.221.0/24,122.105.209.48/28,10.0.1.0/24,"      \
    "130.192.28.0/22,172.16.16.0/16,192.168.0.0/24,169.254.0.0/16"

#define IPV6_LIST "fe80::/64"

static int
setup(void **state)
{
    ssh_session session = NULL;
    char *wd = NULL;
    int verbosity;

    session = ssh_new();

    verbosity = torture_libssh_verbosity();
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    wd = torture_get_current_working_dir();
    ssh_options_set(session, SSH_OPTIONS_SSH_DIR, wd);
    free(wd);

    *state = session;

    return 0;
}

static int
teardown(void **state)
{
    ssh_free(*state);

    return 0;
}

/**
 * @brief helper function loading configuration from either file or string
 */
static void
_parse_config(ssh_session session,
              const char *file,
              const char *string,
              int expected)
{
    /*
     * Initialisation of ret is not needed, but the compiler is not able to
     * understand fail() so it will complain about uninitialised use of ret
     * below in assert_ssh_return_code_equal()
     */
    int ret = -1;

    /*
     * make sure either config file or config string is given,
     * not both
     */
    assert_int_not_equal(file == NULL, string == NULL);

    if (file != NULL) {
        ret = ssh_config_parse_file(session, file);
    } else if (string != NULL) {
        ret = ssh_config_parse_string(session, string);
    } else {
        /* should not happen */
        fail();
    }

    /* make sure parsing went as expected */
    assert_ssh_return_code_equal(session, ret, expected);
}

/**
 * @brief converts subnet mask to prefix length (IPv4)
 */
static int
subnet_mask_to_prefix_length_4(struct in_addr subnet_mask)
{
    uint32_t mask;
    int prefix_length = 0;

    mask = ntohl(subnet_mask.s_addr);

    /* Count the number of consecutive 1 bits */
    while (mask & 0x80000000) {
        prefix_length++;
        mask <<= 1;
    }
    return prefix_length;
}

/**
 * @brief converts subnet mask to prefix length (IPv6)
 */
static int
subnet_mask_to_prefix_length_6(struct in6_addr subnet_mask)
{
    uint8_t *mask = NULL, chunk;
    int i, j, prefix_length = 0;

    mask = subnet_mask.s6_addr;

    /* Count the number of consecutive 1 bits in each byte chunk */
    for (i = 0; i < 16; i++) {
        chunk = mask[i];
        while (chunk) {
            for (j = 0; j < 8; j++) {
                if (chunk & 0x80) {
                    prefix_length++;
                    chunk <<= 1;
                } else {
                    break;
                }
            }
        }
    }
    return prefix_length;
}

/**
 * @brief helper function returning the IPv4 and IPv6 network ID
 * (in CIDR format) corresponding to any of the running local interfaces.
 * The network interface corresponding to IPv4 and IPv6 network ID may be
 * different.
 *
 * @note If no non-loopback network interfaces are found for IPv4 or
 * IPv6, the function will fall back to using the loopback addresses.
 */
static int
get_network_id(char *net_id_4, char *net_id_6)
{
    struct ifaddrs *ifa = NULL, *ifaddrs = NULL;
    struct in_addr addr, network_id_4, subnet_mask_4;
    struct in6_addr addr6, network_id_6, subnet_mask_6;
    struct sockaddr_in netmask;
    struct sockaddr_in6 netmask6;
    char address[NI_MAXHOST], *a = NULL;
    char *network_id_str = NULL, network_id_str6[INET6_ADDRSTRLEN],
         lo_net_id_4[NI_MAXHOST], lo_net_id_6[NI_MAXHOST];
    int i, prefix_length, rc;
    int found_4 = 0, found_lo_4 = 0, found_6 = 0, found_lo_6 = 0;
    socklen_t sa_len;

    ZERO_STRUCT(addr);
    ZERO_STRUCT(network_id_4);
    ZERO_STRUCT(subnet_mask_4);

    ZERO_STRUCT(addr6);
    ZERO_STRUCT(network_id_6);
    ZERO_STRUCT(subnet_mask_6);

    if (getifaddrs(&ifaddrs) != 0) {
        goto out;
    }

    for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        if (found_4 && found_6) {
            break;
        }

        if (ifa->ifa_addr == NULL || (ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }

        switch (ifa->ifa_addr->sa_family) {
        case AF_INET:
            if (found_4) {
                continue;
            }
            sa_len = sizeof(struct sockaddr_in);
            break;
        case AF_INET6:
            if (found_6) {
                continue;
            }
            sa_len = sizeof(struct sockaddr_in6);
            break;
        default:
            continue;
        }

        rc = getnameinfo(ifa->ifa_addr,
                         sa_len,
                         address,
                         sizeof(address),
                         NULL,
                         0,
                         NI_NUMERICHOST);
        if (rc != 0) {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET) {

            /* Extract subnet mask */
            memcpy(&netmask, ifa->ifa_netmask, sizeof(struct sockaddr_in));
            subnet_mask_4 = netmask.sin_addr;

            rc = inet_pton(AF_INET, address, &addr);
            if (rc == 0) {
                continue;
            }

            /* Calculate the network ID */
            network_id_4.s_addr = addr.s_addr & subnet_mask_4.s_addr;

            /* Convert network ID to string and compute prefix length */
            network_id_str = inet_ntoa(network_id_4);
            if (network_id_str == NULL) {
                continue;
            }
            prefix_length = subnet_mask_to_prefix_length_4(subnet_mask_4);
            if (prefix_length > 32) {
                continue;
            }

            if (strcmp(ifa->ifa_name, "lo") == 0) {
                /* Store it temporarily in case needed for fallback */
                snprintf(lo_net_id_4,
                         NI_MAXHOST,
                         "%s/%u",
                         network_id_str,
                         prefix_length);
                found_lo_4 = 1;
            } else {
                snprintf(net_id_4,
                         NI_MAXHOST,
                         "%s/%u",
                         network_id_str,
                         prefix_length);
                found_4 = 1;
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {

            /* Remove interface in case of IPv6 address: addr%interface */
            a = strchr(address, '%');
            if (a != NULL) {
                *a = '\0';
            }

            /* Extract subnet mask */
            memcpy(&netmask6, ifa->ifa_netmask, sizeof(struct sockaddr_in6));
            subnet_mask_6 = netmask6.sin6_addr;

            rc = inet_pton(AF_INET6, address, &addr6);
            if (rc == 0) {
                continue;
            }

            /* Calculate the network ID */
            for (i = 0; i < 16; i++) {
                network_id_6.s6_addr[i] =
                    addr6.s6_addr[i] & subnet_mask_6.s6_addr[i];
            }

            /* Convert network ID to string and compute prefix length */
            if (inet_ntop(AF_INET6,
                          &network_id_6,
                          network_id_str6,
                          INET6_ADDRSTRLEN) == NULL) {
                continue;
            }
            prefix_length = subnet_mask_to_prefix_length_6(subnet_mask_6);
            if (prefix_length > 128) {
                continue;
            }

            if (strcmp(ifa->ifa_name, "lo") == 0) {
                /* Store it temporarily in case needed for fallback */
                snprintf(lo_net_id_6,
                         NI_MAXHOST,
                         "%s/%u",
                         network_id_str6,
                         prefix_length);
                found_lo_6 = 1;
            } else {
                snprintf(net_id_6,
                         NI_MAXHOST,
                         "%s/%u",
                         network_id_str6,
                         prefix_length);
                found_6 = 1;
            }
        }
    }

    /*
     * Fallback to the loopback network ID (127.0.0.0/8) if no other
     * IPv4 network ID has been found.
     */
    if (!found_4 && found_lo_4) {
        snprintf(net_id_4, NI_MAXHOST, "%s", lo_net_id_4);
        found_4 = 1;
    }

    /*
     * Fallback to the loopback network ID (::1/128) if no other
     * IPv6 network ID has been found.
     */
    if (!found_6 && found_lo_6) {
        snprintf(net_id_6, NI_MAXHOST, "%s", lo_net_id_6);
        found_6 = 1;
    }

    freeifaddrs(ifaddrs);

out:
    /* if both net_id_4 and net_id_6 are not set then we should fail */
    return (found_4 && found_6) ? 0 : -1;
}

/**
 * @brief Verify the match between a IPv4/IPv6 address and a IPv4/IPv6 subnet
 */
static void
assert_true_match_cidr(const char *try,
                       const char *match,
                       unsigned int mask_len,
                       int af,
                       int rv)
{
    struct in_addr try_addr, match_addr;
    struct in6_addr try_addr6, match_addr6;
    int r1, r2;

    switch (af) {
    case AF_INET:
        ZERO_STRUCT(try_addr);
        ZERO_STRUCT(match_addr);

        r1 = inet_pton(AF_INET, try, &try_addr);
        r2 = inet_pton(AF_INET, match, &match_addr);
        if (r1 == 0 || r2 == 0) {
            fail();
        }
        assert_int_equal(cidr_match_4(&try_addr, &match_addr, mask_len), rv);
        break;
    case AF_INET6:
        ZERO_STRUCT(try_addr6);
        ZERO_STRUCT(match_addr6);

        r1 = inet_pton(AF_INET6, try, &try_addr6);
        r2 = inet_pton(AF_INET6, match, &match_addr6);
        if (r1 == 0 || r2 == 0) {
            fail();
        }
        assert_int_equal(cidr_match_6(&try_addr6, &match_addr6, mask_len), rv);
        break;
    default:
        fail();
    }
}

/**
 * @brief Verify the configuration parser accepts Match localnetwork keyword
 */
static void
torture_config_match_localnetwork(void **state, bool use_file)
{
    ssh_session session = *state;
    const char *config = NULL;
    char config_string[2048];
    char network_id_4[NI_MAXHOST], network_id_6[NI_MAXHOST];
    const char *file = NULL, *string = NULL;

    if (use_file == true) {
        file = "libssh_testconfig_localnetwork.tmp";
    }

    if (get_network_id(network_id_4, network_id_6) == -1) {
        fail();
    }

    /* IPv4 test */
    snprintf(config_string,
             sizeof(config_string),
             "Match localnetwork %s\n"
             "\tHostName expected.com\n",
             network_id_4);
    config = config_string;

    if (use_file == true) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "expected.com");

    /* IPv6 test */
    snprintf(config_string,
             sizeof(config_string),
             "Match localnetwork %s\n"
             "\tHostName expected.com\n",
             network_id_6);
    config = config_string;

    if (use_file == true) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "expected.com");

    /* Test negate condition */
    snprintf(config_string,
             sizeof(config_string),
             "Match Host station !localnetwork %s\n"
             "\tHostName expected.com\n"
             "Host station\n"
             "\tHostName negate.com\n",
             network_id_4);
    config = config_string;

    if (use_file == true) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "station");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "negate.com");
}

/**
 * @brief Verify the configuration parser accepts Match localnetwork keyword
 * through configuration file.
 */
static void
torture_config_match_localnetwork_file(void **state)
{
    torture_config_match_localnetwork(state, true);
}

/**
 * @brief Verify the configuration parser accepts Match localnetwork keyword
 * through configuration string.
 */
static void
torture_config_match_localnetwork_string(void **state)
{
    torture_config_match_localnetwork(state, false);
}

/**
 * @brief Verify the cidr matching function works correctly
 * with IPv4 addresses
 */
static void
torture_match_cidr_address_list_ipv4(void **state)
{
    int rc;
    (void)state;

    /* Test some valid IPv4 addresses */
    rc = match_cidr_address_list("192.158.50.5", "192.158.50.0/28", AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("10.2.200.200", "10.2.128.0/17", AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("192.168.175.40", "192.168.175.0/26", AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("172.31.140.100", "172.31.128.0/19", AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("10.3.9.50", "10.3.8.0/23", AF_INET);
    assert_int_equal(rc, 1);

    /* Test positive match with unknown host address family */
    rc = match_cidr_address_list("158.15.96.13", "158.12.30.0/12", -1);
    assert_int_equal(rc, 1);

    /* Test some valid IPv4 addresses against IPV4_LIST */
    rc = match_cidr_address_list("164.155.128.15", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("158.46.223.71", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("205.59.221.160", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("10.0.1.254", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("172.16.58.1", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 1);
    rc = match_cidr_address_list("169.254.20.28", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 1);

    rc = match_cidr_address_list("255.255.255.255", "0.0.0.0/0", AF_INET);
    assert_int_equal(rc, 1);

    /* Test some not matching IPv4 addresses */
    rc = match_cidr_address_list("172.21.0.200", "172.20.240.0/20", AF_INET);
    assert_int_equal(rc, 0);
    rc = match_cidr_address_list("10.10.14.100", "10.10.10.0/22", AF_INET);
    assert_int_equal(rc, 0);
    rc = match_cidr_address_list("192.168.150.8", "192.168.150.0/29", AF_INET);
    assert_int_equal(rc, 0);
    rc = match_cidr_address_list("10.238.16.50", "10.255.0.0/12", AF_INET);
    assert_int_equal(rc, 0);
    rc = match_cidr_address_list("172.31.160.100", "172.31.128.0/19", AF_INET);
    assert_int_equal(rc, 0);
    rc = match_cidr_address_list("192.168.4.98", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 0);
    rc = match_cidr_address_list("0.0.0.0", IPV4_LIST, AF_INET);
    assert_int_equal(rc, 0);

    /* Test negative match with unknown host address family */
    rc = match_cidr_address_list("122.105.210.57", IPV4_LIST, -1);
    assert_int_equal(rc, 0);

    /* Test some invalid input */
    rc = match_cidr_address_list("192.168.1.x", "192.168.1.0/24", AF_INET);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("0.168.f2.b8", "172.0.0.0/24", AF_INET);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("10.0.1.2/22", "10.0.1.0/22", AF_INET);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("10.0.1.2/", "10.0.1.0/22", AF_INET);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("172.16.16.5/abc1", "172.16.16.0/24", AF_INET);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("172.16.18.251", "172.16.16.0", AF_INET);
    assert_int_equal(rc, -1);

    /* Test invalid input with unknown host address family */
    rc = match_cidr_address_list("172.67.3.x", IPV4_LIST, -1);
    assert_int_equal(rc, -1);

    /* Test invalid CIDR list */
    rc = match_cidr_address_list(NULL, "192.168.1.0/33", AF_INET);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list(NULL, "", -1);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list(NULL, ",", -1);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list(NULL, ",192.168.1.0/24", -1);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list(NULL, "10.0.0.0/24 , 192.168.1.0/24", -1);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list(
        NULL,
        "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255/128junkdata",
        -1);
    assert_int_equal(rc, -1);
}

/**
 * @brief Verify the cidr matching function works correctly
 * with IPv6 addresses
 */
static void
torture_match_cidr_address_list_ipv6(void **state)
{
    /* Test link-local addresses against fe80::/64 */
    int i, rc, valid_addr_len, invalid_addr_len;
    const char *valid_addr[] = {"fe80::aadf:b119:507a:986a%abcdef",
                                "fe80::0000:b418:efd4:5160:0a25%abcdef",
                                "fe80::c7f5:7f94:4bd9:c35c%abcdef",
                                "fe80::321f:46c2:0cea:ec54%abcdef",
                                "fe80::906d:b670:86a2:fd68%abc",
                                "fe80::b1c2:0000:0039:b598%",
                                "fe80::07e8:39e6:cb49:9cd4",
                                "fe80::1%abcdef",
                                "fe80:0:0:0:202:b3ff:fe1e:8329%abcdef",
                                "fe80:0000:0000:0000:0202:b3ff:fe1e:8329"};

    const char *invalid_addr[] = {"fe80::8d1d:4d88:68a8:44f8:f3e7%abcdef",
                                  "2001:0db8:85a3::8a2e:0370:7334%abcdef",
                                  "fd00::adf8:7c21:147c:6c97",
                                  "::1%lo",
                                  "fe80::1:4d88:68a8:1200:f3e7%abcdef"};

    (void)state;

    /* Test valid link-local addresses */
    valid_addr_len = sizeof(valid_addr) / sizeof(valid_addr[0]);
    for (i = 0; i < valid_addr_len; i++) {
        rc = match_cidr_address_list(valid_addr[i], IPV6_LIST, AF_INET6);
        assert_int_equal(rc, 1);
    }
    rc = match_cidr_address_list("fe80:0000:0000:0000:0202:b3ff:fe1e:8329",
                                 "fe80:0000:0000:0000:0202:b3ff:fe1e:8328/127",
                                 AF_INET6);
    assert_int_equal(rc, 1);

    /* Test positive match with unknown host address family */
    rc = match_cidr_address_list("fe80::aadf:b119:507a:986a%abcdef",
                                 IPV6_LIST,
                                 -1);
    assert_int_equal(rc, 1);

    /* Test some invalid input */
    invalid_addr_len = sizeof(invalid_addr) / sizeof(invalid_addr[0]);
    for (i = 0; i < invalid_addr_len; i++) {
        rc = match_cidr_address_list(invalid_addr[i], IPV6_LIST, AF_INET6);
        assert_int_equal(rc, 0);
    }

    /* Test negative match with unknown host address family */
    rc = match_cidr_address_list("fe80::8d1d:4d88:68a8:44f8:f3e7%abcdef",
                                 IPV6_LIST,
                                 -1);
    assert_int_equal(rc, 0);

    /* Test errors */
    rc = match_cidr_address_list("fe80::be50:09ca::2be3", IPV6_LIST, AF_INET6);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("fe80:x:202:b3ff:fe1e:8329",
                                 IPV6_LIST,
                                 AF_INET6);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("fe80::202:ghfc:zzzz:1a49",
                                 IPV6_LIST,
                                 AF_INET6);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("fe80:0000:0000:0000:0202:b3ff:fe1e:8329",
                                 "fe80:0000:0000:0000:0202:b3ff:fe1e:8329/131",
                                 AF_INET6);
    assert_int_equal(rc, -1);
    rc = match_cidr_address_list("fe80:0000:0000:0000:0202:b3ff:fe1e:8329",
                                 "fe80:0000:0000:0000:0202:b3ff:fe1e:8329//127",
                                 AF_INET6);
    assert_int_equal(rc, -1);

    /* Test invalid input with unknown host address family */
    rc = match_cidr_address_list("fe80::ba67:1002:gffx:zz32", IPV6_LIST, -1);
    assert_int_equal(rc, -1);
}

/**
 * @brief Verify the cidr_match_4 function works correctly
 */
static void
torture_match_cidr_v4(void **state)
{
    int af = AF_INET;
    (void)state;

    /* Test some matching input */
    assert_true_match_cidr("192.168.1.20", "192.168.1.0", 24, af, 1);
    assert_true_match_cidr("172.31.5.128", "172.31.0.0", 16, af, 1);
    assert_true_match_cidr("10.0.0.158", "10.0.0.128", 25, af, 1);
    assert_true_match_cidr("192.168.255.250", "192.168.255.248", 29, af, 1);
    assert_true_match_cidr("122.105.209.57", "122.105.209.48", 28, af, 1);
    assert_true_match_cidr("192.168.100.150", "192.168.64.0", 18, af, 1);

    /* Test some not matching input */
    assert_true_match_cidr("172.16.56.30", "172.16.48.0", 21, af, 0);
    assert_true_match_cidr("10.18.5.5", "10.10.4.0", 23, af, 0);
    assert_true_match_cidr("172.16.32.50", "172.16.0.0", 19, af, 0);
    assert_true_match_cidr("203.0.120.10", "203.0.112.0", 21, af, 0);
    assert_true_match_cidr("172.31.112.150", "172.31.96.0", 20, af, 0);
    assert_true_match_cidr("198.52.20.200", "198.48.0.0", 14, af, 0);
}

/**
 * @brief Verify the cidr_match_6 function works correctly
 */
static void
torture_match_cidr_v6(void **state)
{
    int af = AF_INET6;
    (void)state;

    /* Test some matching input */
    assert_true_match_cidr("2001:0db8:85a3:0000:0000:8a2e:0370:7334",
                           "2001:0db8:85a3:0000::",
                           64,
                           af,
                           1);
    assert_true_match_cidr("2001:0db8:0000:0042:0000:8a2e:0370:7334",
                           "2001:0db8:0000::",
                           48,
                           af,
                           1);
    assert_true_match_cidr("fe80::8a2e:0370:7334", "fe80::", 64, af, 1);
    assert_true_match_cidr("fd00::8a2e:0370:7334", "fd00::", 56, af, 1);
    assert_true_match_cidr("fe80:0000:0000:0000:0000:0000:fe1e:32ff",
                           "fe80::",
                           96,
                           af,
                           1);
    assert_true_match_cidr("2001:0db8:1a2b:3c4d:5e6f:7a8b::18",
                           "2001:0db8:1a2b:3c4d:5e6f:7a8b::",
                           120,
                           af,
                           1);

    /* Test some not matching input */
    assert_true_match_cidr("2001:0db8:1234:5678:9abc:def0:1234:5678",
                           "2001:0db8:1234:5678::",
                           96,
                           af,
                           0);
    assert_true_match_cidr("2001:3858:accd::",
                           "2001:3858:abcd:eaa1::",
                           48,
                           af,
                           0);
    assert_true_match_cidr("2001:0db8:1234:5678::ff4c",
                           "2001:0db8:1234:5600::",
                           110,
                           af,
                           0);
    assert_true_match_cidr("fe80::0001:af12:a1b2:c3d4:e5f7",
                           "fe80::",
                           64,
                           af,
                           0);
    assert_true_match_cidr("2001:0db8:84ff:ffff:ffff:ffff:ffff:fffa",
                           "2001:0db8:8500::",
                           80,
                           af,
                           0);
    assert_true_match_cidr("::3", "::", 127, af, 0);
}

int
torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            torture_config_match_localnetwork_string,
            setup,
            teardown),
        cmocka_unit_test_setup_teardown(torture_config_match_localnetwork_file,
                                        setup,
                                        teardown),
        cmocka_unit_test(torture_match_cidr_address_list_ipv4),
        cmocka_unit_test(torture_match_cidr_address_list_ipv6),
        cmocka_unit_test(torture_match_cidr_v4),
        cmocka_unit_test(torture_match_cidr_v6),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, setup, teardown);
    ssh_finalize();
    return rc;
}
