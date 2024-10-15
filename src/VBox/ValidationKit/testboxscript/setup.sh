#!/usr/bin/env bash
# $Id: setup.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Validation Kit - TestBoxScript Service Setup on Unixy platforms.
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
# !WARNING! Running the whole script in exit-on-failure mode.
#
# Note! Looking at the ash sources, it seems flags will be saved and restored
#       when calling functions.  That's comforting.
#
set -e
#set -x # debug only, disable!

##
# Get the host OS name, returning it in RETVAL.
#
get_host_os() {
    RETVAL=`uname`
    case "$RETVAL" in
        Darwin|darwin)
            RETVAL=darwin
            ;;

        DragonFly)
            RETVAL=dragonfly
            ;;

        freebsd|FreeBSD|FREEBSD)
            RETVAL=freebsd
            ;;

        Haiku)
            RETVAL=haiku
            ;;

        linux|Linux|GNU/Linux|LINUX)
            RETVAL=linux
            ;;

        netbsd|NetBSD|NETBSD)
            RETVAL=netbsd
            ;;

        openbsd|OpenBSD|OPENBSD)
            RETVAL=openbsd
            ;;

        os2|OS/2|OS2)
            RETVAL=os2
            ;;

        SunOS)
            RETVAL=solaris
            ;;

        WindowsNT|CYGWIN_NT-*)
            RETVAL=win
            ;;

        *)
            echo "$0: unknown os $RETVAL" 1>&2
            exit 1
            ;;
    esac
    return 0;
}

##
# Get the host OS/CPU arch, returning it in RETVAL.
#
get_host_arch() {
    if [ "${HOST_OS}" = "solaris" ]; then
        RETVAL=`isainfo | cut -f 1 -d ' '`
    else
        RETVAL=`uname -m`
    fi
    case "${RETVAL}" in
        amd64|AMD64|x86_64|k8|k8l|k9|k10)
            RETVAL='amd64'
            ;;
        x86|i86pc|ia32|i[3456789]86|BePC)
            RETVAL='x86'
            ;;
        sparc32|sparc|sparcv8|sparcv7|sparcv8e)
            RETVAL='sparc32'
            ;;
        sparc64|sparcv9)
            RETVAL='sparc64'
            ;;
        s390)
            RETVAL='s390'
            ;;
        s390x)
            RETVAL='s390x'
            ;;
        ppc32|ppc|powerpc)
            RETVAL='ppc32'
            ;;
        ppc64|powerpc64)
            RETVAL='ppc64'
            ;;
        mips32|mips)
            RETVAL='mips32'
            ;;
        mips64)
            RETVAL='mips64'
            ;;
        ia64)
            RETVAL='ia64'
            ;;
        hppa32|parisc32|parisc)
            RETVAL='hppa32'
            ;;
        hppa64|parisc64)
            RETVAL='hppa64'
            ;;
        arm|armv4l|armv5tel|armv5tejl)
            RETVAL='arm'
            ;;
        arm64|aarch64)
            RETVAL='arm64'
            ;;
        alpha)
            RETVAL='alpha'
            ;;

        *)
            echo "$0: unknown cpu/arch - $RETVAL" 1>&$2
            exit 1
            ;;
    esac
    return 0;
}


##
# Loads config values from the current installation.
#
os_load_config() {
    echo "os_load_config is not implemented" 2>&1
    exit 1
}

##
# Installs, configures and starts the service.
#
os_install_service() {
    echo "os_install_service is not implemented" 2>&1
    exit 1
}

##
# Enables (starts) the service.
os_enable_service() {
    echo "os_enable_service is not implemented" 2>&1
    return 0;
}

##
# Disables (stops) the service.
os_disable_service() {
    echo "os_disable_service is not implemented" 2>&1
    return 0;
}

##
# Adds the testbox user
#
os_add_user() {
    echo "os_add_user is not implemented" 2>&1
    exit 1
}

