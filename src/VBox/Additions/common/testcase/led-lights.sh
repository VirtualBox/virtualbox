#!/bin/bash
# $Id: led-lights.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox guest LED demonstration test
#

#
# Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#       led-lights.sh [-a | -r]
#

#
# Test script to twiddle the console LEDs of a VirtualBox VM.
#
# This is not an automated test, just something for humans to look
# at, to convince themselves that the VM console LEDs are working.
# By default it cycles through the LED types in a specific order.
#
# '-a' twiddles all possible LEDs at the same time
# '-r' reverses the default order
#
# For instance, run the script in 2 VMs at once, one with '-r'.
#
# LEDs are not expected to track perfectly, as other OS activities
# will light them (and buffer cache effects can delay things).  Just
# make sure that all the tested ones (hard disk, optical, USB storage,
# floppy, shared folders, net) are working.  Expected activity:
#
#     - Disk & optical devices show solid 'read'
#     - Virtual USB disk & optical devices show 'write' on the USB LED
#     - Floppy devices and shared folders alternate 'read/write'
#     - Net blinks 'write'
#
# Pre-VM setup:
#
#     Download or locate a bootable Linux ISO able to be used 'live'.
#     Make a tarball of this script + extra junk:
#
#         $ dd if=/dev/zero of=junk bs=100k count=1
#         $ tar cf floppy.img led-lights.sh junk
#
#     NOTE: floppy.img must be >= 20KiB or you will get I/O errors!
#
# VM setup:
#
#     New VM; type: Linux (subtype to match ISO); create default HD.
#     VM Settings:
#         System > raise base memory to 4GiB
#         Storage > insert 'Live bootable' Linux ISO image;
#                   turn on 'Live CD/DVD'
#         Storage > add floppy controller (i82078); insert floppy.img
#         Storage > add USB controller; insert USB HD & USB CD
#         System > move Optical before Floppy in boot order
#         Shared Folders > set up one or more Shared Folders if desired
#                   (they should be permanent, auto-mount,
#                    writable, with at least 10MB free space)
#
# Boot the VM.  Open a shell, become root, optionally install
# VirtualBox Guest Utilities to access Shared Folders, then extract
# and run this script:
#
#     $ sudo bash
#     # yum install virtualbox-guest-utils   # for Shared Folders
#     # tar xf /dev/fd0 led-lights.sh
#     # ./led-lights.sh                      [-a | -r]

if [ ! -w / ]; then
    echo "Must be run as root!" 1>&2
    exit 1
fi

all_all=false
reverse=false

if [ "x$1" = "x-a" ]; then
    all_all=true
fi

if [ "x$1" = "x-r" ]; then
    reverse=true
fi

# Copy binaries to RAM tmpfs to avoid CD I/O after cache purges
MYTMP=/tmp/led-lights.$$
mkdir $MYTMP
for bin in $(which dd sleep sync); do
    case $bin in
        /*)
            cp -p $bin $MYTMP
        ;;
    esac
done
export MYTMP PATH=$MYTMP:$PATH

set -o monitor

# Make device reads keep hitting the 'hardware'
# even if the whole medium fits in cache...
drop_cache()
{
    echo 1 >/proc/sys/vm/drop_caches
}

activate()
{
    kill -CONT -$1 2>/dev/null
}

suppress()
{
    $all_all || kill -STOP -$1 2>/dev/null
}

declare -a pids pidnames
cpids=0

twiddle()
{
    let ++cpids
    new_pid=$!
    pidname=$1
    pids[$cpids]=$new_pid
    pidnames[$cpids]=$pidname
    suppress $new_pid
}

hide_stderr()
{
    exec 3>&2 2>/dev/null
}

show_stderr()
{
    exec 2>&3 3>&-
}

bail()
{
    hide_stderr
    for pid in ${pids[*]}; do
        activate $pid
        kill -TERM -$pid
        kill -TERM $pid
    done 2>/dev/null
    rm -rf $MYTMP
    kill $$
}

trap "bail" INT

drives()
{
    echo $(
        awk '$NF ~/^('$1')$/ { print $NF }' /proc/partitions
    )
}

# Prevent job control 'stopped' msgs during twiddler startup
hide_stderr

# Hard disks
for hdd in $(drives '[sh]d.'); do
    while :; do
        dd if=/dev/$hdd of=/dev/null
        drop_cache
    done 2>/dev/null &
    twiddle disk:$hdd
done

# Optical drives
for odd in $(drives 'sr.|scd.'); do
    while :; do
        dd if=/dev/$odd of=/dev/null
        drop_cache
    done 2>/dev/null &
    twiddle optical:$odd
done

# Floppy drives
for fdd in $(drives 'fd.'); do
    while :; do
        dd if=/dev/$fdd of=$MYTMP/$fdd bs=1k count=20
        dd of=/dev/$fdd if=$MYTMP/$fdd bs=1k count=20
    done 2>/dev/null &
    twiddle floppy:$fdd
done

# Shared folders
if ! lsmod | grep -q vboxsf; then
    echo
    echo "Note: to test the Shared Folders LED, install this"
    echo "distro's VirtualBox Guest Utilities package, e.g.:"
    echo
    echo "    # yum install virtualbox-guest-utils   (Red Hat family)"
    echo "    # apt install virtualbox-guest-utils   (Debian family)"
    echo
fi >&3   # original stderr
for shf in $(mount -t vboxsf | awk '{ print $3 }'); do
    while :; do
        dd if=/dev/urandom of=$shf/tmp.led-lights.$$ bs=100k count=100
        for rep in $(seq 1 10); do
            drop_cache
            dd of=/dev/zero if=$shf/tmp.led-lights.$$ bs=100k count=100
        done
        sync
        rm -f $shf/tmp.led-lights.$$
    done >/dev/null 2>&1 &
    twiddle sharedfs:$shf
done

# Network
ping -i.2 1.2.3.4 >/dev/null &
twiddle net

# Untested LED: Graphics3D -- add some day?

sleep 0.1
show_stderr

if $reverse; then
    seq=$(seq $cpids -1 1)
else
    seq=$(seq 1 $cpids)
fi

show_intr()
{
    intr=$(stty -a | sed -n '/intr/ { s/.*intr *=* *//; s/[ ;].*//p }')
    echo
    echo "[ Hit $intr to stop ]"
    echo
}

if $all_all; then
    printf "%s ...\n" ${pidnames[*]}
    show_intr
    wait
else
    CEOL=$(tput el)
    show_intr
    while :; do
        for pidx in $seq; do
            pid=${pids[$pidx]}
            pidname=${pidnames[$pidx]}
            echo -e -n "$pidname$CEOL\r"
            shift
            activate $pid
            sleep 2
            suppress $pid
            sync
            sleep .5
        done
    done
fi
