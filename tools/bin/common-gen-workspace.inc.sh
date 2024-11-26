#!/usr/bin/env kmk_ash
# $Id: common-gen-workspace.inc.sh 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $
## @file
# Script for generating a SlickEdit workspace.
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

#
# Some constants.
#
MY_CAT="kmk_cat"
MY_CP="kmk_cp"
MY_MKDIR="kmk_mkdir"
MY_MV="kmk_mv"
MY_SED="kmk_sed"
MY_RM="kmk_rm"
MY_SLEEP="kmk_sleep"
MY_EXPR="kmk_expr"
MY_SVN="svn"

#MY_SORT="kmk_cat"
MY_SORT="sort"


#
# Globals.
#
MY_OUT_DIRS="\
out/${KBUILD_TARGET}.${KBUILD_TARGET_ARCH}/${KBUILD_TYPE} \
out/${KBUILD_TARGET}.${KBUILD_TARGET_ARCH}/debug \
out/${KBUILD_TARGET}.${KBUILD_TARGET_ARCH}/release \
out/linux.amd64/debug \
out/linux.x86/debug \
out/win.amd64/debug \
out/win.x86/debug \
out/darwin.amd64/debug \
out/darwin.x86/debug \
out/haiku.amd64/debug \
out/haiku.x86/debug \
out/solaris.amd64/debug \
out/solaris.x86/debug";


#
# Parameters w/ defaults.
#
MY_ROOT_DIR=".."


##
# Gets the absolute path to an existing directory.
#
# @param    $1      The path.
my_abs_dir()
{
    if test -n "${PWD}"; then
        MY_ABS_DIR=`cd ${MY_ROOT_DIR}/${1} && echo ${PWD}`

    else
        # old cygwin shell has no PWD and need adjusting.
        MY_ABS_DIR=`cd ${MY_ROOT_DIR}/${1} && pwd | ${MY_SED} -e 's,^/cygdrive/\(.\)/,\1:/,'`
    fi
    if test -z "${MY_ABS_DIR}"; then
        MY_ABS_DIR="${1}"
    fi
}

##
# Gets the file name part of a path.
#
# @param    $1      The path.
my_get_name()
{
    SAVED_IFS=$IFS
    IFS=":/ "
    set $1
    while test $# -gt 1  -a  -n "${2}";
    do
        shift;
    done

    IFS=$SAVED_IFS
    #echo "$1"
    export MY_GET_NAME=$1
}

##
# Gets the newest version of a library (like openssl).
#
# @param    $1      The library base path relative to root.
my_get_newest_ver()
{
    cd "${MY_ROOT_DIR}" > /dev/null
    latest=
    for ver in "$1"*;
    do
        if test -z "${latest}" || "${MY_EXPR}" "${ver}" ">" "${latest}" > /dev/null; then
            latest="${ver}"
        fi
    done
    if test -z "${latest}"; then
        echo "error: could not find any version of: $1" >&2;
        exit 1;
    fi
    echo "${latest}"
    return 0;
}


