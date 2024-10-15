#!/usr/bin/env kmk_ash
# $Id: tstIEMAImplData.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Shell script for massaging a data file to stub missing instructions.
#

#
# Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

# Get parameters.
     CP="$1"
     MV="$2"
    SED="$3"
 APPEND="$4"
 OUTDIR="$5"
 SRCDIR="$6"
   FILE="$7"

# Globals.
set -e
LC_ALL=C
export LC_ALL
SRCFILE="${SRCDIR}/tstIEMAImplData${FILE}.cpp"
OUTFILE="${OUTDIR}/tstIEMAImplData${FILE}.cpp"

# Copy the file and deal with empty file.
if test -f "${SRCFILE}"; then
    "${CP}" -f -- "${SRCFILE}" "${OUTFILE}.tmp"
else
    "${APPEND}" -t "${OUTFILE}.tmp"
fi
if ! test -s "${OUTFILE}.tmp"; then
    echo '#include "tstIEMAImpl.h"' >> "${OUTFILE}.tmp"
fi
"${APPEND}" "${OUTFILE}.tmp" ""

# Stub empty test arrays.
"${SED}" -n -e 's/\r//' \
    -e 's/TSTIEM_DECLARE_TEST_ARRAY[(]'"${FILE}"', *\([^,]*\), *\([^ ][^ ]*\) *[)];/\1\n\2/p' \
    "${SRCDIR}/tstIEMAImpl.h" \
    --output-binary="${OUTFILE}.tmp2"

while IFS= read -r a_Type && IFS= read -r a_Instr;
do
    if "${SED}" -n -e "/ const g_cTests_${a_Instr} /q1" "${OUTFILE}.tmp"; then
        "${APPEND}" "${OUTFILE}.tmp" "TSTIEM_DEFINE_EMPTY_TEST_ARRAY(${a_Type}, ${a_Instr});"
    fi
done < "${OUTFILE}.tmp2"

# Put the file into place, removing the tmp2 file in the process (avoid needing RM).
"${MV}" -f -- "${OUTFILE}.tmp" "${OUTFILE}.tmp2"
"${MV}" -f -- "${OUTFILE}.tmp2" "${OUTFILE}"

