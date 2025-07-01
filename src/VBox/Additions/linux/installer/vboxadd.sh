#! /bin/sh
# $Id: vboxadd.sh 110053 2025-07-01 06:30:25Z vadim.galitsyn@oracle.com $
## @file
# Linux Additions kernel module init script ($Revision: 110053 $)
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
# SPDX-License-Identifier: GPL-3.0-only
#

# X-Start-Before is a Debian Addition which we use when converting to
# a systemd unit.  X-Service-Type is our own invention, also for systemd.

# chkconfig: 345 10 90
# description: VirtualBox Linux Additions kernel modules
#
### BEGIN INIT INFO
# Provides:       vboxadd
# Required-Start:
# Required-Stop:
# Default-Start:  2 3 4 5
# Default-Stop:   0 1 6
# X-Start-Before: display-manager
# X-Service-Type: oneshot
# Description:    VirtualBox Linux Additions kernel modules
### END INIT INFO

## @todo This file duplicates a lot of script with vboxdrv.sh.  When making
# changes please try to reduce differences between the two wherever possible.

# Testing:
# * Should fail if the configuration file is missing or missing INSTALL_DIR or
#   INSTALL_VER entries.
# * vboxadd, vboxsf and vboxdrmipc user groups should be created if they do not exist - test
#   by removing them before installing.
# * Shared folders can be mounted and auto-mounts accessible to vboxsf group,
#   including on recent Fedoras with SELinux.
# * Setting INSTALL_NO_MODULE_BUILDS inhibits modules and module automatic
#   rebuild script creation; otherwise modules, user, group, rebuild script,
#   udev rule and shared folder mount helper should be created/set up.
# * Setting INSTALL_NO_MODULE_BUILDS inhibits module load and unload on start
#   and stop.
# * Uninstalling the Additions and re-installing them does not trigger warnings.

export LC_ALL=C
PATH=$PATH:/bin:/sbin:/usr/sbin
PACKAGE=VBoxGuestAdditions
MODPROBE=/sbin/modprobe
OLDMODULES="vboxguest vboxadd vboxsf vboxvfs vboxvideo"
SERVICE="VirtualBox Guest Additions"
VBOXSERVICE_PIDFILE="/var/run/vboxadd-service.sh"
## systemd logs information about service status, otherwise do that ourselves.
QUIET=
test -z "${TARGET_VER}" && TARGET_VER=`uname -r`

export VBOX_KBUILD_TYPE
export USERNAME

setup_log()
{
    test -z "${LOG}" || return 0
    # Rotate log files
    LOG="/var/log/vboxadd-setup.log"
    mv -f "${LOG}.3" "${LOG}.4" 2>/dev/null
    mv -f "${LOG}.2" "${LOG}.3" 2>/dev/null
    mv -f "${LOG}.1" "${LOG}.2" 2>/dev/null
    mv -f "${LOG}" "${LOG}.1" 2>/dev/null
}

if $MODPROBE -c 2>/dev/null | grep -q '^allow_unsupported_modules  *0'; then
  MODPROBE="$MODPROBE --allow-unsupported-modules"
fi

# Preamble for Gentoo
if [ "`which $0`" = "/sbin/rc" ]; then
    shift
fi

begin()
{
    test -n "${QUIET}" || echo "${SERVICE}: ${1}"
}

info()
{
    if test -z "${QUIET}"; then
        echo "${SERVICE}: $1" | fold -s
    else
        echo "$1" | fold -s
    fi
}

# When script is running as non-root, it does not have access to log
# files in /var. In this case, lets print error message to stdout and
# exit with bad status.
early_fail()
{
    echo "$1" >&2
    exit 1
}

fail()
{
    log "${1}"
    echo "${SERVICE}: $1" >&2
    echo "The log file $LOG may contain further information." >&2
    exit 1
}

log()
{
    setup_log
    echo "${1}" >> "${LOG}"
}

module_build_log()
{
    log "Error building the module.  Build output follows."
    echo ""
    echo "${1}" >> "${LOG}"
}

dev=/dev/vboxguest
userdev=/dev/vboxuser
config=/var/lib/VBoxGuestAdditions/config
user_config=/etc/virtualbox-guest-additions.conf
owner=vboxadd
group=1

# Include custom user configuration file.
[ -r "$user_config" ] && . "$user_config"

if test -r $config; then
  . $config
else
  fail "Configuration file $config not found"
fi
test -n "$INSTALL_DIR" -a -n "$INSTALL_VER" ||
  fail "Configuration file $config not complete"
MODULE_SRC="$INSTALL_DIR/src/vboxguest-$INSTALL_VER"
BUILDINTMP="$MODULE_SRC/build_in_tmp"

# Path to VBoxService control script.
VBOX_SERVICE_SCRIPT="/sbin/rcvboxadd-service"

# Attempt to detect VirtualBox Guest Additions version and revision information.
VBOXCONTROL="${INSTALL_DIR}/bin/VBoxControl"
VBOX_VERSION="`"$VBOXCONTROL" --version | cut -d r -f1`"
[ -n "$VBOX_VERSION" ] || VBOX_VERSION='unknown'
VBOX_REVISION="r`"$VBOXCONTROL" --version | cut -d r -f2`"
[ "$VBOX_REVISION" != "r" ] || VBOX_REVISION='unknown'

# Returns if a specific module is running or not.
#
# Input $1: Module name to check running status for.
#
# Returns true if the module is running, false if not.
running_module()
{
    mod="$1"
    [ -d "/sys/module/$mod" ]
}

# Returns the version string of a currently running kernel module.
#
# Input $1: Module name to check.
#
# Returns the module version string if found, or none if not found.
running_module_version()
{
    mod="$1"
    version_string_path="/sys/module/$mod/version"

    [ -n "$mod" ] || return
    if [ -r "$version_string_path" ]; then
        cat "$version_string_path"
    else
        echo "unknown"
    fi
}