##
# Prints a final message after successful script execution.
# This can contain additional instructions which needs to be carried out
# manually or similar.
os_final_message() {
    return 0;
}

##
# Checks the installation, verifying that files are there and scripts work fine.
#
check_testboxscript_install() {

    # Presence
    test -r "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript.py"
    test -r "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript_real.py"
    test -r "${TESTBOXSCRIPT_DIR}/testboxscript/linux/testboxscript-service.sh" -o "${HOST_OS}" != "linux"
    test -r "${TESTBOXSCRIPT_DIR}/${HOST_OS}/${HOST_ARCH}/TestBoxHelper"

    # Zip file may be missing the x bits, so set them.
    chmod a+x \
        "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript.py" \
        "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript_real.py" \
        "${TESTBOXSCRIPT_DIR}/${HOST_OS}/${HOST_ARCH}/TestBoxHelper" \
        "${TESTBOXSCRIPT_DIR}/testboxscript/linux/testboxscript-service.sh"


    # Check that the scripts work.
    set +e
    "${TESTBOXSCRIPT_PYTHON}" "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript.py" --version > /dev/null
    if [ $? -ne 2 ]; then
        echo "$0: error: testboxscript.py didn't respond correctly to the --version option."
        exit 1;
    fi

    "${TESTBOXSCRIPT_PYTHON}" "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript_real.py" --version > /dev/null
    if [ $? -ne 2 ]; then
        echo "$0: error: testboxscript.py didn't respond correctly to the --version option."
        exit 1;
    fi
    set -e

    return 0;
}

##
# Check that sudo is installed.
#
check_for_sudo() {
    which sudo
    test -f "${MY_ETC_SUDOERS}"
}

##
# Check that sudo is installed.
#
check_for_cifs() {
    return 0;
}

##
# Checks if the testboxscript_user exists.
does_testboxscript_user_exist() {
    id "${TESTBOXSCRIPT_USER}" > /dev/null 2>&1
    return $?;
}

##
# hushes up the root login.
maybe_hush_up_root_login() {
    # This is a solaris hook.
    return 0;
}

##
# Adds the testbox user and make sure it has unrestricted sudo access.
maybe_add_testboxscript_user() {
    if ! does_testboxscript_user_exist; then
        os_add_user "${TESTBOXSCRIPT_USER}"
    fi

    SUDOERS_LINE="${TESTBOXSCRIPT_USER} ALL=(ALL) NOPASSWD: ALL"
    if ! ${MY_FGREP} -q "${SUDOERS_LINE}" ${MY_ETC_SUDOERS}; then
        echo "# begin tinderboxscript setup.sh" >> ${MY_ETC_SUDOERS}
        echo "${SUDOERS_LINE}"                  >> ${MY_ETC_SUDOERS}
        echo "# end tinderboxscript setup.sh"   >> ${MY_ETC_SUDOERS}
    fi

    maybe_hush_up_root_login;
}


##
# Test the user.
#
test_user() {
    su - "${TESTBOXSCRIPT_USER}" -c "true"

    # sudo 1.7.0 adds the -n option.
    MY_TMP="`sudo -V 2>&1 | head -1 | sed -e 's/^.*version 1\.[6543210]\..*$/old/'`"
    if [ "${MY_TMP}" != "old" ]; then
        echo "Warning: If sudo starts complaining about not having a tty,"
        echo "         disable the requiretty option in /etc/sudoers."
        su - "${TESTBOXSCRIPT_USER}" -c "sudo -n -i true"
    else
        echo "Warning: You've got an old sudo installed. If it starts"
        echo "         complaining about not having a tty, disable the"
        echo "         requiretty option in /etc/sudoers."
        su - "${TESTBOXSCRIPT_USER}" -c "sudo true"
    fi
}

##
# Test if core dumps are enabled. See https://wiki.ubuntu.com/Apport!
#
test_coredumps() {
    # This is a linux hook.
    return 0;
}

