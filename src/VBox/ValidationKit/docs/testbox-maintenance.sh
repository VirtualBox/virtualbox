#!/bin/bash
# $Id: testbox-maintenance.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Validation Kit - testbox maintenance service
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
MY_REBOOT_WHEN_DONE="yes"
#MY_REBOOT_WHEN_DONE="" # enable this for debugging the script

MY_TFTP_ROOT="/mnt/testbox-tftp"
MY_BACKUP_ROOT="/mnt/testbox-backup"
MY_BACKUP_MNT_TEST_FILE="/mnt/testbox-backup/testbox-backup"
MY_GLOBAL_LOG_FILE="${MY_BACKUP_ROOT}/maintenance.log"
MY_DD_BLOCK_SIZE=256K

MY_IP=""
MY_BACKUP_DIR=""
MY_LOG_FILE=""
MY_PXELINUX_CFG_FILE=""


##
# Info message.
#
InfoMsg()
{
    echo $*;
    if test -n "${MY_LOG_FILE}"; then
        echo "`date -uIsec`: ${MY_IP}: info:" $* >> ${MY_LOG_FILE};
    fi
}


##
# Error message and reboot+exit.  First argument is exit code.
#
ErrorMsgExit()
{
    MY_RET=$1
    shift
    echo "testbox-maintenance.sh: error:" $* >&2;
    # Append to the testbox log.
    if test -n "${MY_LOG_FILE}"; then
        echo "`date -uIsec`: ${MY_IP}: error:" $* >> "${MY_LOG_FILE}";
    fi
    # Append to the global log.
    if test -f "${MY_BACKUP_MNT_TEST_FILE}"; then
        echo "`date -uIsec`: ${MY_IP}: error:" $* >> "${MY_GLOBAL_LOG_FILE}";
    fi

    #
    # On error we normally wait 5min before rebooting to avoid repeating the
    # same error too many time before the admin finds out.  We choose NOT to
    # remove the PXE config file here because (a) the admin might otherwise
    # not notice something went wrong, (b) the system could easily be in a
    # weird unbootable state, (c) the problem might be temporary.
    #
    # While debugging, we just exit here.
    #
    if test -n "${MY_REBOOT_WHEN_DONE}"; then
        sleep 5m
        echo "testbox-maintenance.sh: rebooting (after error)" >&2;
        reboot
    fi
    exit ${MY_RET}
}

#
# Try figure out the IP address of the box and the hostname from it again.
#
MY_IP=` hostname -I | cut -f1 -d' ' | head -1 `
if test -z "${MY_IP}"  -o `echo "${MY_IP}" | wc -w` -ne "1"  -o  "${MY_IP}" = "127.0.0.1"; then
    ErrorMsgExit 10 "Failed to get a good IP! (MY_IP=${MY_IP})"
fi
MY_HOSTNAME=`getent hosts "${MY_IP}" | sed -s 's/[[:space:]][[:space:]]*/ /g' | cut -d' ' -f2 `
if test -z "${MY_HOSTNAME}"; then
    MY_HOSTNAME="unknown";
fi

# Derive the backup dir and log file name from it.
if test ! -f "${MY_BACKUP_MNT_TEST_FILE}"; then
    mount "${MY_BACKUP_ROOT}"
    if test ! -f "${MY_BACKUP_MNT_TEST_FILE}"; then
        echo "Retrying mounting '${MY_BACKUP_ROOT}' in 15 seconds..." >&2
        sleep 15
        mount "${MY_BACKUP_ROOT}"
    fi
    if test ! -f "${MY_BACKUP_MNT_TEST_FILE}"; then
        ErrorMsgExit 11 "Backup directory is not mounted."
    fi
fi
MY_BACKUP_DIR="${MY_BACKUP_ROOT}/${MY_IP}"
MY_LOG_FILE="${MY_BACKUP_DIR}/maintenance.log"
mkdir -p "${MY_BACKUP_DIR}"
echo "================ `date -uIsec`: ${MY_IP}: ${MY_HOSTNAME} starts a new session ================" >> "${MY_LOG_FILE}"
echo "`date -uIsec`: ${MY_IP}: ${MY_HOSTNAME} says hi." >> "${MY_GLOBAL_LOG_FILE}"
InfoMsg "MY_IP=${MY_IP}<eol>"