# Checks if a loaded kernel module version matches to the currently installed Guest Additions version and revision.
#
# Input $1: Module name to check.
#
# Returns "1" if the module matches the installed Guest Additions, or none if not.
check_running_module_version()
{
    mod=$1
    expected="$VBOX_VERSION $VBOX_REVISION"

    [ -n "$mod" ] || return
    [ -n "$expected" ] || return

    [ "$expected" = "$(running_module_version "$mod")" ] || return
}

do_vboxguest_non_udev()
{
    if [ ! -c $dev ]; then
        maj=`sed -n 's;\([0-9]\+\) vboxguest;\1;p' /proc/devices`
        if [ ! -z "$maj" ]; then
            min=0
        else
            min=`sed -n 's;\([0-9]\+\) vboxguest;\1;p' /proc/misc`
            if [ ! -z "$min" ]; then
                maj=10
            fi
        fi
        test -n "$maj" || {
            rmmod vboxguest 2>/dev/null
            fail "Cannot locate the VirtualBox device"
        }

        mknod -m 0664 $dev c $maj $min || {
            rmmod vboxguest 2>/dev/null
            fail "Cannot create device $dev with major $maj and minor $min"
        }
    fi
    chown $owner:$group $dev 2>/dev/null || {
        rm -f $dev 2>/dev/null
        rm -f $userdev 2>/dev/null
        rmmod vboxguest 2>/dev/null
        fail "Cannot change owner $owner:$group for device $dev"
    }

    if [ ! -c $userdev ]; then
        maj=10
        min=`sed -n 's;\([0-9]\+\) vboxuser;\1;p' /proc/misc`
        if [ ! -z "$min" ]; then
            mknod -m 0666 $userdev c $maj $min || {
                rm -f $dev 2>/dev/null
                rmmod vboxguest 2>/dev/null
                fail "Cannot create device $userdev with major $maj and minor $min"
            }
            chown $owner:$group $userdev 2>/dev/null || {
                rm -f $dev 2>/dev/null
                rm -f $userdev 2>/dev/null
                rmmod vboxguest 2>/dev/null
                fail "Cannot change owner $owner:$group for device $userdev"
            }
        fi
    fi
}

restart()
{
    stop && start
    return 0
}

## Updates the initramfs.  Debian and Ubuntu put the graphics driver in, and
# need the touch(1) command below.  Everyone else that I checked just need
# the right module alias file from depmod(1) and only use the initramfs to
# load the root filesystem, not the boot splash.  update-initramfs works
# for the first two and dracut for every one else I checked.  We are only
# interested in distributions recent enough to use the KMS vboxvideo driver.
update_initramfs()
{
    ## kernel version to update for.
    version="${1}"
    depmod "${version}"
    rm -f "/lib/modules/${version}/initrd/vboxvideo"
    test ! -d "/lib/modules/${version}/initrd" ||
        test ! -f "/lib/modules/${version}/misc/vboxvideo.ko" ||
        touch "/lib/modules/${version}/initrd/vboxvideo"

    # Systems without systemd-inhibit probably don't need their initramfs
    # rebuild here anyway.
    type systemd-inhibit >/dev/null 2>&1 || return
    if type dracut >/dev/null 2>&1; then
        systemd-inhibit --why="Installing VirtualBox Guest Additions" \
            dracut -f --kver "${version}"
    elif type update-initramfs >/dev/null 2>&1; then
        systemd-inhibit --why="Installing VirtualBox Guest Additions" \
            update-initramfs -u -k "${version}"
    fi
}

# Removes any existing VirtualBox guest kernel modules from the disk, but not
# from the kernel as they may still be in use
cleanup_modules()
{
    # Needed for Ubuntu and Debian, see update_initramfs
    rm -f /lib/modules/*/initrd/vboxvideo
    for i in /lib/modules/*/misc; do
        KERN_VER="${i%/misc}"
        KERN_VER="${KERN_VER#/lib/modules/}"
        unset do_update
        for j in ${OLDMODULES}; do
            for mod_ext in ko ko.gz ko.xz ko.zst; do
                test -f "${i}/${j}.${mod_ext}" && do_update=1 && rm -f "${i}/${j}.${mod_ext}"
            done
        done
        test -z "$do_update" || update_initramfs "$KERN_VER"
        # Remove empty /lib/modules folders which may have been kept around
        rmdir -p "${i}" 2>/dev/null || true
        unset keep
        for j in /lib/modules/"${KERN_VER}"/*; do
            name="${j##*/}"
            test -d "${name}" || test "${name%%.*}" != modules && keep=1
        done
        if test -z "${keep}"; then
            rm -rf /lib/modules/"${KERN_VER}"
            rm -f /boot/initrd.img-"${KERN_VER}"
        fi
    done
    for i in ${OLDMODULES}; do
        # We no longer support DKMS, remove any leftovers.
        rm -rf "/var/lib/dkms/${i}"*
    done
    rm -f /etc/depmod.d/vboxvideo-upstream.conf
}

# Secure boot state.
case "`mokutil --sb-state 2>/dev/null`" in
    *"disabled in shim"*) unset HAVE_SEC_BOOT;;
    *"SecureBoot enabled"*) HAVE_SEC_BOOT=true;;
    *) unset HAVE_SEC_BOOT;;
esac
# So far we can only sign modules on Ubuntu and on Debian 10 and later.
DEB_PUB_KEY=/var/lib/shim-signed/mok/MOK.der
DEB_PRIV_KEY=/var/lib/shim-signed/mok/MOK.priv
# Check if key already enrolled.
unset HAVE_DEB_KEY
case "`mokutil --test-key "$DEB_PUB_KEY" 2>/dev/null`" in
    *"is already"*) DEB_KEY_ENROLLED=true;;
    *) unset DEB_KEY_ENROLLED;;
esac

# Checks if update-secureboot-policy tool supports required commandline options.
update_secureboot_policy_supports()
{
    opt_name="$1"
    [ -n "$opt_name" ] || return

    [ -z "$(update-secureboot-policy --help 2>&1 | grep "$opt_name")" ] && return
    echo "1"
}

HAVE_UPDATE_SECUREBOOT_POLICY_TOOL=
if type update-secureboot-policy >/dev/null 2>&1; then
    [ "$(update_secureboot_policy_supports new-key)" = "1" -a "$(update_secureboot_policy_supports enroll-key)" = "1" ] && \
        HAVE_UPDATE_SECUREBOOT_POLICY_TOOL=true
