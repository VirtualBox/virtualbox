#!/bin/sh
# $Id: autorun.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Guest Additions installation script for *nix guests
#

#
# Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

PATH=$PATH:/bin:/sbin:/usr/sbin

# Deal with differing "which" semantics
mywhich() {
    which "$1" 2>/dev/null | grep -v "no $1"
}

# Get the name and execute switch for a useful terminal emulator
#
# Sets $gxtpath to the emulator path or empty
# Sets $gxttitle to the "title" switch for that emulator
# Sets $gxtexec to the "execute" switch for that emulator
# May clobber $gtx*
# Calls mywhich
getxterm() {
    # gnome-terminal and mate-terminal use -e differently to other emulators
    for gxti in "konsole --title -e" "gnome-terminal --title -x" "mate-terminal --title -x" "xterm -T -e"; do
        set $gxti
        gxtpath="`mywhich $1`"
        case "$gxtpath" in ?*)
            gxttitle=$2
            gxtexec=$3
            return
            ;;
        esac
    done
}

# Quotes its argument by inserting '\' in front of every character save
# for 'A-Za-z0-9/'.  Prints the result to stdout.
quotify() {
    echo "$1" | sed -e 's/\([^a-zA-Z0-9/]\)/\\\1/g'
}

ostype=`uname -s`
if test "$ostype" != "Linux" && test "$ostype" != "SunOS" ; then
  echo "Linux/Solaris not detected."
  exit 1
fi

# The below is GNU-specific.  See VBox.sh for the longer Solaris/OS X version.
TARGET=`readlink -e -- "${0}"` || exit 1
path="${TARGET%/[!/]*}"
# 32-bit or 64-bit?
case `uname -m` in
  i[3456789]86|x86|i86pc)
    arch='x86'
    ;;
  x86_64|amd64|AMD64)
    arch='amd64'
    ;;
  aarch64)
    arch='arm64'
    ;;
  *)
    echo "Unknown architecture `uname -m`."
    exit 1
    ;;
esac

# execute the installer
if test "$ostype" = "Linux"; then

    # Specify path to the architecture specific installer.
    if test "$arch" = "arm64"; then
        installer="$path/VBoxLinuxAdditions-arm64.run"
    else
        installer="$path/VBoxLinuxAdditions.run"
    fi

    # Proceed to installation.
    if test -f "$installer"; then
        getxterm
        case "$gxtpath" in ?*)
            TITLE="VirtualBox Guest Additions installation"
            BINARY="`quotify "$installer"`"
            exec "$gxtpath" "$gxttitle" "$TITLE" "$gxtexec" /bin/sh "$path/runasroot.sh" --has-terminal "$TITLE" "/bin/sh $BINARY --xwin" "Please try running "\""$installer"\"" manually."
            exit
            ;;
        *)
            echo "Unable to start installation process with elevated privileges automatically. Please try running "\""$installer"\"" manually."
            exit
        ;;
        esac
    fi

    # else: unknown failure
    echo "Linux guest additions installer not found -- try to start it manually."
    exit 1

elif test "$ostype" = "SunOS"; then

    # check for combined package
    installfile="$path/VBoxSolarisAdditions.pkg"
    if test -f "$installfile"; then

        # check for pkgadd bin
        pkgaddbin=pkgadd
        found=`which pkgadd | grep "no pkgadd"`
        if test ! -z "$found"; then
            if test -f "/usr/sbin/pkgadd"; then
                pkgaddbin=/usr/sbin/pkgadd
            else
                echo "Could not find pkgadd."
                exit 1
            fi
        fi

        # check for pfexec
        pfexecbin=pfexec
        found=`which pfexec | grep "no pfexec"`
        if test ! -z "$found"; then
            # Use su and prompt for password
            echo "Could not find pfexec."
            subin=`which su`
        else
            idbin=/usr/xpg4/bin/id
            if test ! -x "$idbin"; then
                found=`which id 2> /dev/null`
                if test ! -x "$found"; then
                    echo "Failed to find a suitable user id executable."
                    exit 1
                else
                    idbin=$found
                fi
            fi

            # check if pfexec can get the job done
            if test "$idbin" = "/usr/xpg4/bin/id"; then
                userid=`$pfexecbin $idbin -u`
            else
                userid=`$pfexecbin $idbin | cut -f1 -d'(' | cut -f2 -d'='`
            fi
            if test $userid != "0"; then
                # pfexec exists but user has no pfexec privileges, switch to using su and prompting password
                subin=`which su`
            fi
        fi

        # create temporary admin file for autoinstall
        TMPFILE=`mktemp -q /tmp/vbox.XXXXXX`
        if [ -z $TMPFILE ]; then
            echo "Unable to create a temporary file"
            exit 1
        fi
        echo "basedir=default
runlevel=nocheck
conflict=quit
setuid=nocheck
action=nocheck
partial=quit
instance=unique
idepend=quit
rdepend=quit
space=quit
mail=
" > $TMPFILE

        # check gnome-terminal, use it if it exists.
        if test -f "/usr/bin/gnome-terminal"; then
            # use su/pfexec
            if test -z "$subin"; then
                /usr/bin/gnome-terminal --title "Installing VirtualBox Additions" --command "/bin/sh -c '$pfexecbin $pkgaddbin -G -d $installfile -n -a $TMPFILE SUNWvboxguest; /bin/echo press ENTER to close this window; /bin/read'"
            else
                /usr/bin/gnome-terminal --title "Installing VirtualBox Additions: Root password required." --command "/bin/sh -c '$subin - root -c \"$pkgaddbin -G -d $installfile -n -a $TMPFILE SUNWvboxguest\"; /bin/echo press ENTER to close this window; /bin/read'"
            fi
        elif test -f "/usr/X11/bin/xterm"; then
            # use xterm
            if test -z "$subin"; then
                /usr/X11/bin/xterm -title "Installing VirtualBox Additions" -e "$pfexecbin $pkgaddbin -G -d $installfile -n -a $TMPFILE SUNWvboxguest; /bin/echo press ENTER to close this window; /bin/read"
            else
                /usr/X11/bin/xterm -title "Installing VirtualBox Additions: Root password required." -e "$subin - root -c \"$pkgaddbin -G -d $installfile -n -a $TMPFILE SUNWvboxguest\"; /bin/echo press ENTER to close this window; /bin/read"
            fi
        else
            echo "No suitable terminal not found. -- install additions using pkgadd -d."
        fi
        rm -r $TMPFILE

        exit 0
    fi

    echo "Solaris guest additions installer not found -- try to start them manually."
    exit 1
fi