#
# Redirect stderr+stdout thru tee and to a log file on the server.
#
MY_OUTPUT_LOG_FILE="${MY_BACKUP_DIR}/maintenance-output.log"
echo "" >> "${MY_OUTPUT_LOG_FILE}"
echo "================ `date -uIsec`: ${MY_IP}: ${MY_HOSTNAME} starts a new session ================" >> "${MY_OUTPUT_LOG_FILE}"
exec &> >(tee -a "${MY_OUTPUT_LOG_FILE}")

#
# Convert the IP address to PXELINUX hex format, then check that we've got
# a config file on the TFTP share that we later can remove.  We consider it a
# fatal failure if we don't because we've probably got the wrong IP and we'll
# be stuck doing the same stuff over and over again.
#
MY_TMP=`echo "${MY_IP}" | sed -e 's/\./ /g' `
MY_IP_HEX=`printf "%02X%02X%02X%02X" ${MY_TMP}`
InfoMsg "MY_IP_HEX=${MY_IP_HEX}<eol>"

if test ! -f "${MY_TFTP_ROOT}/pxelinux.0"; then
    mount "${MY_TFTP_ROOT}"
    if test ! -f "${MY_TFTP_ROOT}/pxelinux.0"; then
        echo "Retrying mounting '${MY_TFTP_ROOT}' in 15 seconds..." >&2
        sleep 15
        mount "${MY_BACKUP_ROOT}"
    fi
    if test ! -f "${MY_TFTP_ROOT}/pxelinux.0"; then
        ErrorMsgExit 12 "TFTP share mounted or mixxing pxelinux.0 in the root."
    fi
fi

MY_PXELINUX_CFG_FILE="${MY_TFTP_ROOT}/pxelinux.cfg/${MY_IP_HEX}"
if test ! -f "${MY_PXELINUX_CFG_FILE}"; then
    ErrorMsgExit 13 "No pxelinux.cfg file found (${MY_PXELINUX_CFG_FILE}) - wrong IP?"
fi

#
# Dig the action out of from the kernel command line.
#
if test -n "${MY_REBOOT_WHEN_DONE}"; then
    InfoMsg "/proc/cmdline: `cat /proc/cmdline`"
    set `cat /proc/cmdline`
else
    InfoMsg "Using script command line: $*"
fi
MY_ACTION=not-found
while test $# -ge 1; do
    case "$1" in
        testbox-action-*)
          MY_ACTION="$1"
          ;;
    esac
    shift
done
if test "${MY_ACTION}" = "not-found"; then
    ErrorMsgExit 14 "No action given.  Expected testbox-action-backup, testbox-action-backup-again, testbox-action-restore," \
                    "testbox-action-refresh-info, or testbox-action-rescue on the kernel command line.";
fi

# Validate and shorten the action.
case "${MY_ACTION}" in
    testbox-action-backup)
        MY_ACTION="backup";
        ;;
    testbox-action-backup-again)
        MY_ACTION="backup-again";
        ;;
    testbox-action-restore)
        MY_ACTION="restore";
        ;;
    testbox-action-refresh-info)
        MY_ACTION="refresh-info";
        ;;
    testbox-action-rescue)
        MY_ACTION="rescue";
        ;;
    *)  ErrorMsgExit 15 "Invalid action '${MY_ACTION}'";
        ;;
esac

# Log the action in both logs.
echo "`date -uIsec`: ${MY_IP}: info: Executing '${MY_ACTION}'." >> "${MY_GLOBAL_LOG_FILE}";

#
# Generate missing info for this testbox if backing up.
#
MY_INFO_FILE="${MY_BACKUP_DIR}/testbox-info.txt"
if test '!' -f "${MY_INFO_FILE}"  \
     -o "${MY_ACTION}" = "backup"  \
     -o "${MY_ACTION}" = "backup-again" \
     -o "${MY_ACTION}" = "refresh-info" ;