##
# Test if unattended updates are disabled. See
#   http://ask.xmodulo.com/disable-automatic-updates-ubuntu.html
test_unattended_updates_disabled() {
    # This is a linux hook.
    return 0;
}

##
# Grants the user write access to the testboxscript files so it can perform
# upgrades.
#
grant_user_testboxscript_write_access() {
    chown -R "${TESTBOXSCRIPT_USER}" "${TESTBOXSCRIPT_DIR}"
}

##
# Check the proxy setup.
#
check_proxy_config() {
    if [ -n "${http_proxy}"  -o  -n "${ftp_proxy}" ]; then
        if [ -z "${no_proxy}" ]; then
            echo "Error: Env.vars. http_proxy/ftp_proxy without no_proxy is going to break upgrade among other things."
            exit 1
        fi
    fi
}

##
# Parses the testboxscript.py invocation, setting TESTBOXSCRIPT_xxx config
# variables accordingly.  Both darwin and solaris uses this.
common_testboxscript_args_to_config()
{
    MY_ARG=0
    while [ $# -gt 0 ];
    do
        case "$1" in
            # boolean
            "--hwvirt")                 TESTBOXSCRIPT_HWVIRT="yes";;
            "--no-hwvirt")              TESTBOXSCRIPT_HWVIRT="no";;
            "--nested-paging")          TESTBOXSCRIPT_NESTED_PAGING="yes";;
            "--no-nested-paging")       TESTBOXSCRIPT_NESTED_PAGING="no";;
            "--io-mmu")                 TESTBOXSCRIPT_IOMMU="yes";;
            "--no-io-mmu")              TESTBOXSCRIPT_IOMMU="no";;
            # optios taking values.
            "--system-uuid")            TESTBOXSCRIPT_SYSTEM_UUID="$2"; shift;;
            "--scratch-root")           TESTBOXSCRIPT_SCRATCH_ROOT="$2"; shift;;
            "--test-manager")           TESTBOXSCRIPT_TEST_MANAGER="$2"; shift;;
            "--builds-path")            TESTBOXSCRIPT_BUILDS_PATH="$2"; shift;;
            "--builds-server-type")     TESTBOXSCRIPT_BUILDS_TYPE="$2"; shift;;
            "--builds-server-name")     TESTBOXSCRIPT_BUILDS_NAME="$2"; shift;;
            "--builds-server-share")    TESTBOXSCRIPT_BUILDS_SHARE="$2"; shift;;
            "--builds-server-user")     TESTBOXSCRIPT_BUILDS_USER="$2"; shift;;
            "--builds-server-passwd")   TESTBOXSCRIPT_BUILDS_PASSWD="$2"; shift;;
            "--builds-server-mountopt") TESTBOXSCRIPT_BUILDS_MOUNTOPT="$2"; shift;;
            "--testrsrc-path")          TESTBOXSCRIPT_TESTRSRC_PATH="$2"; shift;;
            "--testrsrc-server-type")   TESTBOXSCRIPT_TESTRSRC_TYPE="$2"; shift;;
            "--testrsrc-server-name")   TESTBOXSCRIPT_TESTRSRC_NAME="$2"; shift;;
            "--testrsrc-server-share")  TESTBOXSCRIPT_TESTRSRC_SHARE="$2"; shift;;
            "--testrsrc-server-user")   TESTBOXSCRIPT_TESTRSRC_USER="$2"; shift;;
            "--testrsrc-server-passwd") TESTBOXSCRIPT_TESTRSRC_PASSWD="$2"; shift;;
            "--testrsrc-server-mountopt") TESTBOXSCRIPT_TESTRSRC_MOUNTOPT="$2"; shift;;
            "--spb")                     ;;
            "--putenv")
                MY_FOUND=no
                MY_VAR=`echo $2 | sed -e 's/=.*$//' `
                for i in ${!TESTBOXSCRIPT_ENVVARS[@]};
                do
                    MY_CURVAR=`echo "${TESTBOXSCRIPT_ENVVARS[i]}" | sed -e 's/=.*$//' `
                    if [ -n "${MY_CURVAR}" -a  "${MY_CURVAR}" = "${MY_VAR}" ]; then
                        TESTBOXSCRIPT_ENVVARS[$i]="$2"
                        MY_FOUND=yes
                    fi
                done
                if [ "${MY_FOUND}" = "no" ]; then
                    TESTBOXSCRIPT_ENVVARS=( "${TESTBOXSCRIPT_ENVVARS[@]}" "$2" );
                fi
                shift;;
            --*)
                echo "error: Unknown option '$1' in existing config"
                exit 1
                ;;

            # Non-option bits.
            *.py) ;; # ignored, should be the script.

            *)  if [ ${MY_ARG} -ne 0 ]; then
                    echo "error: unknown non-option '$1' in existing config"
                    exit 1
                fi
                TESTBOXSCRIPT_PYTHON="$1"
                ;;
        esac
        shift
        MY_ARG=$((${MY_ARG} + 1))
    done
}

