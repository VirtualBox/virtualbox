#!/bin/sh
#
# Oracle VirtualBox
# VirtualBox linux installation script

#
# Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

# Testing:
# * After successful installation, 0 is returned if the vboxdrv module version
#   built matches the one loaded.
# * If the kernel modules cannot be built (run the installer with KERN_VER=none)
#   or loaded (run with KERN_VER=<installed non-current version>)
#   then 1 is returned.

PATH=$PATH:/bin:/sbin:/usr/sbin

# Include routines and utilities needed by the installer
. ./routines.sh

LOG="/var/log/vbox-install.log"
VERSION="_VERSION_"
SVNREV="_SVNREV_"
BUILD="_BUILD_"
ARCH="_ARCH_"
HARDENED="_HARDENED_"
# The "BUILD_" prefixes prevent the variables from being overwritten when we
# read the configuration from the previous installation.
BUILD_VBOX_KBUILD_TYPE="_BUILDTYPE_"
BUILD_USERNAME="_USERNAME_"
CONFIG_DIR="/etc/vbox"
CONFIG="vbox.cfg"
CONFIG_FILES="filelist"
DEFAULT_FILES=`pwd`/deffiles
GROUPNAME="vboxusers"
INSTALLATION_DIR="_INSTALLATION_DIR_"
LICENSE_ACCEPTED=""
PREV_INSTALLATION=""
PYTHON="_PYTHON_"
ACTION=""
SELF=$1
RC_SCRIPT=0
if [ -n "$HARDENED" ]; then
    VBOXDRV_MODE=0600
    VBOXDRV_GRP="root"
else
    VBOXDRV_MODE=0660
    VBOXDRV_GRP=$GROUPNAME
fi
VBOXUSB_MODE=0664
VBOXUSB_GRP=$GROUPNAME

## Were we able to stop any previously running Additions kernel modules?
MODULES_STOPPED=1


##############################################################################
# Helper routines                                                            #
##############################################################################

usage() {
    info ""
    info "Usage: install | uninstall"
    info ""
    info "Example:"
    info "$SELF install"
    exit 1
}

module_loaded() {
    lsmod | grep -q "vboxdrv[^_-]"
}

# This routine makes sure that there is no previous installation of
# VirtualBox other than one installed using this install script or a
# compatible method.  We do this by checking for any of the VirtualBox
# applications in /usr/bin.  If these exist and are not symlinks into
# the installation directory, then we assume that they are from an
# incompatible previous installation.

## Helper routine: test for a particular VirtualBox binary and see if it
## is a link into a previous installation directory
##
## Arguments: 1) the binary to search for and
##            2) the installation directory (if any)
## Returns: false if an incompatible version was detected, true otherwise
check_binary() {
    binary=$1
    install_dir=$2
    test ! -e $binary 2>&1 > /dev/null ||
        ( test -n "$install_dir" &&
              readlink $binary 2>/dev/null | grep "$install_dir" > /dev/null
        )
}

## Main routine
##
## Argument: the directory where the previous installation should be
##           located.  If this is empty, then we will assume that any
##           installation of VirtualBox found is incompatible with this one.
## Returns: false if an incompatible installation was found, true otherwise
check_previous() {
    install_dir=$1
    # These should all be symlinks into the installation folder
    check_binary "/usr/bin/VirtualBox" "$install_dir" &&
    check_binary "/usr/bin/VBoxManage" "$install_dir" &&
    check_binary "/usr/bin/VBoxSDL" "$install_dir" &&
    check_binary "/usr/bin/VBoxVRDP" "$install_dir" &&
    check_binary "/usr/bin/VBoxHeadless" "$install_dir" &&
    check_binary "/usr/bin/VBoxDTrace" "$install_dir" &&
    check_binary "/usr/bin/VBoxBugReport" "$install_dir" &&
    check_binary "/usr/bin/VBoxBalloonCtrl" "$install_dir" &&
    check_binary "/usr/bin/VBoxAutostart" "$install_dir" &&
    check_binary "/usr/bin/vboxwebsrv" "$install_dir" &&
    check_binary "/usr/bin/vbox-img" "$install_dir" &&
    check_binary "/usr/bin/vboximg-mount" "$install_dir" &&
    check_binary "/sbin/rcvboxdrv" "$install_dir"
}

##############################################################################
# Main script                                                                #
##############################################################################

info "VirtualBox Version $VERSION r$SVNREV ($BUILD) installer"


# Make sure that we were invoked as root...
check_root

# Set up logging before anything else
create_log $LOG

log "VirtualBox $VERSION r$SVNREV installer, built $BUILD."
log ""
log "Testing system setup..."

