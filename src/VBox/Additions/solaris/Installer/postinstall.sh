#!/bin/sh
# $Id: postinstall.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox postinstall script for Solaris Guest Additions.
#

#
# Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

# LC_ALL should take precedence over LC_* and LANG but whatever...
LC_ALL=C
export LC_ALL

LANG=C
export LANG

# "Remote" installs ('pkgadd -R') can skip many of the steps below.
REMOTE_INST=0
BASEDIR_OPT=""
if test "x${PKG_INSTALL_ROOT:-/}" != "x/"; then
    BASEDIR_OPT="-R $PKG_INSTALL_ROOT"
    REMOTE_INST=1
fi
export REMOTE_INST
export BASEDIR_OPT

# uncompress(directory, file)
# Updates package metadata and uncompresses the file.
uncompress_file()
{
    if test -z "$1" || test -z "$2"; then
        echo "missing argument to uncompress_file()"
        return 1
    fi

    # Remove compressed path from the pkg
    /usr/sbin/removef $BASEDIR_OPT $PKGINST "$1/$2.Z" 1>/dev/null

    # Add uncompressed path to the pkg
    /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST "$1/$2" f

    # Uncompress the file (removes compressed file when done)
    uncompress -f "$1/$2.Z" > /dev/null 2>&1
}

# uncompress_files(directory_with_*.Z_files)
uncompress_files()
{
    for i in "${1}/"*.Z; do
        uncompress_file "${1}" "`basename \"${i}\" .Z`"
    done
}

solaris64dir="amd64"
solaris32dir="i386"
vboxadditions_path="${PKG_INSTALL_ROOT}/opt/VirtualBoxAdditions"
vboxadditions32_path=$vboxadditions_path/$solaris32dir
vboxadditions64_path=$vboxadditions_path/$solaris64dir

# get the current zone
currentzone=`zonename`
# get what ISA the guest is running
cputype=`isainfo -k`
if test "$cputype" = "amd64"; then
    isadir=$solaris64dir
else
    isadir=""
fi

vboxadditionsisa_path=$vboxadditions_path/$isadir


# uncompress if necessary
if test -f "$vboxadditions32_path/VBoxClient.Z" || test -f "$vboxadditions64_path/VBoxClient.Z"; then
    echo "Uncompressing files..."
    if test -f "$vboxadditions32_path/VBoxClient.Z"; then
        uncompress_files "$vboxadditions32_path"
    fi
    if test -f "$vboxadditions64_path/VBoxClient.Z"; then
        uncompress_files "$vboxadditions64_path"
    fi
fi


if test "$currentzone" = "global"; then
    # vboxguest.sh would've been installed, we just need to call it.
    echo "Configuring VirtualBox guest kernel module..."
    # stop all previous modules (vboxguest, vboxfs) and load vboxguest
    # ('vboxguest.sh start' only starts vboxguest).
    if test "$REMOTE_INST" -eq 0; then
        $vboxadditions_path/vboxguest.sh stopall silentunload
    fi
    $vboxadditions_path/vboxguest.sh start

    # Figure out group to use for /etc/devlink.tab (before Solaris 11 SRU6
    # it was always using group sys)
    group=sys
    if [ -f /etc/dev/reserved_devnames ]; then
        # Solaris 11 SRU6 and later use group root (check a file which isn't
        # tainted by VirtualBox install scripts and allow no other group)
        refgroup=`LC_ALL=C /usr/bin/ls -lL /etc/dev/reserved_devnames | awk '{ print $4 }' 2>/dev/null`
        if [ $? -eq 0 -a "x$refgroup" = "xroot" ]; then
            group=root
        fi
        unset refgroup
    fi

    sed -e '/name=vboxguest/d' ${PKG_INSTALL_ROOT}/etc/devlink.tab > ${PKG_INSTALL_ROOT}/etc/devlink.vbox
    echo "type=ddi_pseudo;name=vboxguest	\D" >> ${PKG_INSTALL_ROOT}/etc/devlink.vbox
    chmod 0644 ${PKG_INSTALL_ROOT}/etc/devlink.vbox
    chown root:$group ${PKG_INSTALL_ROOT}/etc/devlink.vbox
    mv -f ${PKG_INSTALL_ROOT}/etc/devlink.vbox ${PKG_INSTALL_ROOT}/etc/devlink.tab

    # create the device link
    /usr/sbin/devfsadm $BASEDIR_OPT -i vboxguest
fi

# create links
echo "Creating links..."
if test "$currentzone" = "global"; then
    /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST /dev/vboxguest=../devices/pci@0,0/pci80ee,cafe@4:vboxguest s
    /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST /dev/vboxms=../devices/pseudo/vboxms@0:vboxms s