##
# Used by common_compile_testboxscript_command_line, please override.
#
os_add_args() {
    echo "os_add_args is not implemented" 2>&1
    exit 1
}

##
# Compiles the testboxscript.py command line given the current
# configuration and defaults.
#
# This is used by solaris and darwin.
#
# The os_add_args function will be called several with one or two arguments
# each time.  The caller must override it.
#
common_compile_testboxscript_command_line() {
    if [ -n "${TESTBOXSCRIPT_PYTHON}" ]; then
        os_add_args "${TESTBOXSCRIPT_PYTHON}"
    fi
    os_add_args "${TESTBOXSCRIPT_DIR}/testboxscript/testboxscript.py"

    for var in ${TESTBOXSCRIPT_CFG_NAMES};
    do
        varcfg=TESTBOXSCRIPT_${var}
        vardef=TESTBOXSCRIPT_DEFAULT_${var}
        if [ "${!varcfg}" != "${!vardef}"  -a  "${var}" != "PYTHON" ]; then # PYTHON handled above.
            my_opt=TESTBOXSCRIPT_OPT_${var}
            if [ -n "${!my_opt}" ]; then
                if [ "${!my_opt}"  == "--spb" ]; then
                    os_add_args "${!my_opt}"
                elif [ "${!my_opt}"  != "--skip" ]; then
                    os_add_args "${!my_opt}" "${!varcfg}"
                fi
            else
                my_opt_yes=${my_opt}_YES
                my_opt_no=${my_opt}_NO
                if [ -n "${!my_opt_yes}" -a -n "${!my_opt_no}" ]; then
                    if [ "${!varcfg}" = "yes" ]; then
                        os_add_args "${!my_opt_yes}";
                    else
                        if [ "${!varcfg}" != "no" ]; then
                            echo "internal option misconfig: var=${var} not a yes/no value: ${!varcfg}";
                            exit 1;
                        fi
                        os_add_args "${!my_opt_yes}";
                    fi
                else
                    echo "internal option misconfig: var=${var} my_opt_yes=${my_opt_yes}=${!my_opt_yes} my_opt_no=${my_opt_no}=${!my_opt_no}"
                    exit 1;
                fi
            fi
        fi
    done

    i=0
    while [ "${i}" -lt "${#TESTBOXSCRIPT_ENVVARS[@]}" ];
    do
        os_add_args "--putenv" "${TESTBOXSCRIPT_ENVVARS[${i}]}"
        i=$((${i} + 1))
    done
}


#
#
# main()
#
#


#
# Get our bearings and include the host specific code.
#
MY_ETC_SUDOERS="/etc/sudoers"
MY_FGREP=fgrep
DIR=`dirname "$0"`
DIR=`cd "${DIR}"; /bin/pwd`