fi

# Reads kernel configuration option.
kernel_get_config_opt()
{
    kern_ver="$1"
    opt_name="$2"

    [ -n "$kern_ver" ] || return
    [ -n "$opt_name" ] || return

    # Check if there is a kernel tool which can extract config option.
    if test -x /lib/modules/"$kern_ver"/build/scripts/config; then
        /lib/modules/"$kern_ver"/build/scripts/config \
            --file /lib/modules/"$kern_ver"/build/.config \
            --state "$opt_name" 2>/dev/null
    elif test -f /lib/modules/"$kern_ver"/build/.config; then
        # Extract config option manually.
        grep "$opt_name=" /lib/modules/"$kern_ver"/build/.config | sed -e "s/^$opt_name=//" -e "s/\"//g"
    fi
}

# Reads CONFIG_MODULE_SIG_HASH from kernel config.
kernel_module_sig_hash()
{
    kern_ver="$1"
    [ -n "$kern_ver" ] || return

    kernel_get_config_opt "$kern_ver" "CONFIG_MODULE_SIG_HASH"
}

# Returns "1" if kernel module signature hash algorithm
# is supported by us. Or empty string otherwise.
module_sig_hash_supported()
{
    sig_hashalgo="$1"
    [ -n "$sig_hashalgo" ] || return

    # Go through supported list.
    [    "$sig_hashalgo" = "sha1"   \
      -o "$sig_hashalgo" = "sha224" \
      -o "$sig_hashalgo" = "sha256" \
      -o "$sig_hashalgo" = "sha384" \
      -o "$sig_hashalgo" = "sha512" ] || return

    echo "1"
}

# Check if kernel configuration requires modules signature.
kernel_requires_module_signature()
{
    kern_ver="$1"
    vbox_sys_lockdown_path="/sys/kernel/security/lockdown"

    [ -n "$kern_ver" ] || return

    requires=""
    # We consider that if kernel is running in the following configurations,
    # it will require modules to be signed.
    if [ "$(kernel_get_config_opt "$kern_ver" "CONFIG_MODULE_SIG")" = "y" ]; then

        # Modules signature verification is hardcoded in kernel config.
        [ "$(kernel_get_config_opt "$kern_ver" "CONFIG_MODULE_SIG_FORCE")" = "y" ] && requires="1"

        # Unsigned modules loading is restricted by "lockdown" feature in runtime.
        if [   "$(kernel_get_config_opt "$kern_ver" "CONFIG_LOCK_DOWN_KERNEL")" = "y" \
            -o "$(kernel_get_config_opt "$kern_ver" "CONFIG_SECURITY_LOCKDOWN_LSM")" = "y" \
            -o "$(kernel_get_config_opt "$kern_ver" "CONFIG_SECURITY_LOCKDOWN_LSM_EARLY")" = "y" ]; then

            # Once lockdown level is set to something different from "none" (e.g., "integrity"
            # or "confidentiality"), kernel will reject unsigned modules loading.
            if [ -r "$vbox_sys_lockdown_path" ]; then
                [ -n "$(cat "$vbox_sys_lockdown_path" | grep "\[integrity\]")" ] && requires="1"
                [ -n "$(cat "$vbox_sys_lockdown_path" | grep "\[confidentiality\]")" ] && requires="1"
            fi

            # This configuration is used by a number of modern Linux distributions and restricts
            # unsigned modules loading when Secure Boot mode is enabled.
            [ "$(kernel_get_config_opt "$kern_ver" "CONFIG_LOCK_DOWN_IN_EFI_SECURE_BOOT")" = "y" -a -n "$HAVE_SEC_BOOT" ] && requires="1"
        fi
    fi

    [ -n "$requires" ] && echo "1"
}

sign_modules()
{
    KERN_VER="$1"
    test -n "$KERN_VER" || return 1

    # Make list of mudules to sign.
    MODULE_LIST="vboxguest vboxsf"
    # vboxvideo might not present on for older kernels.
    [ -f "/lib/modules/"$KERN_VER"/misc/vboxvideo.ko" ] && MODULE_LIST="$MODULE_LIST vboxvideo"

    # Sign kernel modules if kernel configuration requires it.
    if test "$(kernel_requires_module_signature $KERN_VER)" = "1"; then
        begin "Signing VirtualBox Guest Additions kernel modules"

        # Generate new signing key if needed.
        [ -n "$HAVE_UPDATE_SECUREBOOT_POLICY_TOOL" ] && SHIM_NOTRIGGER=y update-secureboot-policy --new-key

        # Check if signing keys are in place.
        if test ! -f "$DEB_PUB_KEY" || ! test -f "$DEB_PRIV_KEY"; then
            # update-secureboot-policy tool present in the system, but keys were not generated.
            [ -n "$HAVE_UPDATE_SECUREBOOT_POLICY_TOOL" ] && info "

update-secureboot-policy tool does not generate signing keys
in your distribution, see below on how to generate them manually."
            # update-secureboot-policy not present in the system, recommend generate keys manually.
            fail "

System is running in Secure Boot mode, however your distribution
does not provide tools for automatic generation of keys needed for
modules signing. Please consider to generate and enroll them manually:

    sudo mkdir -p /var/lib/shim-signed/mok
    sudo openssl req -nodes -new -x509 -newkey rsa:2048 -outform DER -addext \"extendedKeyUsage=codeSigning\" -keyout $DEB_PRIV_KEY -out $DEB_PUB_KEY
    sudo mokutil --import $DEB_PUB_KEY
    sudo reboot

Restart \"rcvboxadd setup\" after system is rebooted.
"
        fi

        # Get kernel signature hash algorithm from kernel config and validate it.
        sig_hashalgo=$(kernel_module_sig_hash "$KERN_VER")
        [ "$(module_sig_hash_supported $sig_hashalgo)" = "1" ] \
            || fail "Unsupported kernel signature hash algorithm $sig_hashalgo"

        # Sign modules.
        for i in $MODULE_LIST; do

            # Try to find a tool for modules signing.
            SIGN_TOOL=$(which kmodsign 2>/dev/null)
            # Attempt to use in-kernel signing tool if kmodsign not found.
            if test -z "$SIGN_TOOL"; then
                if test -x "/lib/modules/$KERN_VER/build/scripts/sign-file"; then
                    SIGN_TOOL="/lib/modules/$KERN_VER/build/scripts/sign-file"
                fi
            fi

            # Check if signing tool is available.
            [ -n "$SIGN_TOOL" ] || fail "Unable to find signing tool"

            "$SIGN_TOOL" "$sig_hashalgo" "$DEB_PRIV_KEY" "$DEB_PUB_KEY" \
                /lib/modules/"$KERN_VER"/misc/"$i".ko || fail "Unable to sign $i.ko"
        done
        # Enroll signing key if needed.
        if test -n "$HAVE_UPDATE_SECUREBOOT_POLICY_TOOL"; then
            # update-secureboot-policy "expects" DKMS modules.
            # Work around this and talk to the authors as soon
            # as possible to fix it.
            mkdir -p /var/lib/dkms/vbox-temp
            update-secureboot-policy --enroll-key 2>/dev/null ||
                fail "Failed to enroll secure boot key."
            rmdir -p /var/lib/dkms/vbox-temp 2>/dev/null

            # Indicate that key has been enrolled and reboot is needed.
            HAVE_DEB_KEY=true
        fi
    fi
}