# Sanity check: figure out whether build arch matches uname arch
cpu=`uname -m`;
case "$cpu" in
  i[3456789]86|x86)
    cpu="x86"
    ;;
  x86_64)
    cpu="amd64"
    ;;
  aarch64|arm64)
    cpu="arm64"
    ;;
esac
if [ "$cpu" != "$ARCH" ]; then
  info "Detected unsupported $cpu environment."
  log "Detected unsupported $cpu environment."
  exit 1
fi

# Sensible default actions
ACTION="install"
BUILD_MODULE="true"
unset FORCE_UPGRADE
while true
do
    if [ "$2" = "" ]; then
        break
    fi
    shift
    case "$1" in
        install|--install)
            ACTION="install"
            ;;

        uninstall|--uninstall)
            ACTION="uninstall"
            ;;

        force|--force)
            FORCE_UPGRADE=1
            ;;
        license_accepted_unconditionally|--license_accepted_unconditionally)
            # Legacy option
            ;;
        no_module|--no_module)
            BUILD_MODULE=""
            ;;
        *)
            if [ "$ACTION" = "" ]; then
                info "Unknown command '$1'."
                usage
            fi
            info "Specifying an installation path is not allowed -- using _INSTALLATION_DIR_!"
            ;;
    esac
done

if [ "$ACTION" = "install" ]; then
    # Choose a proper umask
    umask 022

    # Find previous installation
    if test -r "$CONFIG_DIR/$CONFIG"; then
        . $CONFIG_DIR/$CONFIG
        PREV_INSTALLATION=$INSTALL_DIR
    fi
    if ! check_previous $INSTALL_DIR && test -z "$FORCE_UPGRADE"
    then
        info
        info "You appear to have a version of VirtualBox on your system which was installed"
        info "from a different source or using a different type of installer (or a damaged"
        info "installation of VirtualBox).  We strongly recommend that you remove it before"
        info "installing this version of VirtualBox."
        info
        info "Do you wish to continue anyway? [yes or no]"
        read reply dummy
        if ! expr "$reply" : [yY] && ! expr "$reply" : [yY][eE][sS]
        then
            info
            info "Cancelling installation."
            log "User requested cancellation of the installation"
            exit 1
        fi
    fi

    # Do additional clean-up in case some-one is running from a build folder.
    ./prerm-common.sh || exit 1

    # Remove previous installation
    test "${BUILD_MODULE}" = true || VBOX_DONT_REMOVE_OLD_MODULES=1

    if [ -n "$PREV_INSTALLATION" ]; then
        [ -n "$INSTALL_REV" ] && INSTALL_REV=" r$INSTALL_REV"
        info "Removing previous installation of VirtualBox $INSTALL_VER$INSTALL_REV from $PREV_INSTALLATION"
        log "Removing previous installation of VirtualBox $INSTALL_VER$INSTALL_REV from $PREV_INSTALLATION"
        log ""

        VBOX_NO_UNINSTALL_MESSAGE=1
        # This also checks $BUILD_MODULE and $VBOX_DONT_REMOVE_OLD_MODULES
        . ./uninstall.sh
    fi

    mkdir -p -m 755 $CONFIG_DIR
    touch $CONFIG_DIR/$CONFIG

    info "Installing VirtualBox to $INSTALLATION_DIR"
    log "Installing VirtualBox to $INSTALLATION_DIR"
    log ""

    # Verify the archive
    mkdir -p -m 755 $INSTALLATION_DIR
    bzip2 -d -c VirtualBox.tar.bz2 > VirtualBox.tar
    if ! tar -tf VirtualBox.tar > $CONFIG_DIR/$CONFIG_FILES; then
        rmdir $INSTALLATION_DIR 2> /dev/null
        rm -f $CONFIG_DIR/$CONFIG 2> /dev/null
        rm -f $CONFIG_DIR/$CONFIG_FILES 2> /dev/null
        log 'Error running "bzip2 -d -c VirtualBox.tar.bz2" or "tar -tf VirtualBox.tar".'
        abort "Error installing VirtualBox.  Installation aborted"
    fi

    # Create installation directory and install
    if ! tar -xf VirtualBox.tar -C $INSTALLATION_DIR; then
        cwd=`pwd`
        cd $INSTALLATION_DIR
        rm -f `cat $CONFIG_DIR/$CONFIG_FILES` 2> /dev/null
        cd $pwd
        rmdir $INSTALLATION_DIR 2> /dev/null
        rm -f $CONFIG_DIR/$CONFIG 2> /dev/null
        log 'Error running "tar -xf VirtualBox.tar -C '"$INSTALLATION_DIR"'".'
        abort "Error installing VirtualBox.  Installation aborted"
    fi

    cp uninstall.sh $INSTALLATION_DIR
    echo "uninstall.sh" >> $CONFIG_DIR/$CONFIG_FILES

    # Hardened build: Mark selected binaries set-user-ID-on-execution,
    #                 create symlinks for working around unsupported $ORIGIN/.. in VBoxC.so (setuid),
    #                 and finally make sure the directory is only writable by the user (paranoid).
    if [ -n "$HARDENED" ]; then
        # Note! Update vboxdrv.sh if the VirtualBoxVM entry changes (bugref:10642).
        test -e $INSTALLATION_DIR/VirtualBoxVM   && chmod 4511 $INSTALLATION_DIR/VirtualBoxVM
        test -e $INSTALLATION_DIR/VBoxSDL        && chmod 4511 $INSTALLATION_DIR/VBoxSDL
        test -e $INSTALLATION_DIR/VBoxHeadless   && chmod 4511 $INSTALLATION_DIR/VBoxHeadless
        test -e $INSTALLATION_DIR/VBoxNetDHCP    && chmod 4511 $INSTALLATION_DIR/VBoxNetDHCP
        test -e $INSTALLATION_DIR/VBoxNetNAT     && chmod 4511 $INSTALLATION_DIR/VBoxNetNAT

        ln -sf $INSTALLATION_DIR/VBoxVMM.so   $INSTALLATION_DIR/components/VBoxVMM.so
        if [ -f $INSTALLATION_DIR/VBoxVMMArm.so ]; then
            ln -sf $INSTALLATION_DIR/VBoxVMMArm.so $INSTALLATION_DIR/components/VBoxVMMArm.so
        fi
        ln -sf $INSTALLATION_DIR/VBoxRT.so    $INSTALLATION_DIR/components/VBoxRT.so

        chmod go-w $INSTALLATION_DIR
    fi

    # This binaries need to be suid root in any case, even if not hardened
    test -e $INSTALLATION_DIR/VBoxNetAdpCtl && chmod 4511 $INSTALLATION_DIR/VBoxNetAdpCtl
    test -e $INSTALLATION_DIR/VBoxVolInfo && chmod 4511 $INSTALLATION_DIR/VBoxVolInfo

    # Make sure the .autoreg file exists and has the current date so that
    # the user's compreg.dat files are updated.
    touch "$INSTALLATION_DIR/.autoreg"

    # Write the configuration.  Needs to be done before the vboxdrv service is
    # started.
    echo "# VirtualBox installation directory" > $CONFIG_DIR/$CONFIG
    echo "INSTALL_DIR='$INSTALLATION_DIR'" >> $CONFIG_DIR/$CONFIG
    echo "# VirtualBox version" >> $CONFIG_DIR/$CONFIG
    echo "INSTALL_VER='$VERSION'" >> $CONFIG_DIR/$CONFIG
    echo "INSTALL_REV='$SVNREV'" >> $CONFIG_DIR/$CONFIG
    echo "# Build type and user name for logging purposes" >> $CONFIG_DIR/$CONFIG
    echo "VBOX_KBUILD_TYPE='$BUILD_VBOX_KBUILD_TYPE'" >> $CONFIG_DIR/$CONFIG
    echo "USERNAME='$BUILD_USERNAME'" >> $CONFIG_DIR/$CONFIG

    # Create users group
    groupadd -r -f $GROUPNAME 2> /dev/null

    # Create symlinks to start binaries
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VirtualBox
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VirtualBoxVM
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxManage
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxSDL
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxVRDP
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxHeadless
    if [ -f $INSTALLATION_DIR/VBoxBalloonCtrl ]; then
        ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxBalloonCtrl
    fi
    if [ -f $INSTALLATION_DIR/VBoxBugReport ]; then
        ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxBugReport
    fi
    ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxAutostart
    if [ -f $INSTALLATION_DIR/vboxwebsrv ]; then
        ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/vboxwebsrv
    fi
    ln -sf $INSTALLATION_DIR/vbox-img /usr/bin/vbox-img
    ln -sf $INSTALLATION_DIR/vboximg-mount /usr/bin/vboximg-mount
    if [ -d /usr/share/pixmaps/ ]; then
        ln -sf $INSTALLATION_DIR/VBox.png /usr/share/pixmaps/VBox.png
    fi
    if [ -f $INSTALLATION_DIR/VBoxDTrace ]; then
        ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxDTrace
    fi
    if [ -f $INSTALLATION_DIR/VBoxAudioTest ]; then
        ln -sf $INSTALLATION_DIR/VBox.sh /usr/bin/VBoxAudioTest
    fi
    # Unity and Nautilus seem to look here for their icons
    if [ -d /usr/share/pixmaps/ ]; then
        ln -sf $INSTALLATION_DIR/icons/128x128/virtualbox.png /usr/share/pixmaps/virtualbox.png
    fi
    if [ -d /usr/share/applications/ ]; then
        ln -sf $INSTALLATION_DIR/virtualbox.desktop /usr/share/applications/virtualbox.desktop
        ln -sf $INSTALLATION_DIR/virtualboxvm.desktop /usr/share/applications/virtualboxvm.desktop
    fi
    if [ -d /usr/share/mime/packages/ ]; then
        ln -sf $INSTALLATION_DIR/virtualbox.xml /usr/share/mime/packages/virtualbox.xml
    fi
    ln -sf $INSTALLATION_DIR/src/vboxhost /usr/src/vboxhost-_VERSION_

    # Convenience symlinks. The creation fails if the FS is not case sensitive
    ln -sf VirtualBox /usr/bin/virtualbox > /dev/null 2>&1
    ln -sf VirtualBoxVM /usr/bin/virtualboxvm > /dev/null 2>&1
    ln -sf VBoxManage /usr/bin/vboxmanage > /dev/null 2>&1
    ln -sf VBoxSDL /usr/bin/vboxsdl > /dev/null 2>&1
    ln -sf VBoxHeadless /usr/bin/vboxheadless > /dev/null 2>&1
    if [ -f $INSTALLATION_DIR/VBoxBugReport ]; then
        ln -sf VBoxBugReport /usr/bin/vboxbugreport > /dev/null 2>&1
    fi
    if [ -f $INSTALLATION_DIR/VBoxDTrace ]; then
        ln -sf VBoxDTrace /usr/bin/vboxdtrace > /dev/null 2>&1
    fi
    if [ -f $INSTALLATION_DIR/VBoxAudioTest ]; then
        ln -sf VBoxAudioTest /usr/bin/vboxaudiotest > /dev/null 2>&1
    fi

    # Icons (if FE/Qt is included)
    if [ -d $INSTALLATION_DIR/icons ]; then
        cur=`pwd`
        cd $INSTALLATION_DIR/icons
        for i in *; do
            cd $i
            if [ -d /usr/share/icons/hicolor/$i ]; then
                for j in *; do
                    if expr "$j" : "virtualbox\..*" > /dev/null; then
                        dst=apps
                    else
                        dst=mimetypes
                    fi
                    if [ -d /usr/share/icons/hicolor/$i/$dst ]; then
                        ln -s $INSTALLATION_DIR/icons/$i/$j /usr/share/icons/hicolor/$i/$dst/$j
                        echo /usr/share/icons/hicolor/$i/$dst/$j >> $CONFIG_DIR/$CONFIG_FILES
                    fi
                done
            fi
            cd -
        done
        cd $cur
    fi

    # Update the MIME database
    update-mime-database /usr/share/mime 2>/dev/null

    # Update the desktop database
    update-desktop-database -q 2>/dev/null

    # If Python is available, install Python bindings
    if [ -n "$PYTHON" ]; then
      maybe_run_python_bindings_installer $INSTALLATION_DIR $CONFIG_DIR $CONFIG_FILES
    fi

    # Do post-installation common to all installer types, currently service
    # script set-up.
    if test "${BUILD_MODULE}" = "true"; then
      START_SERVICES=
    else
      START_SERVICES="--nostart"
    fi
    "${INSTALLATION_DIR}/prerm-common.sh" >> "${LOG}"

    # Now check whether the kernel modules were stopped.
    lsmod | grep -q vboxdrv && MODULES_STOPPED=

    "${INSTALLATION_DIR}/postinst-common.sh" ${START_SERVICES} >> "${LOG}"

    info ""
    info "VirtualBox has been installed successfully."
    info ""
    info "You will find useful information about using VirtualBox in the user manual"
    info "  $INSTALLATION_DIR/UserManual.pdf"
    info "and in the user FAQ"
    info "  http://www.virtualbox.org/wiki/User_FAQ"
    info ""
    info "We hope that you enjoy using VirtualBox."
    info ""

    # And do a final test as to whether the kernel modules were properly created
    # and loaded.  Return 0 if both are true, 1 if not.
    test -n "${MODULES_STOPPED}" &&
        modinfo vboxdrv >/dev/null 2>&1 &&
        lsmod | grep -q vboxdrv ||
        abort "The installation log file is at ${LOG}."

    log "Installation successful"
elif [ "$ACTION" = "uninstall" ]; then
    . ./uninstall.sh
fi
exit $RC_SCRIPT
