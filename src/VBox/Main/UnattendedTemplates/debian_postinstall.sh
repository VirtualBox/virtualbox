#!/bin/bash
## @file
# Post installation script template for debian-like distros.
#
# Note! This script expects to be running w/o chroot.
# Note! When using ubiquity, this is run after installation logs have
#       been copied to /var/log/installation.
#

#
# Copyright (C) 2017-2025 Oracle and/or its affiliates.
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
# SPDX-License-Identifier: GPL-3.0-only
#


#
# Globals.
#

# subiquity will execute this script inside the target already
if [ "$1" = "--direct" ]; then
    MY_TARGET="/"
else
    MY_TARGET="/target"
fi
MY_LOGFILE="${MY_TARGET}/var/log/vboxpostinstall.log"
MY_CHROOT_CDROM="/cdrom"
MY_CDROM_NOCHROOT="/cdrom"
MY_EXITCODE=0
MY_DEBUG="" # "yes"

@@VBOX_COND_HAS_PROXY@@
PROXY="@@VBOX_INSERT_PROXY@@"
export http_proxy="${PROXY}"
export https_proxy="${PROXY}"
echo "HTTP proxy is ${http_proxy}" | tee -a "${MY_LOGFILE}"
echo "HTTPS proxy is ${https_proxy}" | tee -a "${MY_LOGFILE}"
@@VBOX_COND_END@@

