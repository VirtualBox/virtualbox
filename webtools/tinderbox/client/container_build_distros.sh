#!/bin/sh
# $Id: container_build_distros.sh 114035 2026-04-27 18:22:02Z vadim.galitsyn@oracle.com $
## @file
# Starts tinderclient.pl on a tinderbox.
#
# VBOX_ADD_REV=150636 VBOX_DOC_REV=150636 ./container_build_distros.sh \
#   https://linserv.de.oracle.com/vbox/svn/branches/VBox-6.1 150721 /home/vbox/tinderbox \
#   /home/vbox/tinderbox/VBox-6.1-6.1.34-test-artifacts
#

#
# Copyright (C) 2022-2026 Oracle and/or its affiliates.
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


set -eo pipefail

function at_exit()
{
    echo "$0: exited with status $?"
}

trap at_exit EXIT

usage()
{
    echo "$0: $1"
    echo "$0: usage: $0 <SVN URL> <SVN revision> <containers home dir> <output dir>"
    echo "$0:"
    echo "$0: In order to use custom revisions for VBoxEfiFirmware, VBoxGuestAdditions and VBoxDocumentation pre-built"
    echo "$0: archives, one or more of correponding environment variables can be set prior to running this script:"
    echo "$0: VBOX_EFI_REV, VBOX_ADD_REV and VBOX_DOC_REV. Note, revision should be specified as number without"
    echo "$0: leading 'r' character. Corresponding pre-built archive will be taken from a location on the build server"
    echo "$0: which corresponds to a branch which <SVN URL> points to."
    exit 1
}

[ $# != 4 ] && usage "incomplete command line"

SVN_URL="$1"
SVN_REV="$2"
CONTAINERS_DIR="$3"
OUT_DIR="$4"

[ -z "$SVN_URL" ] && usage "no svn url"
[ -z "$SVN_REV" ] && usage "no svn revision"

[ -z "$CONTAINERS_DIR" ]        && usage "no containers dir"
[[ ! "$CONTAINERS_DIR=" = /* ]] && usage "containers dir cannot be specified as a relative path"

[ -z "$OUT_DIR" ]       && usage "no output dir"
[[ ! "$OUT_DIR" = /* ]] && usage "output dir cannot be specified as a relative path"

###
# End of support dates per distribution:
###
#
# 07/2029 (End of Premier Support): OL8
# 07/2032 (End of Premier Support): OL8
#
# 04/2025 (End of statndard support): Ubuntu 20.04
# 04/2029 (End of statndard support): Ubuntu 22.04
# 04/2027 (End of statndard support): Ubuntu 24.04
# 04/2025 (End of statndard support): Ubuntu 24.10
#
# 11/2024 (End of life): Fedora 39
# 05/2025 (End of life): Fedora 40
# 11/2025 (End of life): Fedora 41
#
# 08/2026 (End of Life): Debian 11.0
# 06/2028 (End of Life): Debian 12.0
#
# 12/2024 (Maintained until at least): OpenSuse 15.5 (pkg 15.3)
# 12/2025 (Maintained until at least): OpenSuse 15.6 (pkg 15.3)
distros="
    ol8.10-amd64
    ol9.0-amd64
    ol10.1-amd64
    ubuntu22.04-amd64
    ubuntu24.04-amd64
    ubuntu25.04-amd64
    ubuntu25.10-amd64
    ubuntu26.04-amd64
    fedora40.0-amd64
    fedora45.0-amd64
    debian11.0-amd64
    debian12.0-amd64
    debian13.0-amd64
    opensuse15.6-amd64
    opensuse16.0-amd64
"
(
    cd $containers_root

    for distro in $distros; do
        echo "$0: start building $distro"
        $(dirname $0)/./container_build.sh $distro "$SVN_URL" "$SVN_REV" "$CONTAINERS_DIR" "$OUT_DIR"
    done
)