then
    echo "IP: ${MY_IP}" > ${MY_INFO_FILE};
    echo "HEX-IP: ${MY_IP_HEX}" >> ${MY_INFO_FILE};
    echo "Hostname: ${MY_HOSTNAME}" >> ${MY_INFO_FILE};
    echo "" >> ${MY_INFO_FILE};
    echo "**** cat /proc/cpuinfo ****" >> ${MY_INFO_FILE};
    echo "**** cat /proc/cpuinfo ****" >> ${MY_INFO_FILE};
    echo "**** cat /proc/cpuinfo ****" >> ${MY_INFO_FILE};
    cat /proc/cpuinfo >> ${MY_INFO_FILE};
    echo "" >> ${MY_INFO_FILE};
    echo "**** lspci -vvv ****" >> ${MY_INFO_FILE};
    echo "**** lspci -vvv ****" >> ${MY_INFO_FILE};
    echo "**** lspci -vvv ****" >> ${MY_INFO_FILE};
    lspci -vvv >> ${MY_INFO_FILE} 2>&1;
    echo "" >> ${MY_INFO_FILE};
    echo "**** biosdecode ****" >> ${MY_INFO_FILE};
    echo "**** biosdecode ****" >> ${MY_INFO_FILE};
    echo "**** biosdecode ****" >> ${MY_INFO_FILE};
    biosdecode >> ${MY_INFO_FILE} 2>&1;
    echo "" >> ${MY_INFO_FILE};
    echo "**** dmidecode ****" >> ${MY_INFO_FILE};
    echo "**** dmidecode ****" >> ${MY_INFO_FILE};
    echo "**** dmidecode ****" >> ${MY_INFO_FILE};
    dmidecode >> ${MY_INFO_FILE} 2>&1;
    echo "" >> ${MY_INFO_FILE};
    echo "**** fdisk -l ****" >> ${MY_INFO_FILE};
    echo "**** fdisk -l ****" >> ${MY_INFO_FILE};
    echo "**** fdisk -l ****" >> ${MY_INFO_FILE};
    fdisk -l >> ${MY_INFO_FILE} 2>&1;
    echo "" >> ${MY_INFO_FILE};
    echo "**** dmesg ****" >> ${MY_INFO_FILE};
    echo "**** dmesg ****" >> ${MY_INFO_FILE};
    echo "**** dmesg ****" >> ${MY_INFO_FILE};
    dmesg >> ${MY_INFO_FILE} 2>&1;

    #
    # Get the raw ACPI tables and whatnot since we can.  Use zip as tar will
    # zero pad virtual files due to wrong misleading size returned by stat (4K).
    #
    # Note! /sys/firmware/dmi/entries/15-0/system_event_log/raw_event_log has been
    #       see causing fatal I/O errors, so skip all raw_event_log files.
    #
    zip -qr9 "${MY_BACKUP_DIR}/testbox-info.zip" \
        /proc/cpuinfo \
        /sys/firmware/ \
        -x "*/raw_event_log"
fi

if test '!' -f "${MY_BACKUP_DIR}/${MY_HOSTNAME}" -a "${MY_HOSTNAME}" != "unknown"; then
    echo "${MY_HOSTNAME}" > "${MY_BACKUP_DIR}/${MY_HOSTNAME}"
fi

if test '!' -f "${MY_BACKUP_DIR}/${MY_IP_HEX}"; then
    echo "${MY_IP}" > "${MY_BACKUP_DIR}/${MY_IP_HEX}"
fi

#
# Assemble a list of block devices using /sys/block/* and some filtering.
#
if test -f "${MY_BACKUP_DIR}/disk-devices.lst"; then
    MY_BLOCK_DEVS=`cat ${MY_BACKUP_DIR}/disk-devices.lst \
                   | sed -e 's/[[:space:]][::space::]]*/ /g' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' `;
    if test -z "${MY_BLOCK_DEVS}"; then
        ErrorMsgExit 17 "No block devices found via sys/block."
    fi
    InfoMsg "disk-device.lst: MY_BLOCK_DEVS=${MY_BLOCK_DEVS}";
else
    MY_BLOCK_DEVS="";
    for MY_DEV in `ls /sys/block`; do
        case "${MY_DEV}" in
            [sh]d*)
                MY_BLOCK_DEVS="${MY_BLOCK_DEVS} ${MY_DEV}"
                ;;
            *)  InfoMsg "Ignoring /sys/block/${MY_DEV}";
                ;;
        esac
    done
    if test -z "${MY_BLOCK_DEVS}"; then
        ErrorMsgExit 17 "No block devices found via /sys/block."
    fi
    InfoMsg "/sys/block: MY_BLOCK_DEVS=${MY_BLOCK_DEVS}";
