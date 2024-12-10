#!/bin/sh
# $Id: makepackage.sh 107327 2024-12-10 20:03:52Z brent.paulson@oracle.com $
## @file
# VirtualBox package creation script, Solaris hosts.
#

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

#
# Usage:
#       makepackage.sh [--hardened] [--ips] [--without-VBoxBugReport] [--without-VBoxBalloonCtrl] \
#           [--without-VBoxAutostart] [--without-vboxwebsrv] \
#           $(PATH_TARGET)/install packagename {$(KBUILD_TARGET_ARCH)|neutral} $(VBOX_SVN_REV)


# Parse options.
HARDENED=""
IPS_PACKAGE=""
PACKAGE_SPEC="prototype"
OPT_WITHOUT_VBoxBugReport=""
OPT_WITHOUT_VBoxBalloonCtrl=""
OPT_WITHOUT_VBoxAutostart=""
OPT_WITHOUT_vboxwebsrv=""
while [ $# -ge 1 ];
do
    case "$1" in
        --hardened)
            HARDENED=1
            ;;
        --ips)
            IPS_PACKAGE=1
            PACKAGE_SPEC="virtualbox.p5m"
            ;;
        --without-VBoxBugReport)
            OPT_WITHOUT_VBoxBugReport="yes"
            ;;
        --without-VBoxBalloonCtrl)
            OPT_WITHOUT_VBoxBalloonCtrl="yes"
            ;;
        --without-VBoxAutostart)
            OPT_WITHOUT_VBoxAutostart="yes"
            ;;
        --without-vboxwebsrv)
            OPT_WITHOUT_vboxwebsrv="yes"
            ;;
        *)
            break
            ;;
    esac
    shift
done

if [ -z "$4" ]; then
    echo "Usage: $0 installdir packagename x86|amd64 svnrev"
    echo "-- packagename must not have any extension (e.g. VirtualBox-SunOS-amd64-r28899)"
    exit 1
fi

PKG_BASE_DIR="$1"
PACKAGE_SPEC="$PKG_BASE_DIR/$PACKAGE_SPEC"
VBOX_INSTALLED_DIR=/opt/VirtualBox
if [ -n "$IPS_PACKAGE" ]; then
    VBOX_PKGFILE="$2".p5p
else
    VBOX_PKGFILE="$2".pkg
fi
# VBOX_PKG_ARCH is currently unused.
VBOX_PKG_ARCH="$3"
VBOX_SVN_REV="$4"

if [ -n "$IPS_PACKAGE" ] ; then
    VBOX_PKGNAME=system/virtualbox
else
    VBOX_PKGNAME=SUNWvbox
fi
# any egrep should do the job, the one from /usr/xpg4/bin isn't required
VBOX_EGREP=/usr/bin/egrep
# need dynamic regex support which isn't available in S11 /usr/bin/awk
VBOX_AWK=/usr/xpg4/bin/awk

# bail out on non-zero exit status
set -e

if [ -n "$IPS_PACKAGE" ]; then

package_spec_create()
{
    > "$PACKAGE_SPEC"
}

package_spec_append_info()
{
    : # provided by vbox-ips.mog
}

package_spec_append_content()
{
    rm -rf "$1/vbox-repo"
    pkgsend generate "$1" | pkgfmt >> "$PACKAGE_SPEC"
}

package_spec_append_hardlink()
{
    if [ -f "$3$4/amd64/$2" -o -f "$3$4/i386/$2" ]; then
        echo "hardlink path=$4/$2 target=$1" >> "$PACKAGE_SPEC"
    fi
}

package_spec_fixup_content()
{
    :
}

package_create()
{
    VBOX_DEF_HARDENED=
    [ -z "$HARDENED" ] && VBOX_DEF_HARDENED='#'

    pkgmogrify -DVBOX_PKGNAME="$VBOX_PKGNAME" -DHARDENED_ONLY="$VBOX_DEF_HARDENED" "$PACKAGE_SPEC" "$1/vbox-ips.mog" | pkgfmt > "$PACKAGE_SPEC.1"

    pkgdepend generate -m -d "$1" "$PACKAGE_SPEC.1" | pkgfmt > "$PACKAGE_SPEC.2"

    pkgdepend resolve -m "$PACKAGE_SPEC.2"

    # Too expensive, and in this form not useful since it does not have
    # the package manifests without using options -r (for repo access) and
    # -c (for caching the data). Not viable since the cache would be lost
    # for every build.
    #pkglint "$PACKAGE_SPEC.2.res"

    pkgrepo create "$1/vbox-repo"
    pkgrepo -s "$1/vbox-repo" set publisher/prefix=virtualbox

    # Create package in local file repository
    pkgsend -s "$1/vbox-repo" publish -d "$1" "$PACKAGE_SPEC.2.res"

    pkgrepo -s "$1/vbox-repo" info
    pkgrepo -s "$1/vbox-repo" list

    # Convert into package archive
    rm -f "$1/$2"
    pkgrecv -a -s "$1/vbox-repo" -d "$1/$2" -m latest "$3"
    rm -rf "$1/vbox-repo"
}