# Build and install the VirtualBox guest kernel modules
setup_modules()
{
    KERN_VER="$1"
    test -n "$KERN_VER" || return 1
    # Match (at least): vboxguest.o; vboxguest.ko; vboxguest.ko.xz
    set /lib/modules/"$KERN_VER"/misc/vboxguest.*o*
    #test ! -f "$1" || return 0
    test -d /lib/modules/"$KERN_VER"/build || return 0
    export KERN_VER
    info "Building the modules for kernel $KERN_VER."

    # Prepend PATH for building UEK on OL8/9 distributions.
    case "$KERN_VER" in
        5.15.0-*.el8uek*) PATH="/opt/rh/gcc-toolset-11/root/usr/bin:$PATH";;
        6.12.0-*.el9uek*)PATH="/opt/rh/gcc-toolset-14/root/usr/bin:$PATH";;
    esac

    # Detect if kernel was built with clang.
    unset LLVM
    vbox_cc_is_clang=$(kernel_get_config_opt "$KERN_VER" "CONFIG_CC_IS_CLANG")
    if test "${vbox_cc_is_clang}" = "y"; then
        info "Using clang compiler."
        export LLVM=1
    fi

    log "Building the main Guest Additions $INSTALL_VER module for kernel $KERN_VER."
    if ! myerr=`$BUILDINTMP \
        --save-module-symvers /tmp/vboxguest-Module.symvers \
        --module-source $MODULE_SRC/vboxguest \
        --no-print-directory install 2>&1`; then
        # If check_module_dependencies.sh fails it prints a message itself.
        module_build_log "$myerr"
        "${INSTALL_DIR}"/other/check_module_dependencies.sh 2>&1 &&
            info "Look at $LOG to find out what went wrong"
        return 0
    fi
    log "Building the shared folder support module."
    if ! myerr=`$BUILDINTMP \
        --use-module-symvers /tmp/vboxguest-Module.symvers \
        --module-source $MODULE_SRC/vboxsf \
        --no-print-directory install 2>&1`; then
        module_build_log "$myerr"
        info  "Look at $LOG to find out what went wrong"
        return 0
    fi
    log "Building the graphics driver module."
    if ! myerr=`$BUILDINTMP \
        --use-module-symvers /tmp/vboxguest-Module.symvers \
        --module-source $MODULE_SRC/vboxvideo \
        --no-print-directory install 2>&1`; then
        module_build_log "$myerr"
        info "Look at $LOG to find out what went wrong"
    fi
    [ -d /etc/depmod.d ] || mkdir /etc/depmod.d
    echo "override vboxguest * misc" > /etc/depmod.d/vboxvideo-upstream.conf
    echo "override vboxsf * misc" >> /etc/depmod.d/vboxvideo-upstream.conf
    echo "override vboxvideo * misc" >> /etc/depmod.d/vboxvideo-upstream.conf

    sign_modules "${KERN_VER}"

    update_initramfs "${KERN_VER}"

    return 0
}

create_vbox_user()
{
    # This is the LSB version of useradd and should work on recent
    # distributions
    useradd -d /var/run/vboxadd -g 1 -r -s /bin/false vboxadd >/dev/null 2>&1 || true
    # And for the others, we choose a UID ourselves
    useradd -d /var/run/vboxadd -g 1 -u 501 -o -s /bin/false vboxadd >/dev/null 2>&1 || true

}

create_udev_rule()
{
    # Create udev description file
    if [ -d /etc/udev/rules.d ]; then
        udev_call=""
        udev_app=`which udevadm 2> /dev/null`
        if [ $? -eq 0 ]; then
            udev_call="${udev_app} version 2> /dev/null"
        else
            udev_app=`which udevinfo 2> /dev/null`
            if [ $? -eq 0 ]; then
                udev_call="${udev_app} -V 2> /dev/null"
            fi
        fi
        udev_fix="="
        if [ "${udev_call}" != "" ]; then
            udev_out=`${udev_call}`
            udev_ver=`expr "$udev_out" : '[^0-9]*\([0-9]*\)'`
            if [ "$udev_ver" = "" -o "$udev_ver" -lt 55 ]; then
               udev_fix=""
            fi
        fi
        ## @todo 60-vboxadd.rules -> 60-vboxguest.rules ?
        echo "KERNEL=${udev_fix}\"vboxguest\", NAME=\"vboxguest\", OWNER=\"vboxadd\", MODE=\"0660\"" > /etc/udev/rules.d/60-vboxadd.rules
        echo "KERNEL=${udev_fix}\"vboxuser\", NAME=\"vboxuser\", OWNER=\"vboxadd\", MODE=\"0666\"" >> /etc/udev/rules.d/60-vboxadd.rules
        # Make sure the new rule is noticed.
        udevadm control --reload >/dev/null 2>&1 || true
        udevcontrol reload_rules >/dev/null 2>&1 || true
    fi
}