#
# Do we need to exec using target bash?  If so, we must do that early
# or ash will bark 'bad substitution' and fail.
#
if [ "$1" = "--need-target-bash" ]; then
    # Try figure out which directories we might need in the library path.
    if [ -z "${LD_LIBRARY_PATH}" ]; then
        LD_LIBRARY_PATH="${MY_TARGET}/lib"
    fi
    for x in \
        ${MY_TARGET}/lib \
        ${MY_TARGET}/usr/lib \
        ${MY_TARGET}/lib/*linux-gnu/ \
        ${MY_TARGET}/lib32/ \
        ${MY_TARGET}/lib64/ \
        ${MY_TARGET}/usr/lib/*linux-gnu/ \
        ${MY_TARGET}/usr/lib32/ \
        ${MY_TARGET}/usr/lib64/ \
        ;
    do
        if [ -e "$x" ]; then LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${x}"; fi;
    done
    export LD_LIBRARY_PATH

    # Append target bin directories to the PATH as busybox may not have tee.
    PATH="${PATH}:${MY_TARGET}/bin:${MY_TARGET}/usr/bin:${MY_TARGET}/sbin:${MY_TARGET}/usr/sbin"
    export PATH

    # Drop the --need-target-bash argument and re-exec.
    shift
    echo "******************************************************************************" >> "${MY_LOGFILE}"
    echo "** Relaunching using ${MY_TARGET}/bin/bash $0 $*" >> "${MY_LOGFILE}"
    echo "**   LD_LIBRARY_PATH=${LD_LIBRARY_PATH}" >> "${MY_LOGFILE}"
    echo "**              PATH=${PATH}" >> "${MY_LOGFILE}"
    exec "${MY_TARGET}/bin/bash" "$0" "$@"
fi


#
# Commands.
#

# Logs execution of a command.
log_command()
{
    echo "--------------------------------------------------" >> "${MY_LOGFILE}"
    echo "** Date:      `date -R`" >> "${MY_LOGFILE}"
    echo "** Executing: $*" >> "${MY_LOGFILE}"
    "$@" 2>&1 | tee -a "${MY_LOGFILE}"
    MY_TMP_EXITCODE="${PIPESTATUS[0]}"
    if [ "${MY_TMP_EXITCODE}" != "0" ]; then
        if [ "${MY_TMP_EXITCODE}" != "${MY_IGNORE_EXITCODE}" ]; then
            echo "** exit code: ${MY_TMP_EXITCODE}" | tee -a "${MY_LOGFILE}"
            MY_EXITCODE=1;
        else
            echo "** exit code: ${MY_TMP_EXITCODE} (ignored)" | tee -a "${MY_LOGFILE}"
        fi
    fi
}

# Logs execution of a command inside the target.
log_command_in_target()
{
    #
    # We should be using in-target here, however we don't get any stderr output
    # from it because of log-output. We can get stdout by --pass-stdout, but
    # that's not helpful for failures.
    #
    # So, we try do the chroot prepping that in-target does at the start of the
    # script (see below) and just use chroot here.
    #
    log_command chroot "${MY_TARGET}" "$@"
    # log_command in-target --pass-stdout "$@" # No stderr output... :-(
}

# Checks if $1 is a command on the PATH inside the target jail.
chroot_which()
{
    for dir in /bin /usr/bin /sbin /usr/sbin;
    do
        if [ -x "${MY_TARGET}${dir}/$1" ]; then
            return 0;
        fi
    done
    return 1;
}

#
# Log header.
#
echo "******************************************************************************" >> "${MY_LOGFILE}"
echo "** VirtualBox Unattended Guest Installation - Late installation actions" >> "${MY_LOGFILE}"
echo "** Date:    `date -R`" >> "${MY_LOGFILE}"
echo "** Started: $0 $*" >> "${MY_LOGFILE}"


#
# Setup the target jail ourselves since in-target steals all the output.
#
if [ -f /lib/chroot-setup.sh ]; then
    MY_HAVE_CHROOT_SETUP="yes"
    . /lib/chroot-setup.sh
    if chroot_setup; then
        echo "** chroot_setup: done" | tee -a "${MY_LOGFILE}"
    else
        echo "** chroot_setup: failed $?" | tee -a "${MY_LOGFILE}"
    fi
else
    MY_HAVE_CHROOT_SETUP=""
fi


#
# We want the ISO available inside the target jail.
#
if [ -d "${MY_TARGET}${MY_CHROOT_CDROM}" ]; then
    MY_RMDIR_TARGET_CDROM=
else
    MY_RMDIR_TARGET_CDROM="yes"
    log_command mkdir -p ${MY_TARGET}${MY_CHROOT_CDROM}
fi

if [ -f "${MY_TARGET}${MY_CHROOT_CDROM}/vboxpostinstall.sh" ]; then
    MY_UNMOUNT_TARGET_CDROM=
    echo "** binding cdrom into jail: already done" | tee -a "${MY_LOGFILE}"
else
    MY_UNMOUNT_TARGET_CDROM="yes"
    log_command mount -o bind "${MY_CDROM_NOCHROOT}" "${MY_TARGET}${MY_CHROOT_CDROM}"
    if [ -f "${MY_TARGET}${MY_CHROOT_CDROM}/vboxpostinstall.sh" ]; then
        echo "** binding cdrom into jail: success"  | tee -a "${MY_LOGFILE}"
    else
        echo "** binding cdrom into jail: failed"   | tee -a "${MY_LOGFILE}"
    fi
    if [ "${MY_DEBUG}" = "yes" ]; then
        log_command find "${MY_TARGET}${MY_CHROOT_CDROM}"
    fi
fi


#
# Debug
#
if [ "${MY_DEBUG}" = "yes" ]; then
    log_command id
    log_command ps
    log_command ps auxwwwf
    log_command env
    log_command df
    log_command mount
    log_command_in_target df
    log_command_in_target mount
    #log_command find /
    MY_EXITCODE=0
fi


#
# GAs
#
@@VBOX_COND_IS_INSTALLING_ADDITIONS@@
#
# Packages needed for GAs.
#
echo "--------------------------------------------------" >> "${MY_LOGFILE}"
echo '** Installing packages for building kernel modules...' | tee -a "${MY_LOGFILE}"
log_command_in_target apt-get -y install build-essential
log_command_in_target apt-get -y install linux-headers-$(uname -r)

echo "--------------------------------------------------" >> "${MY_LOGFILE}"
echo '** Installing VirtualBox Guest Additions...' | tee -a "${MY_LOGFILE}"
MY_IGNORE_EXITCODE=2  # returned if modules already loaded and reboot required.
log_command_in_target /bin/bash "${MY_CHROOT_CDROM}/vboxadditions/@@VBOX_INSERT_ADDITIONS_INSTALL_PACKAGE_NAME@@" --nox11
log_command_in_target /bin/bash -c "udevadm control --reload-rules" # GAs doesn't yet do this.
log_command_in_target /bin/bash -c "udevadm trigger"                 # (ditto)
MY_IGNORE_EXITCODE=
# Currently in Ubuntu, the vboxsf group does not exist when this script executes.
# But if it does in the future, the --force option will ensure the command
# still succeeds.
log_command_in_target groupadd --force --system vboxsf
log_command_in_target usermod -a -G vboxsf "@@VBOX_INSERT_USER_LOGIN@@"
@@VBOX_COND_END@@


#
# Test Execution Service.
#
@@VBOX_COND_IS_INSTALLING_TEST_EXEC_SERVICE@@
echo "--------------------------------------------------" >> "${MY_LOGFILE}"
echo '** Installing Test Execution Service...' | tee -a "${MY_LOGFILE}"
log_command_in_target test "${MY_CHROOT_CDROM}/vboxvalidationkit/linux/@@VBOX_INSERT_OS_ARCH@@/TestExecService"
log_command mkdir -p "${MY_TARGET}/opt/validationkit" "${MY_TARGET}/media/cdrom"
log_command cp -R ${MY_CDROM_NOCHROOT}/vboxvalidationkit/* "${MY_TARGET}/opt/validationkit/"
log_command chmod -R u+rw,a+xr "${MY_TARGET}/opt/validationkit/"
if [ -e "${MY_TARGET}/usr/bin/chcon" -o -e "${MY_TARGET}/bin/chcon" -o -e "${MY_TARGET}/usr/sbin/chcon" -o -e "${MY_TARGET}/sbin/chcon" ]; then
    MY_IGNORE_EXITCODE=1
    log_command_in_target chcon -R -t usr_t "/opt/validationkit/"
    MY_IGNORE_EXITCODE=
fi

# systemd service config:
MY_UNIT_PATH="${MY_TARGET}/lib/systemd/system"
test -d "${MY_TARGET}/usr/lib/systemd/system" && MY_UNIT_PATH="${MY_TARGET}/usr/lib/systemd/system"
if [ -d "${MY_UNIT_PATH}" ]; then
    log_command cp "${MY_TARGET}/opt/validationkit/linux/vboxtxs.service" "${MY_UNIT_PATH}/vboxtxs.service"
    log_command chmod 644 "${MY_UNIT_PATH}/vboxtxs.service"
    log_command_in_target systemctl -q enable vboxtxs

# System V like:
elif [ -e "${MY_TARGET}/etc/init.d/" ]; then

    # Install the script.  On rhel6 scripts are under /etc/rc.d/ with /etc/init.d and /etc/rc?.d being symlinks.
    if [ -d "${MY_TARGET}/etc/rc.d/init.d/" ]; then
        MY_INIT_D_PARENT_PATH="${MY_TARGET}/etc/rc.d"
        log_command ln -s "../../../opt/validationkit/linux/vboxtxs" "${MY_INIT_D_PARENT_PATH}/init.d/"
    else
        MY_INIT_D_PARENT_PATH="${MY_TARGET}/etc"
        log_command ln -s    "../../opt/validationkit/linux/vboxtxs" "${MY_INIT_D_PARENT_PATH}/init.d/"
    fi

    # Use runlevel management script if found.
    if chroot_which chkconfig; then     # Redhat based sysvinit systems
        log_command_in_target chkconfig --add vboxtxs
    elif chroot_which insserv; then     # SUSE-based sysvinit systems
        log_command_in_target insserv vboxtxs
    elif chroot_which update-rc.d; then # Debian/Ubuntu-based systems
        log_command_in_target update-rc.d vboxtxs defaults
    elif chroot_which rc-update; then   # Gentoo Linux
        log_command_in_target rc-update add vboxtxs default
    # Fall back on hardcoded symlinking.
    else
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc0.d/K65vboxtxs"
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc1.d/K65vboxtxs"
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc6.d/K65vboxtxs"
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc2.d/S35vboxtxs"
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc3.d/S35vboxtxs"
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc4.d/S35vboxtxs"
        log_command ln -s "../init.d/vboxtxs" "${MY_INIT_D_PARENT_PATH}/rc5.d/S35vboxtxs"
    fi
else
    echo "** error: Unknown init script system." | tee -a "${MY_LOGFILE}"
fi

@@VBOX_COND_END@@

#
# Run user command.
#
@@VBOX_COND_HAS_POST_INSTALL_COMMAND@@
echo '** Running custom user command ...'      | tee -a "${MY_LOGFILE}"
log_command @@VBOX_INSERT_POST_INSTALL_COMMAND@@
@@VBOX_COND_END@@


#
# Unmount the cdrom if we bound it and clean up the chroot if we set it up.
#
if [ -n "${MY_UNMOUNT_TARGET_CDROM}" ]; then
    echo "** unbinding cdrom from jail..." | tee -a "${MY_LOGFILE}"
    log_command umount "${MY_TARGET}${MY_CHROOT_CDROM}"
fi

if [ -n "${MY_RMDIR_TARGET_CDROM}" ]; then
    log_command rmdir "${MY_TARGET}${MY_CHROOT_CDROM}"
fi

if [ -n "${MY_HAVE_CHROOT_SETUP}" ]; then
    if chroot_cleanup; then
        echo "** chroot_cleanup: done"      | tee -a "${MY_LOGFILE}"
    else
        echo "** chroot_cleanup: failed $?" | tee -a "${MY_LOGFILE}"
    fi
fi


#
# Log footer.
#
echo "******************************************************************************" >> "${MY_LOGFILE}"
echo "** Date:            `date -R`" >> "${MY_LOGFILE}"
echo "** Final exit code: ${MY_EXITCODE}" >> "${MY_LOGFILE}"
echo "******************************************************************************" >> "${MY_LOGFILE}"

exit ${MY_EXITCODE}