else

package_spec_create()
{
    > "$PACKAGE_SPEC"
}

package_spec_append_info()
{
    echo 'i pkginfo=vbox.pkginfo' >> "$PACKAGE_SPEC"
    echo 'i checkinstall=checkinstall.sh' >> "$PACKAGE_SPEC"
    echo 'i postinstall=postinstall.sh' >> "$PACKAGE_SPEC"
    echo 'i preremove=preremove.sh' >> "$PACKAGE_SPEC"
    echo 'i space=vbox.space' >> "$PACKAGE_SPEC"
}

# Our package is a non-relocatable package.
#
# pkgadd will take care of "relocating" them when they are used for remote installations using
# $PKG_INSTALL_ROOT and not $BASEDIR. Seems this little subtlety led to it's own page:
# https://docs.oracle.com/cd/E19253-01/820-4042/package-2/index.html

package_spec_append_content()
{
    cd "$1"
    # Exclude directories to not cause install-time conflicts with existing system directories.
    # Also exclude various unpackaged files as well as two unnecessary GTK2 shared objects.
    find . ! -type d | "$VBOX_EGREP" -v '^\./(LICENSE|prototype|makepackage\.sh|vbox\.pkginfo|postinstall\.sh|checkinstall\.sh|preremove\.sh|vbox\.space|vbox-ips.mog|virtualbox\.p5m.*)$|libqgtk2styleVBox\.so|libqgtk2VBox\.so' | LC_COLLATE=C sort | pkgproto >> "$PACKAGE_SPEC"
    cd -
    "$VBOX_AWK" 'NF == 3 && $1 == "s" && $2 == "none" { $3="/"$3 } { print }' "$PACKAGE_SPEC" > "$PACKAGE_SPEC.tmp"
    mv -f "$PACKAGE_SPEC.tmp" "$PACKAGE_SPEC"
    "$VBOX_AWK" 'NF == 6 && ($1 == "f" || $1 == "l") && ($2 == "none" || $2 == "manifest") { $3="/"$3 } { print }' "$PACKAGE_SPEC" > "$PACKAGE_SPEC.tmp"
    mv -f "$PACKAGE_SPEC.tmp" "$PACKAGE_SPEC"

    cd "$1"
    # Include opt/VirtualBox and subdirectories as we want uninstall to clean up directory structure.
    # Include var/svc for manifest class action script does not create them.
    find . -type d | "$VBOX_EGREP" 'opt/VirtualBox|var/svc/manifest/application/virtualbox' | LC_COLLATE=C sort | pkgproto >> "$PACKAGE_SPEC"
    cd -
    "$VBOX_AWK" 'NF == 6 && $1 == "d" && $2 == "none" { $3="/"$3 } { print }' "$PACKAGE_SPEC" > "$PACKAGE_SPEC.tmp"
    mv -f "$PACKAGE_SPEC.tmp" "$PACKAGE_SPEC"
}

package_spec_append_hardlink()
{
    if [ -f "$3$4/amd64/$2" -o -f "$3$4/i386/$2" ]; then
        echo "l none $4/$2=$1" >> "$PACKAGE_SPEC"
    fi
}

# Fixup filelist using awk, the parameters must be in awk syntax
# params: filename condition action
package_spec_fixup_filelist()
{
    "$VBOX_AWK" 'NF == 6 && '"$1"' { '"$2"' } { print }' "$PACKAGE_SPEC" > "$PACKAGE_SPEC.tmp"
    mv -f "$PACKAGE_SPEC.tmp" "$PACKAGE_SPEC"
}

package_spec_fixup_dirlist()
{
    "$VBOX_AWK" 'NF == 6 && $1 == "d" && '"$1"' { '"$2"' } { print }' "$PACKAGE_SPEC" > "$PACKAGE_SPEC.tmp"
    mv -f "$PACKAGE_SPEC.tmp" "$PACKAGE_SPEC"
}