create_module_rebuild_script()
{
    # And a post-installation script for rebuilding modules when a new kernel
    # is installed.
    mkdir -p /etc/kernel/postinst.d /etc/kernel/prerm.d
    cat << EOF > /etc/kernel/postinst.d/vboxadd
#!/bin/sh
# This only works correctly on Debian derivatives - Red Hat calls it before
# installing the right header files.
/sbin/rcvboxadd quicksetup "\${1}"
exit 0
EOF
    cat << EOF > /etc/kernel/prerm.d/vboxadd
#!/bin/sh
for i in ${OLDMODULES}; do rm -f /lib/modules/"\${1}"/misc/"\${i}".ko; done
rmdir -p /lib/modules/"\$1"/misc 2>/dev/null || true
exit 0
EOF
    chmod 0755 /etc/kernel/postinst.d/vboxadd /etc/kernel/prerm.d/vboxadd
}

shared_folder_setup()
{
    # Add a group "vboxsf" for Shared Folders access
    # All users which want to access the auto-mounted Shared Folders have to
    # be added to this group.
    groupadd -r -f vboxsf >/dev/null 2>&1

    # Put the mount.vboxsf mount helper in the right place.
    ## @todo It would be nicer if the kernel module just parsed parameters
    # itself instead of needing a separate binary to do that.
    ln -sf "${INSTALL_DIR}/other/mount.vboxsf" /sbin
    # SELinux security context for the mount helper.
    if test -e /etc/selinux/config; then
        # This is correct.  semanage maps this to the real path, and it aborts
        # with an error, telling you what you should have typed, if you specify
        # the real path.  The "chcon" is there as a back-up for old guests.
        command -v semanage > /dev/null &&
            semanage fcontext -a -t mount_exec_t "${INSTALL_DIR}/other/mount.vboxsf"
        chcon -t mount_exec_t "${INSTALL_DIR}/other/mount.vboxsf" 2>/dev/null
    fi
}

# Returns path to a module file as seen by modinfo(8), or none if not found.
#
# Input $1: Module name to get path for.
#
# Returns the module path as a string.
module_path()
{
    mod="$1"
    [ -n "$mod" ] || return

    modinfo "$mod" 2>/dev/null | grep -e "^filename:" | tr -s ' ' | cut -d " " -f2
}

# Returns module version if module is available, or none if not found.
#
# Input $1: Module name to get version for.
#
# Returns the module version as a string.
module_version()
{
    mod="$1"
    [ -n "$mod" ] || return

    modinfo "$mod" 2>/dev/null | grep -e "^version:" | tr -s ' ' | cut -d " " -f2
}

# Returns the module revision if module is available in the system, or none if not found.
#
# Input $1: Module name to get revision for.
#
# Returns the module revision as a string.
module_revision()
{
    mod="$1"
    [ -n "$mod" ] || return

    modinfo "$mod" 2>/dev/null | grep -e "^version:" | tr -s ' ' | cut -d " " -f3
}

# Checks if a given kernel module is properly signed or not.
#
# Input $1: Module name to check.
#
# Returns "1" if module is signed and signature can be verified
# with public key provided in DEB_PUB_KEY, or none otherwise.
module_signed()
{
    mod="$1"
    [ -n "$mod" ] || return

    # Be nice with distributions which do not provide tools which we
    # use in order to verify module signature. This variable needs to
    # be explicitly set by administrator. This script will look for it
    # in /etc/virtualbox-guest-additions.conf. Make sure that you know
    # what you do!
    if [ "$VBOX_BYPASS_MODULES_SIGNATURE_CHECK" = "1" ]; then
        echo "1"
        return
    fi

    extraction_tool=/lib/modules/"$(uname -r)"/build/scripts/extract-module-sig.pl
    mod_path=$(module_path "$mod" 2>/dev/null)
    openssl_tool=$(which openssl 2>/dev/null)
    # Do not use built-in printf!
    printf_tool=$(which printf 2>/dev/null)

    # Make sure all the tools required for signature validation are available.
    [ -x "$extraction_tool" ] || return
    [ -n "$mod_path"        ] || return
    [ -n "$openssl_tool"    ] || return
    [ -n "$printf_tool"     ] || return

    # Make sure openssl can handle hash algorithm.
    sig_hashalgo=$(modinfo -F sig_hashalgo "$mod" 2>/dev/null)
    [ "$(module_sig_hash_supported $sig_hashalgo)" = "1" ] || return

    # Generate file names for temporary stuff.
    mod_pub_key=$(mktemp -u)
    mod_signature=$(mktemp -u)
    mod_unsigned=$(mktemp -u)

    # Convert public key in DER format into X509 certificate form.
    "$openssl_tool" x509 -pubkey -inform DER -in "$DEB_PUB_KEY" -out "$mod_pub_key" 2>/dev/null
    # Extract raw module signature and convert it into binary format.
    "$printf_tool" \\x$(modinfo -F signature "$mod" | sed -z 's/[ \t\n]//g' | sed -e "s/:/\\\x/g") 2>/dev/null > "$mod_signature"
    # Extract unsigned module for further digest calculation.
    "$extraction_tool" -0 "$mod_path" 2>/dev/null > "$mod_unsigned"

    # Verify signature.
    rc=""
    "$openssl_tool" dgst "-$sig_hashalgo" -binary -verify "$mod_pub_key" -signature "$mod_signature" "$mod_unsigned" 2>&1 >/dev/null && rc="1"
    # Clean up.
    rm -f $mod_pub_key $mod_signature $mod_unsigned

    # Check result.
    [ "$rc" = "1" ] || return

    echo "1"
}