get_host_os
HOST_OS=${RETVAL}
get_host_arch
HOST_ARCH=${RETVAL}

. "${DIR}/${HOST_OS}/setup-routines.sh"


#
# Config.
#
TESTBOXSCRIPT_CFG_NAMES="DIR PYTHON USER HWVIRT IOMMU NESTED_PAGING SYSTEM_UUID PATH_TESTRSRC TEST_MANAGER SCRATCH_ROOT"
TESTBOXSCRIPT_CFG_NAMES="${TESTBOXSCRIPT_CFG_NAMES} BUILDS_PATH   BUILDS_TYPE   BUILDS_NAME   BUILDS_SHARE   BUILDS_USER"
TESTBOXSCRIPT_CFG_NAMES="${TESTBOXSCRIPT_CFG_NAMES} BUILDS_PASSWD BUILDS_MOUNTOPT TESTRSRC_PATH TESTRSRC_TYPE TESTRSRC_NAME"
TESTBOXSCRIPT_CFG_NAMES="${TESTBOXSCRIPT_CFG_NAMES} TESTRSRC_SHARE TESTRSRC_USER TESTRSRC_PASSWD TESTRSRC_MOUNTOPT SPB"

# testboxscript.py option to config mappings.
TESTBOXSCRIPT_OPT_DIR="--skip"
TESTBOXSCRIPT_OPT_PYTHON="--skip"
TESTBOXSCRIPT_OPT_USER="--skip"
TESTBOXSCRIPT_OPT_HWVIRT_YES="--hwvirt"
TESTBOXSCRIPT_OPT_HWVIRT_NO="--no-hwvirt"
TESTBOXSCRIPT_OPT_NESTED_PAGING_YES="--nested-paging"
TESTBOXSCRIPT_OPT_NESTED_PAGING_NO="--no-nested-paging"
TESTBOXSCRIPT_OPT_IOMMU_YES="--io-mmu"
TESTBOXSCRIPT_OPT_IOMMU_NO="--no-io-mmu"
TESTBOXSCRIPT_OPT_SPB="--spb"
TESTBOXSCRIPT_OPT_SYSTEM_UUID="--system-uuid"
TESTBOXSCRIPT_OPT_TEST_MANAGER="--test-manager"
TESTBOXSCRIPT_OPT_SCRATCH_ROOT="--scratch-root"
TESTBOXSCRIPT_OPT_BUILDS_PATH="--builds-path"
TESTBOXSCRIPT_OPT_BUILDS_TYPE="--builds-server-type"
TESTBOXSCRIPT_OPT_BUILDS_NAME="--builds-server-name"
TESTBOXSCRIPT_OPT_BUILDS_SHARE="--builds-server-share"
TESTBOXSCRIPT_OPT_BUILDS_USER="--builds-server-user"
TESTBOXSCRIPT_OPT_BUILDS_PASSWD="--builds-server-passwd"
TESTBOXSCRIPT_OPT_BUILDS_MOUNTOPT="--builds-server-mountopt"
TESTBOXSCRIPT_OPT_PATH_TESTRSRC="--testrsrc-path"
TESTBOXSCRIPT_OPT_TESTRSRC_TYPE="--testrsrc-server-type"
TESTBOXSCRIPT_OPT_TESTRSRC_NAME="--testrsrc-server-name"
TESTBOXSCRIPT_OPT_TESTRSRC_SHARE="--testrsrc-server-share"
TESTBOXSCRIPT_OPT_TESTRSRC_USER="--testrsrc-server-user"
TESTBOXSCRIPT_OPT_TESTRSRC_PASSWD="--testrsrc-server-passwd"
TESTBOXSCRIPT_OPT_TESTRSRC_MOUNTOPT="--testrsrc-server-mountopt"