##
# Generate file entry for the specified file if it was found to be of interest.
#
# @param    $1      The output file name base.
# @param    $2      The file name.
# @param    $3      Optional folder override.
my_file()
{
    # sort and filter by file type.
    case "$2" in
        # drop these.
        *.kup|*~|*.pyc|*.exe|*.sys|*.dll|*.o|*.obj|*.lib|*.a|*.ko)
            return 0
            ;;

        # by prefix or directory component.
        tst*|*/testcase/*)
            MY_FOLDER="$1-Testcases.lst"
            ;;

        # by extension.
        *.c|*.cpp|*.m|*.mm|*.pl|*.py|*.as|*.c.h|*.cpp.h|*.java)
            MY_FOLDER="$1-Sources.lst"
            ;;

        *.h|*.hpp|*.mm)
            MY_FOLDER="$1-Headers.lst"
            ;;

        *.asm|*.s|*.S|*.inc|*.mac)
            MY_FOLDER="$1-Assembly.lst"
            ;;

        *)
            MY_FOLDER="$1-Others.lst"
            ;;
    esac
    if test -n "$3";
    then
        MY_FOLDER="$1-$3.lst"
    fi

    ## @todo only files which are in subversion.
#    if ${MY_SVN} info "${2}" > /dev/null 2>&1; then
        my_get_name "${2}"
        echo ' <!-- sortkey: '"${MY_GET_NAME}"' --> <F N="'"${2}"'"/>' >> "${MY_FOLDER}"
#    fi
}

##
# Generate file entry for the specified file if it was found to be of interest.
#
# @param    $1      The output file name base.
# @param    $2      The wildcard spec.
# @param    $3      Optional folder override.
my_wildcard()
{
    if test -n "$3"; then
        MY_FOLDER="$1-$3.lst"
    else
        MY_FOLDER="$1-All.lst"
    fi
    EXCLUDES="*.log;*.kup;*~;*.bak;*.bak?;*.pyc;*.exe;*.sys;*.dll;*.o;*.obj;*.lib;*.a;*.ko;*.class;*.cvsignore;*.done;*.project;*.actionScriptProperties;*.scm-settings;*.svnpatch.rej;*.svn-base;.svn/*;*.gitignore;*.gitattributes;*.gitmodules;*.swagger-codegen-ignore;*.png;*.bmp;*.jpg;*.gif;*.jar;*.checksrc;*.clang-format;*.dir-locals.el;*.drirc;*.editorconfig;*.htaccess"
    echo '        <F N="'"${2}"'/*" Recurse="1" Excludes="'"${EXCLUDES}"'"/>' >> "${MY_FOLDER}"
}

##
# Generate file entries for the specified sub-directory tree.
#
# @param    $1      The output filename.
# @param    $2      The sub-directory.
# @param    $3      Optional folder override.
my_sub_tree()
{
    if test -n "$MY_DBG"; then echo "dbg: my_sub_tree: ${2}"; fi

    # Skip .svn directories.
    case "$2" in
        */.svn|*/.svn)
            return 0
            ;;
    esac

    # Process the files in the directory.
    for f in $2/*;
    do
        if test -d "${f}";
        then
            my_sub_tree "${1}" "${f}" "${3}"
        else
            my_file "${1}" "${f}" "${3}"
        fi
    done
    return 0;
}

##
# Generate the projects.
#
# This requires the main script to implement the my_generate_project() function.
#
# Note! The configs aren't optimal yet, lots of adjustment wrt headers left to be done.
#
my_generate_all_projects()
{
    # src/VBox/Runtime
    my_generate_project "IPRT"          "src/VBox/Runtime"                      --begin-incs "include" "src/VBox/Runtime/include"               --end-includes "include/iprt" "src/VBox/Runtime"

    # src/VBox/VMM
    my_generate_project "VMM"           "src/VBox/VMM"                          --begin-incs "include" "src/VBox/VMM"                           --end-includes "src/VBox/VMM" \
        "include/VBox/vmm/cfgm.h" \
        "include/VBox/vmm/cpum.*" \
        "include/VBox/vmm/dbgf.h" \
        "include/VBox/vmm/em.h" \
        "include/VBox/vmm/gim.h" \
        "include/VBox/vmm/apic.h" \
        "include/VBox/vmm/gmm.*" \
        "include/VBox/vmm/gvm.*" \
        "include/VBox/vmm/hm*.*" \
        "include/VBox/vmm/iom.h" \
        "include/VBox/vmm/mm.h" \
        "include/VBox/vmm/patm.*" \
        "include/VBox/vmm/pdm*.h" \
        "include/VBox/vmm/pgm.*" \
        "include/VBox/vmm/selm.*" \
        "include/VBox/vmm/ssm.h" \
        "include/VBox/vmm/stam.*" \
        "include/VBox/vmm/tm.h" \
        "include/VBox/vmm/trpm.*" \
        "include/VBox/vmm/vm.*" \
        "include/VBox/vmm/vmm.*"

    # src/VBox/Additions
    my_generate_project "Add-darwin"    "src/VBox/Additions/darwin"             --begin-incs "include" "src/VBox/Additions/darwin"              --end-includes "src/VBox/Additions/darwin"
    my_generate_project "Add-freebsd"   "src/VBox/Additions/freebsd"            --begin-incs "include" "src/VBox/Additions/freebsd"             --end-includes "src/VBox/Additions/freebsd"
    my_generate_project "Add-haiku"     "src/VBox/Additions/haiku"              --begin-incs "include" "src/VBox/Additions/haiku"               --end-includes "src/VBox/Additions/haiku"
    my_generate_project "Add-linux"     "src/VBox/Additions/linux"              --begin-incs "include" "src/VBox/Additions/linux"               --end-includes "src/VBox/Additions/linux"
    my_generate_project "Add-os2"       "src/VBox/Additions/os2"                --begin-incs "include" "src/VBox/Additions/os2"                 --end-includes "src/VBox/Additions/os2"
    my_generate_project "Add-solaris"   "src/VBox/Additions/solaris"            --begin-incs "include" "src/VBox/Additions/solaris"             --end-includes "src/VBox/Additions/solaris"
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "Add-x11"   "src/VBox/Additions/x11"                --begin-incs "include" "src/VBox/Additions/x11"                 --end-includes "src/VBox/Additions/x11"
    fi
    my_generate_project "Add-Control"   "src/VBox/Additions/common/VBoxControl" --begin-incs "include" "src/VBox/Additions/common/VBoxControl"  --end-includes "src/VBox/Additions/common/VBoxControl"
    my_generate_project "Add-VBoxGuest" "src/VBox/Additions/common/VBoxGuest"   --begin-incs "include" "src/VBox/Additions/common/VBoxGuest"    --end-includes "src/VBox/Additions/common/VBoxGuest"      "include/VBox/VBoxGuest*.*"
    my_generate_project "Add-Vbgl"      "src/VBox/Additions/common/VBoxGuest/lib" --begin-incs "include" "src/VBox/Additions/common/VBoxGuest/lib" --end-includes "src/VBox/Additions/common/VBoxGuest/lib" "include/VBox/VBoxGuest/lib/*.*"
    my_generate_project "Add-VBoxService" "src/VBox/Additions/common/VBoxService" --begin-incs "include" "src/VBox/Additions/common/VBoxService"  --end-includes "src/VBox/Additions/common/VBoxService"
    my_generate_project "Add-VBoxVideo" "src/VBox/Additions/common/VBoxVideo"   --begin-incs "include" "src/VBox/Additions/common/VBoxVideo"    --end-includes "src/VBox/Additions/common/VBoxVideo"
    my_generate_project "Add-nt-Tray"   "src/VBox/Additions/WINNT/VBoxTray"     --begin-incs "include" "src/VBox/Additions/WINNT/include" "src/VBox/Additions/WINNT/VBoxTray" --end-includes "src/VBox/Additions/WINNT/VBoxTray"
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "Add-cmn-pam"       "src/VBox/Additions/common/pam"         --begin-incs "include" "src/VBox/Additions/common/pam"          --end-includes "src/VBox/Additions/common/pam"
        my_generate_project "Add-cmn-test"      "src/VBox/Additions/common/testcase"    --begin-incs "include" "src/VBox/Additions/common/testcase"     --end-includes "src/VBox/Additions/common/testcase"
        mesa=$(my_get_newest_ver src/VBox/Additions/3D/mesa/mesa)
        my_generate_project "Add-3D-Mesa"       "${mesa}"                               --begin-incs "include" "${mesa}/include" --end-includes "${mesa}"
        my_generate_project "Add-3D-win"        "src/VBox/Additions/3D/win"             --begin-incs "include" "src/VBox/Additions/3D/win/include" "${mesa}/include" --end-includes "src/VBox/Additions/3D/win"
        my_generate_project "Add-nt-Graphics"   "src/VBox/Additions/WINNT/Graphics"     --begin-incs "include" "src/VBox/Additions/WINNT/Graphics/Video/common" "src/VBox/Additions/WINNT/include" "src/VBox/Additions/3D/win/include" "${mesa}/include" --end-includes "src/VBox/Additions/WINNT/Graphics"
        my_generate_project "Add-nt-Installer"  "src/VBox/Additions/WINNT/Installer"    --begin-incs "include" "src/VBox/Additions/WINNT/include"       --end-includes "src/VBox/Additions/WINNT/Installer"
        my_generate_project "Add-nt-Mouse"      "src/VBox/Additions/WINNT/Mouse"        --begin-incs "include" "src/VBox/Additions/WINNT/include"       --end-includes "src/VBox/Additions/WINNT/Mouse"
        my_generate_project "Add-nt-SharedFolders" "src/VBox/Additions/WINNT/SharedFolders" --begin-incs "include" "src/VBox/Additions/WINNT/include"   --end-includes "src/VBox/Additions/WINNT/SharedFolders"
        my_generate_project "Add-nt-tools"      "src/VBox/Additions/WINNT/tools"        --begin-incs "include" "src/VBox/Additions/WINNT/include"       --end-includes "src/VBox/Additions/WINNT/tools"
        my_generate_project "Add-nt-CredProv"   "src/VBox/Additions/WINNT/VBoxCredProv" --begin-incs "include" "src/VBox/Additions/WINNT/VBoxCredProv"  --end-includes "src/VBox/Additions/WINNT/VBoxCredProv"
        my_generate_project "Add-nt-GINA"       "src/VBox/Additions/WINNT/VBoxGINA"     --begin-incs "include" "src/VBox/Additions/WINNT/VBoxGINA"      --end-includes "src/VBox/Additions/WINNT/VBoxGINA"
        my_generate_project "Add-nt-Hook"       "src/VBox/Additions/WINNT/VBoxHook"     --begin-incs "include" "src/VBox/Additions/WINNT/include"       --end-includes "src/VBox/Additions/WINNT/VBoxHook"
    fi

    # src/VBox/Debugger
    my_generate_project "Debugger"      "src/VBox/Debugger"                     --begin-incs "include" "src/VBox/Debugger"                      --end-includes "src/VBox/Debugger" "include/VBox/dbggui.h" "include/VBox/dbg.h"

    # src/VBox/Devices
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "Devices"   "src/VBox/Devices"                      --begin-incs "include" "src/VBox/Devices"                       --end-includes "src/VBox/Devices" "include/VBox/pci.h" "include/VBox/pdm*.h"
    fi
    ## @todo split this up.

    # src/VBox/Disassembler
    my_generate_project "DIS"           "src/VBox/Disassembler"                 --begin-incs "include" "src/VBox/Disassembler"                  --end-includes "src/VBox/Disassembler" "include/VBox/dis*.h"

    # src/VBox/Frontends
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "FE-VBoxAutostart"   "src/VBox/Frontends/VBoxAutostart"   --begin-incs "include" "src/VBox/Frontends/VBoxAutostart"       --end-includes "src/VBox/Frontends/VBoxAutostart"
        my_generate_project "FE-VBoxBugReport"   "src/VBox/Frontends/VBoxBugReport"   --begin-incs "include" "src/VBox/Frontends/VBoxBugReport"       --end-includes "src/VBox/Frontends/VBoxBugReport"
        my_generate_project "FE-VBoxBalloonCtrl" "src/VBox/Frontends/VBoxBalloonCtrl" --begin-incs "include" "src/VBox/Frontends/VBoxBalloonCtrl"     --end-includes "src/VBox/Frontends/VBoxBalloonCtrl"
    fi
    my_generate_project "FE-VBoxManage"     "src/VBox/Frontends/VBoxManage"     --begin-incs "include" "src/VBox/Frontends/VBoxManage"          --end-includes "src/VBox/Frontends/VBoxManage"
    my_generate_project "FE-VBoxHeadless"   "src/VBox/Frontends/VBoxHeadless"   --begin-incs "include" "src/VBox/Frontends/VBoxHeadless"        --end-includes "src/VBox/Frontends/VBoxHeadless"
    my_generate_project "FE-VBoxSDL"        "src/VBox/Frontends/VBoxSDL"        --begin-incs "include" "src/VBox/Frontends/VBoxSDL"             --end-includes "src/VBox/Frontends/VBoxSDL"
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "FE-VBoxShell"  "src/VBox/Frontends/VBoxShell"      --begin-incs "include" "src/VBox/Frontends/VBoxShell"           --end-includes "src/VBox/Frontends/VBoxShell"
        my_generate_project "FE-VBoxBFE"    "src/VBox/Frontends/VBoxBFE"        --begin-incs "include" "src/VBox/Frontends/VBoxBFE"             --end-includes "src/VBox/Frontends/VBoxBFE"
    fi
    FE_VBOX_WRAPPERS=""
    for d in ${MY_OUT_DIRS};
    do
        if test -d "${MY_ROOT_DIR}/${d}/obj/VirtualBox/include"; then
            FE_VBOX_WRAPPERS="${d}/obj/VirtualBox/include"
            break
        fi
    done
    if test -n "${FE_VBOX_WRAPPERS}"; then
        my_generate_project "FE-VirtualBox" "src/VBox/Frontends/VirtualBox"     --begin-incs "include" "${FE_VBOX_WRAPPERS}"                    --end-includes "src/VBox/Frontends/VirtualBox" "${FE_VBOX_WRAPPERS}/COMWrappers.cpp" "${FE_VBOX_WRAPPERS}/COMWrappers.h"
    else
        my_generate_project "FE-VirtualBox" "src/VBox/Frontends/VirtualBox"     --begin-incs "include"                                          --end-includes "src/VBox/Frontends/VirtualBox"
    fi

    # src/VBox/GuestHost
    my_generate_project "HGSMI-GH"      "src/VBox/GuestHost/HGSMI"              --begin-incs "include"                                          --end-includes "src/VBox/GuestHost/HGSMI"
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "DragAndDrop-GH" "src/VBox/GuestHost/DragAndDrop"   --begin-incs "include"                                          --end-includes "src/VBox/GuestHost/DragAndDrop"
    fi
    my_generate_project "ShClip-GH"     "src/VBox/GuestHost/SharedClipboard"    --begin-incs "include"                                          --end-includes "src/VBox/GuestHost/SharedClipboard"
    my_generate_project "Install-GH"    "src/VBox/GuestHost/installation"       --begin-incs "include"                                          --end-includes "src/VBox/GuestHost/installation"

    # src/VBox/HostDrivers
    my_generate_project "SUP"           "src/VBox/HostDrivers/Support"          --begin-incs "include" "src/VBox/HostDrivers/Support"           --end-includes "src/VBox/HostDrivers/Support" "include/VBox/sup.h" "include/VBox/sup.mac"
    my_generate_project "VBoxNetAdp"    "src/VBox/HostDrivers/VBoxNetAdp"       --begin-incs "include" "src/VBox/HostDrivers/VBoxNetAdp"        --end-includes "src/VBox/HostDrivers/VBoxNetAdp" "include/VBox/intnet.h"
    my_generate_project "VBoxNetFlt"    "src/VBox/HostDrivers/VBoxNetFlt"       --begin-incs "include" "src/VBox/HostDrivers/VBoxNetFlt"        --end-includes "src/VBox/HostDrivers/VBoxNetFlt" "src/VBox/HostDrivers/win" "src/VBox/HostDrivers/darwin" "src/VBox/HostDrivers/linux" "src/VBox/HostDrivers/freebsd" "include/VBox/intnet.h"
    my_generate_project "VBoxUSB"       "src/VBox/HostDrivers/VBoxUSB"          --begin-incs "include" "src/VBox/HostDrivers/VBoxUSB"           --end-includes "src/VBox/HostDrivers/VBoxUSB" "include/VBox/usblib*.h" "include/VBox/usbfilter.h"
    my_generate_project "AdpCtl"        "src/VBox/HostDrivers/adpctl"           --begin-incs "include"                                          --end-includes "src/VBox/HostDrivers/adpctl"

    # src/VBox/HostServices
    my_generate_project "HS-auth"       "src/VBox/HostServices/auth"            --begin-incs "include" "src/VBox/HostServices/auth"            --end-includes "src/VBox/HostServices/auth"
    my_generate_project "HS-common"     "src/VBox/HostServices/common"          --begin-incs "include" "src/VBox/HostServices/common"          --end-includes "src/VBox/HostServices/common"
    my_generate_project "HS-testcase"   "src/VBox/HostServices/testcase"        --begin-incs "include" "src/VBox/HostServices/testcase"        --end-includes "src/VBox/HostServices/testcase"
    my_generate_project "GstCtl-HS"     "src/VBox/HostServices/GuestControl"    --begin-incs "include" "src/VBox/HostServices/GuestControl"    --end-includes "src/VBox/HostServices/GuestControl"
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "DragAndDrop-HS" "src/VBox/HostServices/DragAndDrop" --begin-incs "include" "src/VBox/HostServices/DragAndDrop"     --end-includes "src/VBox/HostServices/DragAndDrop"
    fi
    my_generate_project "GuestProps-HS" "src/VBox/HostServices/GuestProperties" --begin-incs "include" "src/VBox/HostServices/GuestProperties" --end-includes "src/VBox/HostServices/GuestProperties"
    my_generate_project "ShClip-HS"     "src/VBox/HostServices/SharedClipboard" --begin-incs "include" "src/VBox/HostServices/SharedClipboard" --end-includes "src/VBox/HostServices/SharedClipboard"
    my_generate_project "ShFolders-HS"  "src/VBox/HostServices/SharedFolders"   --begin-incs "include" "src/VBox/HostServices/SharedFolders"   --end-includes "src/VBox/HostServices/SharedFolders" "include/VBox/shflsvc.h"

    # src/VBox/ImageMounter
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "ImageMounter"  "src/VBox/ImageMounter"             --begin-incs "include" "src/VBox/ImageMounter"              --end-includes "src/VBox/ImageMounter"
    fi

    # src/VBox/Installer
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "Installers"    "src/VBox/Installer"                --begin-incs "include"                                      --end-includes "src/VBox/Installer"
    fi

    # src/VBox/Main
    my_generate_project "Main"          "src/VBox/Main"                         --begin-incs "include" "src/VBox/Main/include"                  --end-includes "src/VBox/Main" "include/VBox/com" "include/VBox/settings.h"
    ## @todo seperate webservices and Main. pick the right headers. added generated headers.

    # src/VBox/Network
    my_generate_project "Net-DHCP"      "src/VBox/NetworkServices/Dhcpd"        --begin-incs "include" "src/VBox/NetworkServices/NetLib"        --end-includes "src/VBox/NetworkServices/Dhcpd"
    my_generate_project "Net-NAT"       "src/VBox/NetworkServices/NAT"          --begin-incs "include" "src/VBox/NetworkServices/NAT"           --end-includes "src/VBox/NetworkServices/NAT" "src/VBox/Devices/Network/slirp"
    my_generate_project "Net-NetLib"    "src/VBox/NetworkServices/NetLib"       --begin-incs "include" "src/VBox/NetworkServices/NetLib"        --end-includes "src/VBox/NetworkServices/NetLib"

    # src/VBox/RDP
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "RDP-Server"    "src/VBox/RDP/server"               --begin-incs "include" "src/VBox/RDP/server"                --end-includes "src/VBox/RDP/server"
        my_generate_project "RDP-WebClient" "src/VBox/RDP/webclient"            --begin-incs "include" "src/VBox/RDP/webclient"             --end-includes "src/VBox/RDP/webclient"
        my_generate_project "RDP-Misc"      "src/VBox/RDP"                      --begin-incs "include"                                      --end-includes "src/VBox/RDP/auth" "src/VBox/RDP/tscpasswd" "src/VBox/RDP/x11server"
    fi

    # src/VBox/Storage
    my_generate_project "Storage"       "src/VBox/Storage"                      --begin-incs "include" "src/VBox/Storage"                       --end-includes "src/VBox/Storage"

    # src/VBox/ValidationKit
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "ValidationKit" "src/VBox/ValidationKit"            --begin-incs "include"                                      --end-includes "src/VBox/ValidationKit"
    fi

    # src/VBox/ExtPacks
    my_generate_project "ExtPacks"      "src/VBox/ExtPacks"                     --begin-incs "include"                                          --end-includes "src/VBox/ExtPacks"

    # src/bldprogs
    my_generate_project "bldprogs"      "src/bldprogs"                          --begin-incs "include"                                          --end-includes "src/bldprogs"

    # A few things from src/lib
    if test -z "$MY_OPT_MINIMAL"; then
        lib=$(my_get_newest_ver src/libs/zlib)
        my_generate_project "zlib"          "${lib}"                            --begin-incs "include"                                          --end-includes "${lib}/*.c" "${lib}/*.h"
        lib=$(my_get_newest_ver src/libs/liblzf)
        my_generate_project "liblzf"        "${lib}"                            --begin-incs "include"                                          --end-includes "${lib}"
        lib=$(my_get_newest_ver src/libs/libpng)
        my_generate_project "libpng"        "${lib}"                            --begin-incs "include"                                          --end-includes "${lib}/*.c" "${lib}/*.h"
        lib=$(my_get_newest_ver src/libs/openssl)
        my_generate_project "openssl"       "${lib}"                            --begin-incs "include" "${lib}/crypto"                          --end-includes "${lib}"
        lib=$(my_get_newest_ver src/libs/curl)
        my_generate_project "curl"          "${lib}"                            --begin-incs "include" "${lib}/include"                         --end-includes "${lib}"
        lib=$(my_get_newest_ver src/libs/softfloat)
        my_generate_project "softfloat"     "${lib}"                            --begin-incs "include" "${lib}/source/include"                  --end-includes "${lib}"
        lib=$(my_get_newest_ver src/libs/libvorbis)
        my_generate_project "libvorbis"     "${lib}"                            --begin-incs "include" "${lib}/include/vorbis"                  --end-includes "${lib}"
        lib=$(my_get_newest_ver src/libs/libogg)
        my_generate_project "libogg"        "${lib}"                            --begin-incs "include" "${lib}/include/ogg"                     --end-includes "${lib}"
    fi

    # webtools
    if test -z "$MY_OPT_MINIMAL"; then
        my_generate_project "webtools"      "webtools"                          --begin-incs "include" "webtools/tinderbox/server/Tinderbox3"   --end-includes "webtools"
    fi

    # include/VBox
    my_generate_project "VBoxHeaders"   "include"                               --begin-incs "include"                                          --end-includes "include/VBox"

    # Misc
    my_generate_project "misc"          "."                                     --begin-incs "include"                                          --end-includes \
        "configure" \
        "configure.vbs" \
        "Config.kmk" \
        "Makefile.kmk" \
        "src/Makefile.kmk" \
        "src/VBox/Makefile.kmk" \
        "tools/env.sh" \
        "tools/env.cmd" \
        "tools/envSub.vbs" \
        "tools/envSub.cmd" \
        "tools/win/vbscript"


    # out/x.y/z/bin/sdk/bindings/xpcom
    XPCOM_INCS="src/libs/xpcom18a4"
    for d in \
        "out/${KBUILD_TARGET}.${KBUILD_TARGET_ARCH}/${KBUILD_TYPE}/dist/sdk/bindings/xpcom" \
        "out/${KBUILD_TARGET}.${KBUILD_TARGET_ARCH}/${KBUILD_TYPE}/bin/sdk/bindings/xpcom" \
        "out/linux.amd64/debug/bin/sdk/bindings/xpcom" \
        "out/linux.x86/debug/bin/sdk/bindings/xpcom" \
        "out/darwin.amd64/debug/dist/sdk/bindings/xpcom" \
        "out/darwin.x86/debug/bin/dist/bindings/xpcom" \
        "out/haiku.amd64/debug/bin/sdk/bindings/xpcom" \
        "out/haiku.x86/debug/bin/sdk/bindings/xpcom" \
        "out/solaris.amd64/debug/bin/sdk/bindings/xpcom" \
        "out/solaris.x86/debug/bin/sdk/bindings/xpcom";
    do
        if test -d "${MY_ROOT_DIR}/${d}"; then
            my_generate_project "SDK-xpcom" "${d}"  --begin-incs "include" "${d}/include" --end-includes "${d}"
            XPCOM_INCS="${d}/include"
            break
        fi
    done

    # lib/xpcom
    my_generate_project "xpcom"         "src/libs/xpcom18a4"                    --begin-incs "include" "${XPCOM_INCS}"                          --end-includes "src/libs/xpcom18a4"
}