# Checks if a given kernel module matches the installed VirtualBox Guest Additions version.
#
# Input $1: Module name to check.
#
# Returns "1" if externally built module is available in the system and its
# version and revision number do match to current VirtualBox installation.
# None otherwise.
module_available()
{
    mod="$1"
    [ -n "$mod" ] || return

    [ "$VBOX_VERSION" = "$(module_version "$mod")" ] || return
    [ "$VBOX_REVISION" = "$(module_revision "$mod")" ] || return

    # Check if module belongs to VirtualBox installation.
    #
    # We have a convention that only modules from /lib/modules/*/misc
    # belong to us. Modules from other locations are treated as
    # externally built.
    mod_path="$(module_path "$mod")"

    # If module path points to a symbolic link, resolve actual file location.
    [ -L "$mod_path" ] && mod_path="$(readlink -e -- "$mod_path")"

    # File exists?
    [ -f "$mod_path" ] || return

    # Extract last component of module path and check whether it is located
    # outside of /lib/modules/*/misc.
    mod_dir="$(dirname "$mod_path" | sed 's;^.*/;;')"
    [ "$mod_dir" = "misc" ] || return

    # In case if kernel configuration (for currently loaded kernel) requires
    # module signature, check if module is signed.
    if test "$(kernel_requires_module_signature $(uname -r))" = "1"; then
        [ "$(module_signed "$mod")" = "1" ] || return
    fi

    echo "1"
}

# Check if required modules are installed in the system and versions match.
#
# Returns "1" on success, none otherwise.
setup_complete()
{
    [ "$(module_available vboxguest)"   = "1" ] || return
    [ "$(module_available vboxsf)"      = "1" ] || return

    # All modules are in place.
    echo "1"
}

# setup_script
setup()
{
    info "Setting up modules"

    # chcon is needed on old Fedora/Redhat systems.  No one remembers which.
    test ! -e /etc/selinux/config ||
        chcon -t bin_t "$BUILDINTMP" 2>/dev/null

    if test -z "$INSTALL_NO_MODULE_BUILDS"; then
        # Check whether modules setup is already complete for currently running kernel.
        # Prevent unnecessary rebuilding in order to speed up booting process.
        if test "$(setup_complete)" = "1"; then
            info "VirtualBox Guest Additions kernel modules $VBOX_VERSION $VBOX_REVISION are \
already available for kernel $TARGET_VER and do not require to be rebuilt."
        else
            info "Building the VirtualBox Guest Additions kernel modules.  This may take a while."
            info "To build modules for other installed kernels, run"
            info "  /sbin/rcvboxadd quicksetup <version>"
            info "or"
            info "  /sbin/rcvboxadd quicksetup all"
            if test -d /lib/modules/"$TARGET_VER"/build; then
                setup_modules "$TARGET_VER"
                depmod
            else
                info "Kernel headers not found for target kernel $TARGET_VER. \
Please install them and execute
  /sbin/rcvboxadd setup"
            fi
        fi
    fi
    create_vbox_user
    create_udev_rule
    test -n "${INSTALL_NO_MODULE_BUILDS}" || create_module_rebuild_script
    shared_folder_setup
    # Create user group which will have permissive access to DRP IPC server socket.
    groupadd -r -f vboxdrmipc >/dev/null 2>&1

    if  running_module "vboxguest"; then
        # Only warn user if currently loaded modules version do not match Guest Additions Installation.
        check_running_module_version "vboxguest" || info "Running kernel modules will not be replaced until the system is restarted or 'rcvboxadd reload' triggered"
    fi

    # Put the X.Org driver in place.  This is harmless if it is not needed.
    # Also set up the OpenGL library.
    myerr=`"${INSTALL_DIR}/init/vboxadd-x11" setup 2>&1`
    test -z "${myerr}" || log "${myerr}"

    return 0
}

# cleanup_script
cleanup()
{
    if test -z "${INSTALL_NO_MODULE_BUILDS}"; then
        # Delete old versions of VBox modules.
        cleanup_modules
        depmod

        # Remove old module sources
        for i in $OLDMODULES; do
          rm -rf /usr/src/$i-*
        done
    fi

    # Clean-up X11-related bits
    "${INSTALL_DIR}/init/vboxadd-x11" cleanup

    # Remove other files
    if test -z "${INSTALL_NO_MODULE_BUILDS}"; then
        rm -f /etc/kernel/postinst.d/vboxadd /etc/kernel/prerm.d/vboxadd
        rmdir -p /etc/kernel/postinst.d /etc/kernel/prerm.d 2>/dev/null || true
    fi
    rm -f /sbin/mount.vboxsf 2>/dev/null
    rm -f /etc/udev/rules.d/60-vboxadd.rules 2>/dev/null
    udevadm control --reload >/dev/null 2>&1 || true
    udevcontrol reload_rules >/dev/null 2>&1 || true
}

start()
{
    begin "Starting."

    # Check if kernel modules for currently running kernel are ready
    # and rebuild them if needed.
    test "$(setup_complete)" = "1" || setup

    # Warn if Secure Boot setup not yet complete.
    if test "$(kernel_requires_module_signature)" = "1" && test -z "$DEB_KEY_ENROLLED"; then
        if test -n "$HAVE_DEB_KEY"; then
            info "You must re-start your system to finish secure boot set-up."
        else
            info "You must sign vboxguest, vboxsf and
vboxvideo (if present) kernel modules before using
VirtualBox Guest Additions. See the documentation
for your Linux distribution."
        fi
    fi

    if test -z "${INSTALL_NO_MODULE_BUILDS}"; then
        test -d /sys &&
            ps -A -o comm | grep -q '/*udevd$' 2>/dev/null ||
            no_udev=1
        check_running_module_version "vboxguest" || {
            rm -f $dev || {
                fail "Cannot remove $dev"
            }
            rm -f $userdev || {
                fail "Cannot remove $userdev"
            }
            # Assuming modules were just (re-)built, try to reload everything.
            reload
        }
        case "$no_udev" in 1)
            do_vboxguest_non_udev;;
        esac
    fi  # INSTALL_NO_MODULE_BUILDS

    return 0
}

