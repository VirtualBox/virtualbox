#!/usr/bin/env kmk_ash
## @file
# VirtualBox Validation Kit - Creates VM for the Bootsector Tests.
#

#
# Copyright (C) 2026 Oracle and/or its affiliates.
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
# Figure the script directory.
#
if false && dirname 1 > /dev/null 2>&1; then
    if readlink . > /dev/null 2>&1; then
        SCRIPT_DIR=$(readlink -f "$0")
        SCRIPT_DIR=$(dirname "${SCRIPT_DIR}")
    else
        SCRIPT_DIR=$(dirname "$0")
        SCRIPT_DIR=$(cd "${SCRIPT_DIR}" && pwd)
    fi
else
    SCRIPT_DIR=$(echo "$0" | kmk_sed -e 's,\\,/,g' -e 's,/[^/][^/]*$,,' -e 's,^[^/][^/]*$,.,' )
    echo ${SCRIPT_DIR}
fi
if test "${SCRIPT_DIR}" = "."; then
    SCRIPT_DIR=$(pwd)
fi
if test -z "${SCRIPT_DIR}"; then
    echo "error: failed to determin the script directory." >&2
    exit 1;
fi

#
# Establish the basic environment.
#
if test -z "${KBUILD_TYPE}"; then
    KBUILD_TYPE=debug;
fi
if test -z "${KBUILD_TARGET}"; then
    if test -n "${windir}"; then
        KBUILD_TARGET=win;
    elif test "${OSTYPE}" = "linux-gnu"; then
        KBUILD_TARGET=linux;
    else
        echo "error: Unknown KBUILD_TARGET." >&2
        exit 1;
    fi
fi
if test -z "${KBUILD_TARGET_ARCH}"; then
    if test "${HOSTTYPE}" = "x86_64" -o "${PROCESSOR_ARCHITECTURE}" = "AMD64"; then
        KBUILD_TARGET=amd64;
    else
        echo "error: Unknown KBUILD_TARGET_ARCH." >&2
        exit 1;
    fi
fi
if test -z "${PATH_OUT}"; then
    if test -z "${PATH_OUT_BASE}"; then
        PATH_OUT_BASE=$(cd "${SCRIPT_DIR}/../../../.." && pwd)
        if test -z "${PATH_OUT_BASE}"; then
            echo "error: failed to determine PATH_OUT_BASE." >&2
            exit 1;
        fi
        PATH_OUT_BASE="${PATH_OUT_BASE}/out"
    fi
    PATH_OUT="${PATH_OUT_BASE}/${KBUILD_TARGET}.${KBUILD_TARGET_ARCH}/${KBUILD_TYPE}"
fi


#
# Determine the VirtualBox bin and validation kit directories.
#
if test -z "${PATH_VBOX_BIN}"; then
    PATH_VBOX_BIN="${PATH_OUT}/bin";
    if test "${KBUILD_TARGET}" = "darwin"; then
        PATH_VBOX_BIN="${PATH_OUT}/dist/VirtualBox.app/Contents/MacOS";
    fi
fi
if test -z "${PATH_VALKIT}"; then
    PATH_VALKIT="${PATH_OUT}/validationkit";
fi

#
# TODO: Parse arguments...
#
OPT_VMGROUP="/BootSectors"
if test "$#" -ne "0"; then
    echo "syntax error: paraemter parsing not implemented..." >&2
    exit 2;
fi


#
# Some final path variables.
#
EXESUFF=""
test "${KBUILD_TARGET}" = "win" && EXESUFF=".exe"

VBOXMANAGE="${PATH_VBOX_BIN}/VBoxManage${EXESUFF}"

