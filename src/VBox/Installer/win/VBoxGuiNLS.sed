# $Id: VBoxGuiNLS.sed 110270 2025-07-16 17:58:34Z klaus.espenlaub@oracle.com $
## @file
# VBox Installer - SED script for converting the VirtualBox GUI NLS file list
#                  to the WiX include file
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

1 i <Include>
s/^\(.*\)$/    <File Id="nlsqt\1" Name="qt_\1.qm" LongName="qt_\1.qm" DiskId="1" Vital="yes"\
          src="\$(env.PATH_OUT)\\bin\\nls\\qt_\1.qm" \/>\
    <File Id="nlsgui\1" LongName="VirtualBox_\1.qm" DiskId="1" Vital="yes"\
          src="\$(env.PATH_OUT)\\bin\\nls\\VirtualBox_\1.qm" \/>/
$ a </Include>