fi

#
# Take action
#
case "${MY_ACTION}" in
    #
    # Create a backup.  The 'backup' action refuses to overwrite an
    # existing backup, but is otherwise identical to 'backup-again'.
    #
    backup|backup-again)
        for MY_DEV in ${MY_BLOCK_DEVS}; do
            MY_DST="${MY_BACKUP_DIR}/${MY_DEV}.gz"
            if test -f "${MY_DST}"; then
                if test "${MY_ACTION}" != 'backup-again'; then
                    ErrorMsgExit 18 "${MY_DST} already exists"
                fi
                InfoMsg "${MY_DST} already exists"
            fi
        done

        # Do the backing up.
        for MY_DEV in ${MY_BLOCK_DEVS}; do
            MY_SRC="/dev/${MY_DEV}"
            MY_DST="${MY_BACKUP_DIR}/${MY_DEV}.gz"
            if test -f "${MY_DST}"; then
                mv -f "${MY_DST}" "${MY_DST}.old";
            fi
            if test -b "${MY_SRC}"; then
                InfoMsg "Backing up ${MY_SRC} to ${MY_DST}...";
                dd if="${MY_SRC}" bs=${MY_DD_BLOCK_SIZE} | gzip -c > "${MY_DST}";
                MY_RCS=("${PIPESTATUS[@]}");
                if test "${MY_RCS[0]}" -eq 0 -a "${MY_RCS[1]}" -eq 0; then
                    InfoMsg "Successfully backed up ${MY_SRC} to ${MY_DST}";
                else
                    rm -f "${MY_DST}";
                    ErrorMsgExit 19 "There was a problem backing up ${MY_SRC} to ${MY_DST}: dd => ${MY_RCS[0]}; gzip => ${MY_RCS[1]}";
                fi
            else
                InfoMsg "Skipping ${MY_SRC} as it either doesn't exist or isn't a block device";
            fi
        done
        ;;

    #
    # Restore existing.
    #
    restore)
        for MY_DEV in ${MY_BLOCK_DEVS}; do
            MY_SRC="${MY_BACKUP_DIR}/${MY_DEV}.gz"
            MY_DST="/dev/${MY_DEV}"
            if test -b "${MY_DST}"; then
                if test -f "${MY_SRC}"; then
                    InfoMsg "Restoring ${MY_SRC} onto ${MY_DST}...";
                    gunzip -c "${MY_SRC}" | dd of="${MY_DST}" bs=${MY_DD_BLOCK_SIZE} iflag=fullblock;
                    MY_RCS=("${PIPESTATUS[@]}");
                    if test ${MY_RCS[0]} -eq 0 -a ${MY_RCS[1]} -eq 0; then
                        InfoMsg "Successfully restored ${MY_SRC} onto ${MY_DST}";
                    else
                        ErrorMsgExit 20 "There was a problem restoring ${MY_SRC} onto ${MY_DST}: dd => ${MY_RCS[1]}; gunzip => ${MY_RCS[0]}";
                    fi
                else
                    InfoMsg "Skipping ${MY_DST} because ${MY_SRC} does not exist.";
                fi
            else
                InfoMsg "Skipping ${MY_DST} as it either doesn't exist or isn't a block device.";
            fi
        done
        ;;

    #
    # Nothing else to do for refresh-info.
    #
    refresh-info)
        ;;

    #
    # For the rescue action, we just quit without removing the PXE config or
    # rebooting the box.  The admin will do that once the system has been rescued.
    #
    rescue)
        InfoMsg "rescue: exiting. Admin must remove PXE config and reboot manually when done."
        exit 0;
        ;;

    *)  ErrorMsgExit 98 "Huh? MY_ACTION='${MY_ACTION}'"
        ;;
esac

#
# If we get here, remove the PXE config and reboot immediately.
#
InfoMsg "'${MY_ACTION}' - done";
if test -n "${MY_REBOOT_WHEN_DONE}"; then
    sync
    if rm -f "${MY_PXELINUX_CFG_FILE}"; then
        InfoMsg "removed ${MY_PXELINUX_CFG_FILE}";
    else
        ErrorMsgExit 99 "failed to remove ${MY_PXELINUX_CFG_FILE}";
    fi
    sync
    InfoMsg "rebooting";
    reboot
fi
exit 0
