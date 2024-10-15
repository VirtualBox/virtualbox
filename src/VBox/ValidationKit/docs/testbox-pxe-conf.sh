#!/bin/bash
# $Id: testbox-pxe-conf.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Validation Kit - testbox pxe config emitter.
#

#
# Copyright (C) 2006-2024 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
# in the VirtualBox distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#
# SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
#


#
# Global Variables (config first).
#
MY_NFS_SERVER_IP="10.165.98.101"
MY_GATEWAY_IP="10.165.98.1"
MY_NETMASK="255.255.254.0"
MY_ETH_DEV="eth0"
MY_AUTO_CFG="none"

# options
MY_PXELINUX_CFG_DIR="/mnt/testbox-tftp/pxelinux.cfg"
MY_ACTION=""
MY_IP=""
MY_IP_HEX=""

#
# Parse arguments.
#
while test "$#" -ge 1; do
    MY_ARG=$1
    shift
    case "${MY_ARG}" in
        -c|--cfg-dir)
            MY_PXELINUX_CFG_DIR="$1";
            shift;
            if test -z "${MY_PXELINUX_CFG_DIR}"; then
                echo "syntax error: Empty pxeclient.cfg path." >&2;
                exit 2;
            fi
            ;;

        -h|--help)
            echo "usage: testbox-pxe-conf.sh: [-c /mnt/testbox-tftp/pxelinux.cfg] <ip> <action>";
            echo "Actions: backup, backup-again, restore, refresh-info, rescue";
            exit 0;
            ;;
        -*)
            echo "syntax error: Invalid option: ${MY_ARG}" >&2;
            exit 2;
            ;;

        *)  if test -z "$MY_ARG"; then
                echo "syntax error: Empty argument" >&2;
                exit 2;
            fi
            if test -z "${MY_IP}"; then
                # Split up the IP if possible, if not do gethostbyname on the argument.
                MY_TMP=`echo "${MY_ARG}" | sed -e 's/\./ /g'`
                if   test `echo "${MY_TMP}" | wc -w` -ne 4 \
                  || ! printf "%02X%02X%02X%02X" ${MY_TMP} > /dev/null 2>&1; then
                    MY_TMP2=`getent hosts "${MY_ARG}" | head -1 | cut -d' ' -f1`;
                    MY_TMP=`echo "${MY_TMP2}" | sed -e 's/\./ /g'`
                    if   test `echo "${MY_TMP}" | wc -w` -eq 4 \
                      && printf "%02X%02X%02X%02X" ${MY_TMP} > /dev/null 2>&1; then
                        echo "info: resolved '${MY_ARG}' as '${MY_TMP2}'";
                        MY_ARG="${MY_TMP2}";
                    else
                        echo "syntax error: Invalid IP: ${MY_ARG}" >&2;
                        exit 2;
                    fi
                fi
                MY_IP_HEX=`printf "%02X%02X%02X%02X" ${MY_TMP}`;
                MY_IP="${MY_ARG}";
            else
                if test -z "${MY_ACTION}"; then
                    case "${MY_ARG}" in
                        backup|backup-again|restore|refresh-info|rescue)
                            MY_ACTION="${MY_ARG}";
                            ;;
                        *)
                            echo "syntax error: Invalid action: ${MY_ARG}" >&2;
                            exit 2;
                            ;;
                    esac
                else
                    echo "syntax error: Too many arguments" >&2;
                    exit 2;
                fi
            fi
            ;;
    esac
done

if test -z "${MY_ACTION}"; then
    echo "syntax error: Insufficient arguments" >&2;
    exit 2;
fi
if test ! -d "${MY_PXELINUX_CFG_DIR}"; then
    echo "error: pxeclient.cfg path does not point to a directory: ${MY_PXELINUX_CFG_DIR}" >&2;
    exit 1;
fi
if test ! -f "${MY_PXELINUX_CFG_DIR}/default"; then
    echo "error: pxeclient.cfg path does contain a 'default' file: ${MY_PXELINUX_CFG_DIR}" >&2;
    exit 1;
fi


#
# Produce the file.
# Using echo here so we can split up the APPEND line more easily.
#
MY_CFG_FILE="${MY_PXELINUX_CFG_DIR}/${MY_IP_HEX}"
set +e
echo "PATH bios" > "${MY_CFG_FILE}";
echo "DEFAULT maintenance" >> "${MY_CFG_FILE}";
echo "LABEL maintenance" >> "${MY_CFG_FILE}";
echo "  MENU LABEL Maintenance (NFS)" >> "${MY_CFG_FILE}";
echo "  KERNEL maintenance-boot/vmlinuz-3.16.0-4-amd64" >> "${MY_CFG_FILE}";
echo -n "  APPEND initrd=maintenance-boot/initrd.img-3.16.0-4-amd64 testbox-action-${MY_ACTION}" >> "${MY_CFG_FILE}";
echo -n " ro aufs=tmpfs boot=nfs root=/dev/nfs" >> "${MY_CFG_FILE}";
echo -n " nfsroot=${MY_NFS_SERVER_IP}:/export/testbox-nfsroot,ro,tcp" >> "${MY_CFG_FILE}";
echo -n " nfsvers=3 nfsrootdebug" >> "${MY_CFG_FILE}";
if test "${MY_AUTO_CFG}" = "none"; then
    # Note! Only 6 arguments to ip! Userland ipconfig utility barfs if autoconf and dns options are given.
    echo -n " ip=${MY_IP}:${MY_NFS_SERVER_IP}:${MY_GATEWAY_IP}:${MY_NETMASK}:maintenance:${MY_ETH_DEV}" >> "${MY_CFG_FILE}";
else
    echo -n " ip=${MY_AUTO_CFG}" >> "${MY_CFG_FILE}";
fi
echo "" >> "${MY_CFG_FILE}";
echo "LABEL local-boot" >> "${MY_CFG_FILE}";
echo "LOCALBOOT" >> "${MY_CFG_FILE}";
echo "Successfully generated '${MY_CFG_FILE}'."
exit 0;

