/*
 * config.h - parse the ssh config file
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2009-2018    by Andreas Schneider <asn@cryptomilk.org>
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

#ifndef LIBSSH_CONFIG_H_
#define LIBSSH_CONFIG_H_

#include "libssh/libssh.h"

enum ssh_config_opcode_e {
    /* Unknown opcode */
    SOC_UNKNOWN = -3,
    /* Known and not applicable to libssh */
    SOC_NA = -2,
    /* Known but not supported by current libssh version */
    SOC_UNSUPPORTED = -1,
    SOC_HOST,
    SOC_MATCH,
    SOC_HOSTNAME,
    SOC_PORT,
    SOC_USERNAME,
    SOC_IDENTITY,
    SOC_CIPHERS,
    SOC_MACS,
    SOC_COMPRESSION,
    SOC_TIMEOUT,
    SOC_STRICTHOSTKEYCHECK,
    SOC_KNOWNHOSTS,
    SOC_PROXYCOMMAND,
    SOC_PROXYJUMP,
    SOC_GSSAPISERVERIDENTITY,
    SOC_GSSAPICLIENTIDENTITY,
    SOC_GSSAPIDELEGATECREDENTIALS,
    SOC_INCLUDE,
    SOC_BINDADDRESS,
    SOC_GLOBALKNOWNHOSTSFILE,
    SOC_LOGLEVEL,
    SOC_HOSTKEYALGORITHMS,
    SOC_KEXALGORITHMS,
    SOC_GSSAPIAUTHENTICATION,
    SOC_KBDINTERACTIVEAUTHENTICATION,
    SOC_PASSWORDAUTHENTICATION,
    SOC_PUBKEYAUTHENTICATION,
    SOC_PUBKEYACCEPTEDKEYTYPES,
    SOC_REKEYLIMIT,
    SOC_IDENTITYAGENT,
    SOC_IDENTITIESONLY,
    SOC_CONTROLMASTER,
    SOC_CONTROLPATH,
    SOC_CERTIFICATE,
    SOC_REQUIRED_RSA_SIZE,
    SOC_ADDRESSFAMILY,
    SOC_GSSAPIKEYEXCHANGE,
    SOC_GSSAPIKEXALGORITHMS,

    SOC_MAX /* Keep this one last in the list */
};
enum ssh_config_opcode_e ssh_config_get_opcode(char *keyword);
int ssh_config_parse_line_cli(ssh_session session, const char *line);

#endif /* LIBSSH_CONFIG_H_ */