stop()
{
    begin "Stopping."

    if test -r /etc/ld.so.conf.d/00vboxvideo.conf; then
        rm /etc/ld.so.conf.d/00vboxvideo.conf
        ldconfig
    fi
    if ! umount -a -t vboxsf 2>/dev/null; then
        # Make sure we only fail, if there are truly no more vboxsf
        # mounts in the system.
        [ -n "$(findmnt -t vboxsf)" ] && fail "Cannot unmount vboxsf folders"
    fi
    test -n "${INSTALL_NO_MODULE_BUILDS}" ||
        info "You may need to restart your guest system to finish removing guest drivers or consider running 'rcvboxadd reload'."
    return 0
}

check_root()
{
    # Check if script is running with root privileges and exit if it does not.
    [ `id -u` -eq 0 ] || early_fail "root privileges are required"
}

# Check if process with this PID is running.
check_pid()
{
    pid="$1"

    test -n "$pid" -a -d "/proc/$pid"
}

# A wrapper for check_running_module_version.
# Verify if module is loaded and its version matches
# to current Additions installation version.
check_running_module()
{
    mod="$1"

    # Check args.
    [ -n "$mod" ] || return

    # During reload action it may take some time for module
    # to be fully loaded, so make a few attempts while checking this.
    for attempt in 1 2 3 4 5; do

        # Wait before the next attempt.
        [ -n "$vbox_add_wait" -a $? -ne 0 ] && sleep 1

        running_module "$mod"
        if [ $? -eq 0 ]; then
            mod_is_running="1"
            check_running_module_version "$mod"
            [ $? -eq 0 ] && break
        else
            mod_is_running=""
            false
        fi

    done

    # In case of error, try to print out proper reason of failure.
    if [ $? -ne 0 ]; then
        # Was module loaded?
        if [ -z "$mod_is_running" ]; then
            info "module $mod is not loaded"
        else
            # If module was loaded it means that it has incorrect version.
            info "currently loaded module $mod version ($(running_module_version "$mod")) does not match to VirtualBox Guest Additions installation version ($VBOX_VERSION $VBOX_REVISION)"
        fi

        # Set "bad" rc.
        false
    fi
}

# Go through list of Additions modules and check
# if they were properly loaded.
check_status_kernel()
{
    # Module vboxguest should be loaded unconditionally once Guest Additions were installed.
    check_running_module "vboxguest"

    # Module vboxsf module might not be loaded if VM has no Shared Folder mappings.
    # Check that first and then verify the module.
    if [ $? -eq 0 ]; then
        VBoxControl sharedfolder list >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            check_running_module "vboxsf"
        else
            # Do not spoil $?.
            true
        fi
    else
        false
    fi

    # Module vboxvideo is optional and expected to be loaded only when VM is
    # running VBoxVGA or VBoxSVGA graphics.
    if [ $? -eq 0 ]; then
        gpu_vendor=$(lspci | grep 'VGA compatible controller' | cut -d ' ' -f 5 2>/dev/null)

        # vboxvideo is not installed for kernels 3.10.x and older.
        have_vboxvideo=
        if [ "$(printf '%s\n%s\n' "3.10" "$(uname -r)" | sort -Vr | head -1)" = "$(uname -r)" ]; then
            have_vboxvideo="1"
        fi

        if [ -n "$have_vboxvideo" -a "$gpu_vendor" = "InnoTek" ]; then
            check_running_module "vboxvideo"
        else
            # Do not spoil $?.
            true
        fi
    else
        false
    fi
}

# Check whether user-land processes are running.
# Currently only check for VBoxService.
check_status_user()
{
    [ -r "$VBOXSERVICE_PIDFILE" ] && check_pid "$(cat $VBOXSERVICE_PIDFILE)" >/dev/null 2>&1
}

send_signal_by_pidfile()
{
    sig="$1"
    pidfile="$2"

    if [ -f "$pidfile" ]; then
        check_pid $(cat "$pidfile")
        if [ $? -eq 0 ]; then
            kill "$sig" $(cat "$pidfile") >/dev/null 2>&1
        else
            # Do not spoil $?.
            true
        fi
    else
        # Do not spoil $?.
        true
    fi
}

# SIGUSR1 is used in order to notify VBoxClient processes that system
# update is started or kernel modules are going to be reloaded,
# so VBoxClient can release vboxguest.ko resources and then restart itself.
send_signal()
{
    sig="$1"
    # Specify whether we sending signal to VBoxClient parent (control)
    # process or a child (actual service) process.
    process_type="$2"

    pidfile_postfix=""
    [ -z "$process_type" ] || pidfile_postfix="-$process_type"

    for user_name in $(getent passwd | cut -d ':' -f 1); do

        # Filter out empty login names (paranoia).
        [ -n "$user_name" ] || continue

        user_shell=$(getent passwd "$user_name" | cut -d ':' -f 7)

        # Filter out login names with not specified shells (paranoia).
        [ -n "$user_shell" ] || continue

        # Filter out know non-login account names.
        case "$user_shell" in
        *nologin)   skip_user_home="1";;
        *sync)      skip_user_home="1";;
        *shutdown)  skip_user_home="1";;
        *halt)      skip_user_home="1";;
        *)          skip_user_home=""
        esac
        [ -z "$skip_user_home" ] || continue;

        user_home=$(getent passwd "$user_name" | cut -d ':' -f 6)

        for pid_file in "$user_home"/.vboxclient-*"$pidfile_postfix".pid; do

            [ -r "$pid_file" ] || continue

            # If process type was not specified, we assume that signal supposed
            # to be sent to legacy VBoxClient processes which have different
            # pidfile name pattern (it does not contain "control" or "service").
            # Skip those pidfiles who has.
            [ -z "$process_type" -a -n "$(echo "$pid_file" | grep "control")" ] && continue
            [ -z "$process_type" -a -n "$(echo "$pid_file" | grep "service")" ] && continue

            send_signal_by_pidfile -USR1 "$pid_file"

        done
    done
}

# Helper function which executes a command, prints error message if command fails,
# and preserves command execution status for further processing.
try_load_preserve_rc()
{
    cmd="$1"
    msg="$2"

    $cmd >/dev/null 2>&1

    rc=$?
    [ $rc -eq 0 ] || info "$msg"

    return $rc
}