fi

# check if X.Org exists (snv_130 and higher have /usr/X11/* as /usr/*)
if test -f "${PKG_INSTALL_ROOT}/usr/bin/Xorg"; then
    xorgbin="${PKG_INSTALL_ROOT}/usr/bin/Xorg"
elif test -f "${PKG_INSTALL_ROOT}/usr/X11/bin/Xorg"; then
    xorgbin="${PKG_INSTALL_ROOT}/usr/X11/bin/Xorg"
else
    xorgbin=""
    retval=0
fi

# Install Xorg components to the required places
if test ! -z "$xorgbin"; then
    xorgversion_long=`$xorgbin -version 2>&1 | grep "X Window System Version"`
    xorgversion=`/usr/bin/expr "${xorgversion_long}" : 'X Window System Version \([^ ]*\)'`
    if test -z "$xorgversion_long"; then
        xorgversion_long=`$xorgbin -version 2>&1 | grep "X.Org X Server"`
        xorgversion=`/usr/bin/expr "${xorgversion_long}" : 'X.Org X Server \([^ ]*\)'`
    fi

    # "X.Y.Z" - strip off all numerics after the 2nd '.' character, e.g. "1.11.3" -> "1.11"
    # The second sed invocation removes all '.' characters, e.g. "1.11" -> "111".
    fileversion=`echo $xorgversion | sed "s/\.[0-9]*//2" | sed "s/\.//"`
    vboxvideo_src="vboxvideo_drv_$fileversion.so"

    # Handle exceptions now where the X.org version does not exactly match the file-version.
    case "$xorgversion" in
        1.5.99 )
            vboxvideo_src="vboxvideo_drv_16.so"
            ;;
        7.2.* )
            vboxvideo_src="vboxvideo_drv_71.so"
            ;;
        6.9.* )
            vboxvideo_src="vboxvideo_drv_70.so"
            ;;
    esac

    retval=0
    if test -z "$vboxvideo_src"; then
        echo "*** Unknown version of the X Window System installed."
        echo "*** Failed to install the VirtualBox X Window System drivers."

        # Exit as partially failed installation
        retval=2
    elif test ! -f "$vboxadditions32_path/$vboxvideo_src" && test ! -f "$vboxadditions64_path/$vboxvideo_src"; then
        if test ! -f "${PKG_INSTALL_ROOT}/usr/lib/xorg/modules/drivers/vboxvideo_drv.so"; then
            # Xorg 1.19 and later (delivered first in st_006) already contain a driver
            # for vboxvideo so advise users to install the required package if it isn't
            # already present.
            echo "As of X.Org Server 1.19, the VirtualBox graphics driver (vboxvideo) is part"
            echo "of Solaris.  Please install the package pkg:/x11/server/xorg/driver/xorg-video-vboxvideo"
            echo "from the package repository for the vboxvideo_drv.so graphics driver."
        fi
    else
        echo "Installing video driver for X.Org $xorgversion..."

        # Determine destination paths (snv_130 and above use "/usr/lib/xorg", older use "/usr/X11/lib"
        vboxvideo32_dest_base="${PKG_INSTALL_ROOT}/usr/lib/xorg/modules/drivers"
        if test ! -d $vboxvideo32_dest_base; then
            vboxvideo32_dest_base="${PKG_INSTALL_ROOT}/usr/X11/lib/modules/drivers"
        fi

        vboxvideo64_dest_base=$vboxvideo32_dest_base/$solaris64dir

        # snv_163 drops 32-bit support completely, and uses 32-bit locations for the 64-bit stuff. Ugly.
        # We try to detect this by looking at bitness of "vesa_drv.so", and adjust our destination paths accordingly.
        # We do not rely on using Xorg -version's ABI output because some builds (snv_162 iirc) have 64-bit ABI with
        # 32-bit file locations.
        if test -f "$vboxvideo32_dest_base/vesa_drv.so"; then
            bitsize=`file "$vboxvideo32_dest_base/vesa_drv.so" | grep -i "32-bit"`
            skip32="no"
        else
            echo "* Warning vesa_drv.so missing. Assuming Xorg ABI is 64-bit..."
        fi

        if test -z "$bitsize"; then
            skip32="yes"
            vboxvideo64_dest_base=$vboxvideo32_dest_base
        fi

        # Make sure destination path exists
        if test ! -d $vboxvideo64_dest_base; then
            echo "*** Missing destination paths for video module. Aborting."
            echo "*** Failed to install the VirtualBox X Window System driver."

            # Exit as partially failed installation
            retval=2
        else
            # 32-bit x11 drivers
            if test "$skip32" = "no" && test -f "$vboxadditions32_path/$vboxvideo_src"; then
                vboxvideo_dest="$vboxvideo32_dest_base/vboxvideo_drv.so"
                /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST "$vboxvideo_dest" f
                cp "$vboxadditions32_path/$vboxvideo_src" "$vboxvideo_dest"

                # Removing redundant names from pkg and files from disk
                /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions32_path/vboxvideo_drv_* 1>/dev/null
                rm -f $vboxadditions32_path/vboxvideo_drv_*
            fi

            # 64-bit x11 drivers
            if test -f "$vboxadditions64_path/$vboxvideo_src"; then
                vboxvideo_dest="$vboxvideo64_dest_base/vboxvideo_drv.so"
                /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST "$vboxvideo_dest" f
                cp "$vboxadditions64_path/$vboxvideo_src" "$vboxvideo_dest"

                # Removing redundant names from pkg and files from disk
                /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions64_path/vboxvideo_drv_* 1>/dev/null
                rm -f $vboxadditions64_path/vboxvideo_drv_*
            fi

            # Some distros like Indiana have no xorg.conf, deal with this
            if  test ! -f '${PKG_INSTALL_ROOT}/etc/X11/xorg.conf' && \
                test ! -f '${PKG_INSTALL_ROOT}/etc/X11/.xorg.conf'; then

                # Xorg 1.3.x+ should use the modeline-less Xorg confs while older should
                # use ones with all the video modelines in place. Argh.
                xorgconf_file="solaris_xorg_modeless.conf"
                xorgconf_unfit="solaris_xorg.conf"
                case "$xorgversion" in
                    7.1.* | 7.2.* | 6.9.* | 7.0.* )
                        xorgconf_file="solaris_xorg.conf"
                        xorgconf_unfit="solaris_xorg_modeless.conf"
                        ;;
                esac

                /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions_path/$xorgconf_file 1>/dev/null
                mv -f $vboxadditions_path/$xorgconf_file ${PKG_INSTALL_ROOT}/etc/X11/.xorg.conf

                /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions_path/$xorgconf_unfit 1>/dev/null
                rm -f $vboxadditions_path/$xorgconf_unfit
            fi

            # Check for VirtualBox graphics card
            # S10u10's prtconf doesn't support the '-d' option, so let's use -v even though it's slower.
            is_vboxgraphics=`${PKG_INSTALL_ROOT}/usr/sbin/prtconf -v | grep -i pci80ee,beef`
            if test "$?" -eq 0; then
                drivername="vboxvideo"
            else
                # Check for VMware graphics card
                is_vmwaregraphics=`${PKG_INSTALL_ROOT}/usr/sbin/prtconf -v | grep -i pci15ad,405`
                if test "$?" -eq 0; then
                    echo "Configuring X.Org to use VMware SVGA graphics driver..."
                    drivername="vmware"
                fi
            fi

            # Adjust xorg.conf with video driver sections if a supported graphics card is found
            if test ! -z "$drivername"; then
                $vboxadditions_path/x11config15sol.pl "$drivername"
            else
                # No supported graphics card found, do nothing.
                echo "## No supported graphics card found. Skipped configuring of X.org drivers."
            fi
        fi
    fi


    # Setup our VBoxClient
    echo "Configuring client..."
    vboxclient_src=$vboxadditions_path
    vboxclient_dest="${PKG_INSTALL_ROOT}/usr/share/gnome/autostart"
    clientinstalled=0
    if test -d "$vboxclient_dest"; then
        /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST $vboxclient_dest/vboxclient.desktop=$vboxadditions_path/vboxclient.desktop s
        clientinstalled=1
    fi
    vboxclient_dest="${PKG_INSTALL_ROOT}/usr/dt/config/Xsession.d"
    if test -d "$vboxclient_dest"; then
        /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST $vboxclient_dest/1099.vboxclient=$vboxadditions_path/1099.vboxclient s
        clientinstalled=1
    fi

    # Try other autostart locations if none of the above ones work
    if test $clientinstalled -eq 0; then
        vboxclient_dest="${PKG_INSTALL_ROOT}/etc/xdg/autostart"
        if test -d "$vboxclient_dest"; then
            /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST $vboxclient_dest/1099.vboxclient=$vboxadditions_path/1099.vboxclient s
            clientinstalled=1
        else
            echo "*** Failed to configure client, couldn't find any autostart directory!"
            # Exit as partially failed installation
            retval=2
        fi
    fi
