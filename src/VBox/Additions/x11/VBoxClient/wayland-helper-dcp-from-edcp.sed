#  $Id: wayland-helper-dcp-from-edcp.sed 114743 2026-07-21 18:31:58Z knut.osmundsen@oracle.com $ */
## @file
# Guest Additions - Generate DCP code from EDCP.
#
# The two differs only in interface names.
#
# @note Obsolete. Only used with 'kmk VBOX_WITH_WAYLAND_ADDITIONS_LEGACY=1'.
#

#
# Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

s/\([^a-z]\)ext_/\1zwlr_/g
s/_edcp_/_dcp_/g
s/Edcp/Dcp/g
s/wl-edcp/wl-dcp/g
s/wayland-ext-dcp/wayland-dcp-legacy/g
s/Ext data control/Legacy data control/g
s/Ext Data Control Protocol (EDCP)/Legacy Data Control (DCP)/g
s/ext-data-control-v1.h/wlr-data-control-unstable-v1.h/g