# Defaults:
TESTBOXSCRIPT_DEFAULT_DIR="there-is-no-default-for-this-value"
TESTBOXSCRIPT_DEFAULT_PYTHON=""
TESTBOXSCRIPT_DEFAULT_USER="vbox"
TESTBOXSCRIPT_DEFAULT_HWVIRT=""
TESTBOXSCRIPT_DEFAULT_IOMMU=""
TESTBOXSCRIPT_DEFAULT_NESTED_PAGING=""
TESTBOXSCRIPT_DEFAULT_SPB=""
TESTBOXSCRIPT_DEFAULT_SYSTEM_UUID=""
TESTBOXSCRIPT_DEFAULT_PATH_TESTRSRC=""
TESTBOXSCRIPT_DEFAULT_TEST_MANAGER=""
TESTBOXSCRIPT_DEFAULT_SCRATCH_ROOT=""
TESTBOXSCRIPT_DEFAULT_BUILDS_PATH=""
TESTBOXSCRIPT_DEFAULT_BUILDS_TYPE="cifs"
TESTBOXSCRIPT_DEFAULT_BUILDS_NAME="10.165.98.144"
TESTBOXSCRIPT_DEFAULT_BUILDS_SHARE="builds"
TESTBOXSCRIPT_DEFAULT_BUILDS_USER="guestr"
TESTBOXSCRIPT_DEFAULT_BUILDS_PASSWD="guestr"
TESTBOXSCRIPT_DEFAULT_BUILDS_MOUNTOPT=""
TESTBOXSCRIPT_DEFAULT_TESTRSRC_PATH=""
TESTBOXSCRIPT_DEFAULT_TESTRSRC_TYPE="cifs"
TESTBOXSCRIPT_DEFAULT_TESTRSRC_NAME="teststor.de.oracle.com"
TESTBOXSCRIPT_DEFAULT_TESTRSRC_SHARE="testrsrc"
TESTBOXSCRIPT_DEFAULT_TESTRSRC_USER="guestr"
TESTBOXSCRIPT_DEFAULT_TESTRSRC_PASSWD="guestr"
TESTBOXSCRIPT_DEFAULT_TESTRSRC_MOUNTOPT=""

# Set config values to defaults.
for var in ${TESTBOXSCRIPT_CFG_NAMES}
do
    defvar=TESTBOXSCRIPT_DEFAULT_${var}
    eval TESTBOXSCRIPT_${var}="${!defvar}"
done
declare -a TESTBOXSCRIPT_ENVVARS

# Load old config values (platform specific).
os_load_config


#
# Config tweaks.
#

# The USER must be a non-empty value for the successful execution of this script.
if [ -z "${TESTBOXSCRIPT_USER}" ]; then
    TESTBOXSCRIPT_USER=${TESTBOXSCRIPT_DEFAULT_USER};
fi;

# The DIR must be according to the setup.sh location.
TESTBOXSCRIPT_DIR=`dirname "${DIR}"`

# Storage server replacement trick.
if [ "${TESTBOXSCRIPT_BUILDS_NAME}" = "vboxstor.de.oracle.com" ]; then
    TESTBOXSCRIPT_BUILDS_NAME=${TESTBOXSCRIPT_DEFAULT_BUILDS_NAME}
fi
if [ "${TESTBOXSCRIPT_TESTRSRC_NAME}" = "solserv.de.oracle.com" ]; then
    TESTBOXSCRIPT_TESTRSRC_NAME=${TESTBOXSCRIPT_DEFAULT_TESTRSRC_NAME}
fi