else
    echo "(*) X.Org not found, skipped configuring X.Org guest additions."
fi


# Shared Folder kernel module (different for S10 & Nevada)
osverstr=`uname -r`
vboxfsmod="vboxfs"
vboxfsunused="vboxfs_s10"
if test "$osverstr" = "5.10"; then
    vboxfsmod="vboxfs_s10"
    vboxfsunused="vboxfs"
fi

# Move the appropriate module to kernel/fs & remove the unused module name from pkg and file from disk
# 64-bit shared folder module
if test -f "$vboxadditions64_path/$vboxfsmod"; then
    echo "Installing 64-bit shared folders module..."
    /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST "/usr/kernel/fs/$solaris64dir/vboxfs" f
    mv -f $vboxadditions64_path/$vboxfsmod ${PKG_INSTALL_ROOT}/usr/kernel/fs/$solaris64dir/vboxfs
    /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions64_path/$vboxfsmod 1>/dev/null
    /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions64_path/$vboxfsunused 1>/dev/null
    rm -f $vboxadditions64_path/$vboxfsunused
fi

# 32-bit shared folder module
if test -f "$vboxadditions32_path/$vboxfsmod"; then
    echo "Installing 32-bit shared folders module..."
    /usr/sbin/installf $BASEDIR_OPT -c none $PKGINST "/usr/kernel/fs/vboxfs" f
    mv -f $vboxadditions32_path/$vboxfsmod ${PKG_INSTALL_ROOT}/usr/kernel/fs/vboxfs
    /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions32_path/$vboxfsmod 1>/dev/null
    /usr/sbin/removef $BASEDIR_OPT $PKGINST $vboxadditions32_path/$vboxfsunused 1>/dev/null
    rm -f $vboxadditions32_path/$vboxfsunused