package_spec_fixup_content()
{
    # fix up file permissions (owner/group)
    # don't grok for class-specific files (like sed, if any)
    package_spec_fixup_filelist '$2 == "none"'                                                                  '$5 = "root"; $6 = "bin"'

    # HostDriver vboxdrv
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/vboxdrv.conf"'                               '$6 = "sys"'
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/amd64/vboxdrv"'                              '$6 = "sys"'

    # NetFilter vboxflt
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/vboxflt.conf"'                               '$6 = "sys"'
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/amd64/vboxflt"'                              '$6 = "sys"'

    # NetFilter vboxbow
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/vboxbow.conf"'                               '$6 = "sys"'
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/amd64/vboxbow"'                              '$6 = "sys"'

    # NetAdapter vboxnet
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/vboxnet.conf"'                               '$6 = "sys"'
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/amd64/vboxnet"'                              '$6 = "sys"'

    # USBMonitor vboxusbmon
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/vboxusbmon.conf"'                            '$6 = "sys"'
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/amd64/vboxusbmon"'                           '$6 = "sys"'

    # USB Client vboxusb
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/vboxusb.conf"'                               '$6 = "sys"'
    package_spec_fixup_filelist '$3 == "/platform/i86pc/kernel/drv/amd64/vboxusb"'                              '$6 = "sys"'

    # Manifest class action scripts
    package_spec_fixup_filelist '$3 == "/var/svc/manifest/application/virtualbox/virtualbox-webservice.xml"'    '$2 = "manifest";$6 = "sys"'
    if [ -z "${OPT_WITHOUT_VBoxBalloonCtrl}" ]; then
        package_spec_fixup_filelist '$3 == "/var/svc/manifest/application/virtualbox/virtualbox-balloonctrl.xml"'   '$2 = "manifest";$6 = "sys"'
    fi
    package_spec_fixup_filelist '$3 == "/var/svc/manifest/application/virtualbox/virtualbox-zoneaccess.xml"'    '$2 = "manifest";$6 = "sys"'

    # Use 'root' as group so as to match attributes with the previous installation and prevent a conflict. Otherwise pkgadd bails out thinking
    # we're violating directory attributes of another (non existing) package
    package_spec_fixup_dirlist '$3 == "/var/svc/manifest/application/virtualbox"'                               '$6 = "root"'

    # Hardening requires some executables to be marked setuid.
    if [ -n "$HARDENED" ]; then
        package_spec_fixup_filelist '(   $3 == "/opt/VirtualBox/amd64/VirtualBoxVM" \
                                      || $3 == "/opt/VirtualBox/amd64/VBoxHeadless" \
                                      || $3 == "/opt/VirtualBox/amd64/VBoxSDL" \
                                      || $3 == "/opt/VirtualBox/i386/VirtualBox" \
                                      || $3 == "/opt/VirtualBox/i386/VBoxHeadless" \
                                      || $3 == "/opt/VirtualBox/i386/VBoxSDL" )'                                '$4 = "4755"'
    fi

    # Other executables that need setuid root (hardened or otherwise)
    package_spec_fixup_filelist '(   $3 == "/opt/VirtualBox/amd64/VBoxNetAdpCtl" \
                                  || $3 == "/opt/VirtualBox/i386/VBoxNetAdpCtl" \
                                  || $3 == "/opt/VirtualBox/amd64/VBoxNetDHCP" \
                                  || $3 == "/opt/VirtualBox/i386/VBoxNetDHCP" \
                                  || $3 == "/opt/VirtualBox/amd64/VBoxNetNAT" \
                                  || $3 == "/opt/VirtualBox/i386/VBoxNetNAT" )'                                 '$4 = "4755"'

    echo " --- start of $PACKAGE_SPEC  ---"
    cat "$PACKAGE_SPEC"
    echo " --- end of $PACKAGE_SPEC ---"
}

package_create()
{
    # Create the package instance
    pkgmk -o -f "$PACKAGE_SPEC" -r "$1"

    # Translate into package datastream
    pkgtrans -s -o /var/spool/pkg "$1/$2" "$3"

    rm -rf "/var/spool/pkg/$2"
}

fi


# Prepare package spec
package_spec_create

# Metadata
package_spec_append_info "$PKG_BASE_DIR"

# File and direcory list
package_spec_append_content "$PKG_BASE_DIR"

# Add hardlinks for executables to launch the 32-bit or 64-bit executable
for f in VBoxManage VBoxSDL VBoxZoneAccess VBoxSVC VirtualBox VirtualBoxVM vbox-img VBoxHeadless; do
    package_spec_append_hardlink VBoxISAExec $f "$PKG_BASE_DIR" "$VBOX_INSTALLED_DIR"
done
if [ -z "${OPT_WITHOUT_VBoxBugReport}" ]; then
    package_spec_append_hardlink VBoxISAExec VBoxBugReport "$PKG_BASE_DIR" "$VBOX_INSTALLED_DIR"
fi
if [ -z "${OPT_WITHOUT_VBoxBalloonCtrl}" ]; then
    package_spec_append_hardlink VBoxISAExec VBoxBalloonCtrl "$PKG_BASE_DIR" "$VBOX_INSTALLED_DIR"
fi
if [ -z "${OPT_WITHOUT_VBoxAutostart}" ]; then
    package_spec_append_hardlink VBoxISAExec VBoxAutostart "$PKG_BASE_DIR" "$VBOX_INSTALLED_DIR"
fi
if [ -z "${OPT_WITHOUT_vboxwebsrv}" ]; then
    package_spec_append_hardlink VBoxISAExec vboxwebsrv "$PKG_BASE_DIR" "$VBOX_INSTALLED_DIR"
fi

package_spec_fixup_content

package_create "$PKG_BASE_DIR" "$VBOX_PKGFILE" "$VBOX_PKGNAME" "$VBOX_SVN_REV"

echo "## Package file created successfully!"

exit $?