#
# Parse arguments.
#
while test $# -gt 0;
do
    case "$1" in
        -h|--help)
            echo "TestBox Script setup utility."
            echo "";
            echo "Usage: setup.sh [options]";
            echo "";
            echo "Options:";
            echo "    Later...";
            exit 0;
            ;;
        -V|--version)
            echo '$Revision: 106320 $'
            exit 0;
            ;;

        --python)                   TESTBOXSCRIPT_PYTHON="$2"; shift;;
        --test-manager)             TESTBOXSCRIPT_TEST_MANAGER="$2"; shift;;
        --scratch-root)             TESTBOXSCRIPT_SCRATCH_ROOT="$2"; shift;;
        --system-uuid)              TESTBOXSCRIPT_SYSTEM_UUID="$2"; shift;;
        --hwvirt)                   TESTBOXSCRIPT_HWVIRT="yes";;
        --no-hwvirt)                TESTBOXSCRIPT_HWVIRT="no";;
        --nested-paging)            TESTBOXSCRIPT_NESTED_PAGING="yes";;
        --no-nested-paging)         TESTBOXSCRIPT_NESTED_PAGING="no";;
        --io-mmu)                   TESTBOXSCRIPT_IOMMU="yes";;
        --no-io-mmu)                TESTBOXSCRIPT_IOMMU="no";;
        --builds-path)              TESTBOXSCRIPT_BUILDS_PATH="$2"; shift;;
        --builds-server-type)       TESTBOXSCRIPT_BUILDS_TYPE="$2"; shift;;
        --builds-server-name)       TESTBOXSCRIPT_BUILDS_NAME="$2"; shift;;
        --builds-server-share)      TESTBOXSCRIPT_BUILDS_SHARE="$2"; shift;;
        --builds-server-user)       TESTBOXSCRIPT_BUILDS_USER="$2"; shift;;
        --builds-server-passwd)     TESTBOXSCRIPT_BUILDS_PASSWD="$2"; shift;;
        --builds-server-mountopt)   TESTBOXSCRIPT_BUILDS_MOUNTOPT="$2"; shift;;
        --testrsrc-path)            TESTBOXSCRIPT_TESTRSRC_PATH="$2"; shift;;
        --testrsrc-server-type)     TESTBOXSCRIPT_TESTRSRC_TYPE="$2"; shift;;
        --testrsrc-server-name)     TESTBOXSCRIPT_TESTRSRC_NAME="$2"; shift;;
        --testrsrc-server-share)    TESTBOXSCRIPT_TESTRSRC_SHARE="$2"; shift;;
        --testrsrc-server-user)     TESTBOXSCRIPT_TESTRSRC_USER="$2"; shift;;
        --testrsrc-server-passwd)   TESTBOXSCRIPT_TESTRSRC_PASSWD="$2"; shift;;
        --testrsrc-server-mountopt) TESTBOXSCRIPT_TESTRSRC_MOUNTOPT="$2"; shift;;
        --spb)                      TESTBOXSCRIPT_SPB="yes";;
        *)
            echo 'Syntax error: Unknown option:' "$1" >&2;
            exit 1;
            ;;
    esac
    shift;
done


#
# Find usable python if not already specified.
#
if [ -z "${TESTBOXSCRIPT_PYTHON}" ]; then
    set +e
    MY_PYTHON_VER_TEST="\
import sys;\
x = (sys.version_info[0] == 3 and sys.version_info[1] >= 5) or (sys.version_info[0] == 2 and sys.version_info[1] >= 7);\
sys.exit(not x);\
";
    for python in python3.{30..5} python3 python2.7 python2 python;
    do
        # python=`which ${python} 2> /dev/null`
        python=`command -v ${python} 2> /dev/null`
        if [ -n "${python}" -a -x "${python}" ]; then
            if ${python} -c "${MY_PYTHON_VER_TEST}"; then
                TESTBOXSCRIPT_PYTHON="${python}";
                break;
            fi
        fi
    done
    set -e
    test -n "${TESTBOXSCRIPT_PYTHON}";
fi


#
# Do the job
#
set -e
check_testboxscript_install;
check_for_sudo;
check_for_cifs;
check_proxy_config;

maybe_add_testboxscript_user;
test_user;
test_coredumps;
test_unattended_updates_disabled;

grant_user_testboxscript_write_access;

os_disable_service;
os_install_service;
os_enable_service;

#
# That's all folks.
#
echo "done"
os_final_message;