fi

# Add a group "vboxsf" for Shared Folders access
# All users which want to access the auto-mounted Shared Folders have to
# be added to this group.
if test "$REMOTE_INST" -eq 0; then
    groupadd vboxsf >/dev/null 2>&1
fi

# Move the pointer integration module to kernel/drv & remove the unused module name from pkg and file from disk

# Finalize
/usr/sbin/removef $BASEDIR_OPT -f $PKGINST
/usr/sbin/installf $BASEDIR_OPT -f $PKGINST


if test "$currentzone" = "global"; then
    /usr/sbin/devfsadm $BASEDIR_OPT -i vboxguest

    # Setup VBoxService and vboxmslnk and start the services automatically
    echo "Configuring VBoxService and vboxmslnk services (this might take a while)..."
    cmax=32
    cslept=0
    success=0
    sync

    if test "$REMOTE_INST" -eq 0; then
        # Since S11 the way to import a manifest is via restarting manifest-import which is asynchronous and can
        # take a while to complete, using disable/enable -s doesn't work either. So we restart it, and poll in
        # 1 second intervals to see if our service has been successfully imported and timeout after 'cmax' seconds.
        /usr/sbin/svcadm restart svc:system/manifest-import:default
        /usr/bin/svcs virtualbox/vboxservice >/dev/null 2>&1 && /usr/bin/svcs virtualbox/vboxmslnk >/dev/null 2>&1
        while test "$?" -ne 0;
        do
            sleep 1
            cslept=`expr $cslept + 1`
            if test "$cslept" -eq "$cmax"; then
                success=1
                break
            fi
            /usr/bin/svcs virtualbox/vboxservice >/dev/null 2>&1 && /usr/bin/svcs virtualbox/vboxmslnk >/dev/null 2>&1
        done
        if test "$success" -eq 0; then
            echo "Enabling services..."
            /usr/sbin/svcadm enable -s virtualbox/vboxservice
            /usr/sbin/svcadm enable -s virtualbox/vboxmslnk
        else
            echo "## Service import failed."
            echo "## See /var/svc/log/system-manifest-import:default.log for details."
            # Exit as partially failed installation
            retval=2
        fi
    fi

    # Update boot archive
    BOOTADMBIN=/sbin/bootadm
    if test -x "$BOOTADMBIN"; then
        if test -h "${PKG_INSTALL_ROOT}/dev/vboxguest"; then
            echo "Updating boot archive..."
            $BOOTADMBIN update-archive $BASEDIR_OPT > /dev/null
        else
            echo "## Guest kernel module doesn't seem to be up. Skipped explicit boot-archive update."
        fi
    else
        echo "## $BOOTADMBIN not found/executable. Skipped explicit boot-archive update."
    fi
fi

echo "Done."
if test "$REMOTE_INST" -eq 0; then
    if test $retval -eq 0; then
        if test ! -z "$xorgbin"; then
            echo "Please re-login to activate the X11 guest additions."
        fi
        echo "If you have just un-installed the previous guest additions a REBOOT is required."
    fi
fi
exit $retval