#
# Function for creating a VM.
#
CreateBootSectorVm()
{
    VM_NAME="$1"
    FLOPPY_FILE="${PATH_VALKIT}/bootsectors/$2"
    ENABLE_TESTING_MMIO=
    DISABLE_TESTING_MMIO=
    shift 2
    while test "$#" -gt 0;
    do
        case "$1" in
            --disable-testing)      DISABLE_TESTING_MMIO=yes;;
            --testing-mmio)         ENABLE_TESTING_MMIO=yes;;
            *) echo "syntax error: Unknown option $1" >&2; exit 2;;
        esac
        shift
    done

    # Check if the VM already exists.
    set -e
    if "${VBOXMANAGE}" showvminfo "${VM_NAME}" 2> /dev/null > /dev/null; then
        echo "info: ${VM_NAME} already exists, reconfiguring..."
    else
        echo "info: Creating ${VM_NAME}..."
        "${VBOXMANAGE}" createvm --name="${VM_NAME}" --groups="${OPT_VMGROUP}" --register \
            --platform-architecture=x86 --ostype=VBoxBS_64 --default
    fi
    "${VBOXMANAGE}" storageattach "${VM_NAME}" --storagectl="Floppy" --port=0 --device=0 --type=fdd \
        --medium="${FLOPPY_FILE}"
    if test -n "${DISABLE_TESTING_MMIO}"; then
        "${VBOXMANAGE}" setextradata "${VM_NAME}" "VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled"
    fi
    if test -n "${ENABLE_TESTING_MMIO}"; then
        "${VBOXMANAGE}" setextradata "${VM_NAME}" "VBoxInternal/Devices/VMMDev/0/Config/TestingMMIO" "1"
    fi
    set +e
}


# Plain simple boot sectors.
CreateBootSectorVm "bs-shutdown"                "bootsector-shutdown.img"           --disable-testing
CreateBootSectorVm "bs-pae"                     "bootsector-pae.img"                --disable-testing

# Boot Sector Kit v2
CreateBootSectorVm "bs2-test1"                  "bootsector2-test1.img"             --testing-mmio
CreateBootSectorVm "bs2-cpu-hidden-regs-1"      "bootsector2-cpu-hidden-regs-1.img"
CreateBootSectorVm "bs2-cpu-instr-1"            "bootsector2-cpu-instr-1.img"
CreateBootSectorVm "bs2-cpu-pf-1"               "bootsector2-cpu-pf-1.img"
CreateBootSectorVm "bs2-cpu-xcpt-1"             "bootsector2-cpu-xcpt-1.img"
CreateBootSectorVm "bs2-cpu-xcpt-2"             "bootsector2-cpu-xcpt-2.img"
CreateBootSectorVm "bs2-cpu-a20-1"              "bootsector2-cpu-a20-1.img"
CreateBootSectorVm "bs2-cpu-basic-1"            "bootsector2-cpu-basic-1.img"
CreateBootSectorVm "bs2-cpu-ac-loop"            "bootsector2-cpu-ac-loop.img"       # triggers VERR_EM_GUEST_CPU_HANG guru
CreateBootSectorVm "bs2-cpu-db-loop"            "bootsector2-cpu-db-loop.img"       # gets stuck (on AMD-V at least)
CreateBootSectorVm "bs2-boot-registers-1"       "bootsector2-boot-registers-1.img"  # Displays the boots regs and halts!
CreateBootSectorVm "bs2-triple-fault-1"         "bootsector2-triple-fault-1.img"

# Boot Sector Kit v3
CreateBootSectorVm "bs3-apic-1"                 "bs3-apic-1.img"
CreateBootSectorVm "bs3-cpu-basic-2"            "bs3-cpu-basic-2.img"
CreateBootSectorVm "bs3-cpu-basic-3"            "bs3-cpu-basic-3.img"
CreateBootSectorVm "bs3-cpu-weird-1"            "bs3-cpu-weird-1.img"
CreateBootSectorVm "bs3-cpu-state64-1"          "bs3-cpu-state64-1.img"
CreateBootSectorVm "bs3-cpu-decoding-1"         "bs3-cpu-decoding-1.img"
CreateBootSectorVm "bs3-cpu-instr-2"            "bs3-cpu-instr-2.img"
CreateBootSectorVm "bs3-cpu-instr-3"            "bs3-cpu-instr-3.img"
CreateBootSectorVm "bs3-cpu-instr-4"            "bs3-cpu-instr-4.img"
CreateBootSectorVm "bs3-cpu-instr-5"            "bs3-cpu-instr-5.img"
CreateBootSectorVm "bs3-cpu-generated-1"        "bs3-cpu-generated-1.img"
CreateBootSectorVm "bs3-fpustate-1"             "bs3-fpustate-1.img"            --testing-mmio
CreateBootSectorVm "bs3-memalloc-1"             "bs3-memalloc-1.img"
CreateBootSectorVm "bs3-timers-1"               "bs3-timers-1.img"
CreateBootSectorVm "bs3-timing-1"               "bs3-timing-1.img"
CreateBootSectorVm "bs3-locking-1"              "bs3-locking-1.img"