reload()
{
    begin "reloading kernel modules and services"

    # Stop VBoxService if running.
    $VBOX_SERVICE_SCRIPT status >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        $VBOX_SERVICE_SCRIPT stop >/dev/null 2>&1 || fail "unable to stop VBoxService"
    fi

    # Unmount Shared Folders.
    umount -a -t vboxsf >/dev/null 2>&1 || fail "unable to unmount shared folders, mount point(s) might be still in use"

    # Stop VBoxDRMClient.
    send_signal_by_pidfile "-USR1" "/var/run/VBoxDRMClient" || fail "unable to stop VBoxDRMClient"

    if [ $? -eq 0 ]; then
        # Tell legacy VBoxClient processes to release vboxguest.ko references.
        send_signal "-USR1" ""

        # Tell compatible VBoxClient processes to release vboxguest.ko references.
        send_signal "-USR1" "service"

        # Try unload.
        for attempt in 1 2 3 4 5; do

            # Give VBoxClient processes some time to close reference to vboxguest module.
            [ $? -ne 0 ] && sleep 1

            # Try unload drivers unconditionally (ignore previous command exit code).
            # If final goal of unloading vboxguest.ko won't be met, we will fail on
            # the next step anyway.
            running_module "vboxsf" && modprobe -r vboxsf >/dev/null  2>&1
            running_module "vboxguest"
            if [ $? -eq 0 ]; then
                modprobe -r vboxguest >/dev/null  2>&1
                [ $? -eq 0 ] && break
            else
                # Do not spoil $?.
                true
            fi
        done

        # Check if we succeeded with unloading vboxguest after several attempts.
        running_module "vboxguest"
        if [ $? -eq 0 ]; then
            info "cannot reload kernel modules: one or more module(s) is still in use"
            false
        else
            # Do not spoil $?.
            true
        fi

        # Load drivers (skip vboxvideo since it is not loaded for very old guests).
        [ $? -eq 0 ] && try_load_preserve_rc "modprobe vboxguest" "unable to load vboxguest kernel module, see dmesg"
        [ $? -eq 0 ] && try_load_preserve_rc "modprobe vboxsf" "unable to load vboxsf kernel module, see dmesg"

        # Start VBoxService and VBoxDRMClient.
        [ $? -eq 0 ] && try_load_preserve_rc "$VBOX_SERVICE_SCRIPT start" "unable to start VBoxService"

        # Reload VBoxClient processes.
        [ $? -eq 0 ] && try_load_preserve_rc "send_signal -USR1 control" "unable to reload user session services"

        # Check if we just loaded modules of correct version.
        [ $? -eq 0 ] && try_load_preserve_rc "check_status_kernel" "kernel modules were not reloaded"

        # Check if user-land processes were restarted as well.
        [ $? -eq 0 ] && try_load_preserve_rc "check_status_user" "user-land services were not started"

        if [ $? -eq 0 ]; then
            # Take reported version of running Guest Additions from running vboxguest module (as a paranoia check).
            info "kernel modules and services $(running_module_version "vboxguest") reloaded"
            info "NOTE: you may still consider to re-login if some user session specific services (Shared Clipboard, Drag and Drop, Seamless or Guest Screen Resize) were not restarted automatically"
        else
            # In case of failure, sent SIGTERM to abandoned control processes to remove leftovers from failed reloading.
            send_signal "-TERM" "control"

            fail "kernel modules and services were not reloaded"
        fi
    else
        fail "cannot stop user services"
    fi
}

dmnstatus()
{
    if running_module "vboxguest"; then
        echo "The VirtualBox Additions are currently running."
    else
        echo "The VirtualBox Additions are not currently running."
    fi
}

for i; do
    case "$i" in quiet) QUIET=yes;; esac
done
case "$1" in
# Does setup without clean-up first and marks all kernels currently found on the
# system so that we can see later if any were added.
start)
    check_root
    start
    ;;
# Tries to build kernel modules for kernels added since start.  Tries to unmount
# shared folders.  Uninstalls our Chromium 3D libraries since we can't always do
# this fast enough at start time if we discover we do not want to use them.
stop)
    check_root
    stop
    ;;
restart)
    check_root
    restart
    ;;
# Tries to reload kernel modules and restart user processes.
reload)
    check_root
    # reload() we will call modprobe(8) in order to reload kernel
    # modules. This operation is asynchronous and requires some time for
    # modules to be loaded in most of the cases. By setting this variable, we
    # ask check_running_module() to wait a bit before making a decision
    # whether modules were loaded or not.
    vbox_add_wait=1
    reload
    ;;
# Setup does a clean-up (see below) and re-does all Additions-specific
# configuration of the guest system, including building kernel modules for the
# current kernel.
setup)
    check_root
    cleanup && start
    ;;
# Builds kernel modules for the specified kernels if they are not already built.
quicksetup)
    check_root
    if test x"$2" = xall; then
       for topi in /lib/modules/*; do
           KERN_VER="${topi%/misc}"
           KERN_VER="${KERN_VER#/lib/modules/}"
           setup_modules "$KERN_VER"
        done
    elif test -n "$2"; then
        setup_modules "$2"
    else
        setup_modules "$TARGET_VER"
    fi
    ;;
# Clean-up removes all Additions-specific configuration of the guest system,
# including all kernel modules.
cleanup)
    check_root
    cleanup
    ;;
status)
    dmnstatus
    ;;
status-kernel)
    check_root
    check_status_kernel
    if [ $? -eq 0 ]; then
        info "kernel modules $VBOX_VERSION $VBOX_REVISION are loaded"
    else
        info "kernel modules $VBOX_VERSION $VBOX_REVISION were not loaded"
        false
    fi
    ;;
status-user)
    check_root
    check_status_user
    if [ $? -eq 0 ]; then
        info "user-land services $VBOX_VERSION $VBOX_REVISION are running"
    else
        info "user-land services $VBOX_VERSION $VBOX_REVISION are not running"
        false
    fi
    ;;
*)
    echo "Usage: $0 {start|stop|restart|reload|status|status-kernel|status-user|setup|quicksetup|cleanup} [quiet]"
    exit 1
esac

exit
